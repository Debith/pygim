# -*- coding: utf-8 -*-
"""Tests for the release workflow's version resolution (.github/scripts/release_version.py).

The script only ever runs inside GitHub Actions, where a mistake publishes the
wrong version to PyPI — which never accepts a version twice. Its decisions are
pure functions of the event, the tags and PyPI's versions, tested here.
"""

import importlib.util
import pathlib
import re

import pytest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / ".github" / "scripts" / "release_version.py"

pytestmark = pytest.mark.skipif(not SCRIPT.exists(), reason="needs the repository checkout")


@pytest.fixture(scope="module")
def rv():
    spec = importlib.util.spec_from_file_location("release_version", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TAGS = ["v0.0.8", "v0.0.9", "v0.1.0rc1", "nightly", "vnext"]


def _resolve(rv, event, ref_name="", bump="patch", version="", tags=TAGS, pypi=()):
    return rv.resolve(event, ref_name, bump, version, tags, set(pypi))


# ─── release: on release/<version>, the branch names the version ────────────


@pytest.mark.parametrize("event", ["push", "workflow_dispatch"])
def test_release_branch_names_the_version(rv, event):
    assert _resolve(rv, event, "release/0.1.0") == {"action": "release", "version": "0.1.0", "prerelease": False}


def test_release_branch_may_name_a_prerelease(rv):
    assert _resolve(rv, "push", "release/0.1.0rc2")["prerelease"] is True


@pytest.mark.parametrize("ref_name,problem", [
    ("release/v0.1.0", "not spelled canonically: use '0.1.0'"),
    ("release/0.1.0-rc1", "not spelled canonically: use '0.1.0rc1'"),
    ("release/0.1.0+local", "local part"),
    ("release/next", "not a PEP 440 version"),
    ("main", "named release/<version>"),
])
def test_malformed_release_branch_is_refused(rv, ref_name, problem):
    with pytest.raises(rv.ReleaseError, match=re.escape(problem)):
        _resolve(rv, "push", ref_name)


def test_version_input_cannot_rename_a_release_branch(rv):
    with pytest.raises(rv.ReleaseError, match="contradicts the branch"):
        _resolve(rv, "workflow_dispatch", "release/0.1.0", version="0.2.0")
    assert _resolve(rv, "workflow_dispatch", "release/0.1.0", version="0.1.0")["version"] == "0.1.0"


def test_released_version_is_refused(rv):
    with pytest.raises(rv.ReleaseError, match="v0.0.9 is already released"):
        _resolve(rv, "push", "release/0.0.9")


def test_version_on_pypi_is_refused_even_without_a_tag(rv):
    with pytest.raises(rv.ReleaseError, match="PyPI already holds"):
        _resolve(rv, "push", "release/0.1.0", pypi=["0.1.0"])


# ─── cut: dispatched on main, it names a new release branch ─────────────────


@pytest.mark.parametrize("bump,expected", [
    ("patch", "0.0.10"),
    ("minor", "0.1.0"),
    ("major", "1.0.0"),
])
def test_bump_starts_from_the_latest_final_release(rv, bump, expected):
    # v0.1.0rc1 is a prerelease: a patch bump continues the 0.0.x line.
    assert _resolve(rv, "workflow_dispatch", "main", bump=bump) == {
        "action": "cut", "version": expected, "prerelease": False,
    }


def test_bump_without_any_release_tag_starts_at_zero(rv):
    assert _resolve(rv, "workflow_dispatch", "main", bump="minor", tags=["nightly"])["version"] == "0.1.0"


def test_bump_pads_a_short_version(rv):
    assert _resolve(rv, "workflow_dispatch", "main", bump="patch", tags=["v2.1"])["version"] == "2.1.1"


def test_explicit_version_overrides_bump(rv):
    assert _resolve(rv, "workflow_dispatch", "main", bump="major", version="0.2.0b1") == {
        "action": "cut", "version": "0.2.0b1", "prerelease": True,
    }


def test_cut_of_a_released_version_is_refused(rv):
    with pytest.raises(rv.ReleaseError, match="already released"):
        _resolve(rv, "workflow_dispatch", "main", version="0.0.9")


def test_unknown_bump_is_refused(rv):
    with pytest.raises(rv.ReleaseError, match="bump must be one of"):
        _resolve(rv, "workflow_dispatch", "main", bump="huge")


def test_nothing_is_released_from_another_branch(rv):
    with pytest.raises(rv.ReleaseError, match="nothing is released from 'core/memory'"):
        _resolve(rv, "workflow_dispatch", "core/memory")


# ─── dry-run: a pull request never publishes ────────────────────────────────


def test_pull_request_is_an_unpublished_dev_build(rv):
    assert _resolve(rv, "pull_request") == {"action": "dry-run", "version": "0.0.10.dev0", "prerelease": True}


def test_other_events_are_refused(rv):
    with pytest.raises(rv.ReleaseError, match="does not run on 'schedule'"):
        _resolve(rv, "schedule")
