"""Resolve the version a run of ``.github/workflows/release.yml`` releases.

A release is named by its branch, ``release/<version>``. The workflow reaches
here from three events, and each names the version differently:

- ``push`` of ``release/<version>``: the branch name is the version.
- ``workflow_dispatch``: the ``version`` input, or else the latest released
  tag (``v<version>``) bumped by the ``bump`` input.
- ``pull_request``: a dry run; the next patch version as ``.dev0``, never
  published.

A version is released once: its tag must not exist and PyPI must not already
hold it. Outputs are written as ``key=value`` lines to ``$GITHUB_OUTPUT``
(stdout when unset).
"""

import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

from packaging.version import InvalidVersion, Version

PROJECT = "python-gimmicks"
BRANCH_PREFIX = "release/"
TAG_PREFIX = "v"
BUMPS = ("patch", "minor", "major")


class ReleaseError(Exception):
    """The run cannot release: the message says why and what to do instead."""


def parse_version(text):
    """*text* as a public PEP 440 version, spelled canonically, or ReleaseError."""
    try:
        version = Version(text)
    except InvalidVersion:
        raise ReleaseError(f"{text!r} is not a PEP 440 version (e.g. 1.2.0, 1.2.0rc1)") from None
    if version.local is not None:
        raise ReleaseError(f"{text!r} has a local part (+...), which PyPI rejects")
    if str(version) != text:
        raise ReleaseError(f"{text!r} is not spelled canonically: use {str(version)!r}")
    return version


def released_versions(tags):
    """The versions named by ``v<version>`` tags; other tags are ignored."""
    versions = []
    for tag in tags:
        if not tag.startswith(TAG_PREFIX):
            continue
        try:
            versions.append(Version(tag[len(TAG_PREFIX):]))
        except InvalidVersion:
            continue
    return versions


def bump(tags, kind):
    """The latest final release among *tags*, incremented at *kind*."""
    if kind not in BUMPS:
        raise ReleaseError(f"bump must be one of {', '.join(BUMPS)}, not {kind!r}")
    finals = [v for v in released_versions(tags) if not v.is_prerelease]
    major, minor, patch = (max(finals).release + (0, 0))[:3] if finals else (0, 0, 0)
    if kind == "major":
        return Version(f"{major + 1}.0.0")
    if kind == "minor":
        return Version(f"{major}.{minor + 1}.0")
    return Version(f"{major}.{minor}.{patch + 1}")


def resolve(event, ref_name, bump_kind, version_input, tags, pypi_versions):
    """The release a run makes: ``{"version", "publish", "prerelease"}``.

    *pypi_versions* is the set of version strings PyPI already holds.
    """
    if event == "pull_request":
        version = Version(f"{bump(tags, 'patch')}.dev0")
        return {"version": str(version), "publish": False, "prerelease": True}

    if event == "push":
        if not ref_name.startswith(BRANCH_PREFIX):
            raise ReleaseError(f"a release branch is named {BRANCH_PREFIX}<version>, not {ref_name!r}")
        version = parse_version(ref_name[len(BRANCH_PREFIX):])
    elif event == "workflow_dispatch":
        version = parse_version(version_input) if version_input else bump(tags, bump_kind)
    else:
        raise ReleaseError(f"the release workflow does not run on {event!r} events")

    if version in released_versions(tags):
        raise ReleaseError(
            f"{TAG_PREFIX}{version} is already released. A fix goes out as a new version: "
            f"cut {BRANCH_PREFIX}<next version> from the branch that holds it."
        )
    if any(Version(v) == version for v in pypi_versions):
        raise ReleaseError(f"PyPI already holds {PROJECT} {version}; PyPI never accepts a version twice.")
    return {"version": str(version), "publish": True, "prerelease": version.is_prerelease}


def _git_tags():
    out = subprocess.run(["git", "tag", "--list", f"{TAG_PREFIX}*"], capture_output=True, text=True, check=True)
    return out.stdout.split()


def _pypi_versions():
    try:
        with urllib.request.urlopen(f"https://pypi.org/pypi/{PROJECT}/json", timeout=30) as response:
            return set(json.load(response)["releases"])
    except urllib.error.HTTPError as error:
        if error.code == 404:  # never published
            return set()
        raise


def main():
    event = os.environ["GITHUB_EVENT_NAME"]
    try:
        release = resolve(
            event=event,
            ref_name=os.environ.get("GITHUB_REF_NAME", ""),
            bump_kind=os.environ.get("BUMP", "patch"),
            version_input=os.environ.get("VERSION", "").strip(),
            tags=_git_tags(),
            pypi_versions=set() if event == "pull_request" else _pypi_versions(),
        )
    except ReleaseError as error:
        print(f"::error title=Cannot release::{error}")
        return 1

    lines = [
        f"version={release['version']}",
        f"publish={str(release['publish']).lower()}",
        f"prerelease={str(release['prerelease']).lower()}",
    ]
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
