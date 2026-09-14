"""Decide what a run of ``.github/workflows/release.yml`` does, and for which version.

Nothing is released from ``main``: a release runs on its branch,
``release/<version>``. A run takes one of three actions:

- ``cut``: ``workflow_dispatch`` on ``main``. The version is the ``version``
  input, or else the latest released tag (``v<version>``) bumped by the
  ``bump`` input. The run creates ``release/<version>`` and starts the release
  on it; it builds nothing itself.
- ``release``: a ``push`` to ``release/<version>``, or ``workflow_dispatch``
  on it. The branch name is the version. The run builds, tests and publishes.
- ``dry-run``: ``pull_request``. The next patch version as ``.dev0``; built and
  tested, never published.

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
    """What the run does: ``{"action", "version", "prerelease"}``.

    *pypi_versions* is the set of version strings PyPI already holds.
    """
    if event == "pull_request":
        version = Version(f"{bump(tags, 'patch')}.dev0")
        return {"action": "dry-run", "version": str(version), "prerelease": True}

    if event == "workflow_dispatch" and ref_name == "main":
        action = "cut"
        version = parse_version(version_input) if version_input else bump(tags, bump_kind)
    elif event in ("push", "workflow_dispatch") and ref_name.startswith(BRANCH_PREFIX):
        action = "release"
        version = parse_version(ref_name[len(BRANCH_PREFIX):])
        if version_input and version_input != str(version):
            raise ReleaseError(
                f"the version input {version_input!r} contradicts the branch {ref_name!r}; "
                f"a release branch is its own version"
            )
    elif event == "workflow_dispatch":
        raise ReleaseError(
            f"nothing is released from {ref_name!r}: run the workflow on main to cut a "
            f"release branch, or on {BRANCH_PREFIX}<version> to release it"
        )
    elif event == "push":
        raise ReleaseError(f"a release branch is named {BRANCH_PREFIX}<version>, not {ref_name!r}")
    else:
        raise ReleaseError(f"the release workflow does not run on {event!r} events")

    if version in released_versions(tags):
        raise ReleaseError(
            f"{TAG_PREFIX}{version} is already released. A fix goes out as a new version: "
            f"cut {BRANCH_PREFIX}<next version> from the branch that holds it."
        )
    if any(Version(v) == version for v in pypi_versions):
        raise ReleaseError(f"PyPI already holds {PROJECT} {version}; PyPI never accepts a version twice.")
    return {"action": action, "version": str(version), "prerelease": version.is_prerelease}


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
        f"action={release['action']}",
        f"version={release['version']}",
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
