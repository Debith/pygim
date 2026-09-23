# -*- coding: utf-8 -*-
"""What the program reads from outside itself, read once where it is wired (`_pygim._config`).

`read` takes the machine as arguments — the variables, the working directory, the home directory,
the platform — so every case here is plain values, with no machine arranged around it. That was the
module's promise, and until 2026-09-23 it reached past its own arguments to the real machine: four
review sessions that day found it, and each test below fails on the code as it stood.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from _pygim import _config

HOME, CWD = Path("/pretend/home"), Path("/pretend/cwd")


def read(variables, **kw):
    return _config.read(variables, cwd=kw.pop("cwd", CWD), home=kw.pop("home", HOME), platform="linux", **kw)


class TestPathsAreReadAgainstWhatWasGiven:
    """`expanduser()` asks the process for $HOME and `resolve()` asks it for the working directory,
    whatever `home` and `cwd` the caller handed in — so a test that named its own world got the
    developer's, and so did any caller that moved `cwd`."""

    def test_a_tilde_means_the_home_that_was_given(self):
        assert read({"PYGIM_ENACT_GLOBAL": "~/stores/global"}).global_root == HOME / "stores" / "global"

    def test_a_relative_path_means_the_directory_that_was_given(self):
        assert read({"PYGIM_ENACT_GLOBAL": "stores/global"}).global_root == CWD / "stores" / "global"

    def test_the_store_root_is_resolved_the_same_way_and_kept_as_a_path(self):
        where = read({"PYGIM_ENACT_ROOT": "~/proj/.enact"})
        assert where.store_root == HOME / "proj" / ".enact" and where.store_root_from == "$PYGIM_ENACT_ROOT"
        flagged = read({}).with_root("rel/.enact")
        assert flagged.store_root == CWD / "rel" / ".enact" and flagged.store_root_from == "--root"


class TestAValueThatIsNotANumberIsNotASession:
    @pytest.mark.parametrize("value, session", [("7", 7), (" 7 ", 7), ("²", None), ("abc", None), ("", None)])
    def test_the_session_is_a_number_or_nothing_and_never_a_crash(self, value, session):
        assert read({"PYGIM_ENACT_SESSION": value}).session == session


class TestTheSwitches:
    def test_no_color_counts_only_when_it_is_not_empty(self):
        """no-color.org: "when present and not an empty string (regardless of its value)"."""
        assert read({"NO_COLOR": "1"}).colour is False
        assert read({"NO_COLOR": ""}).colour is True
        assert read({"PYGIM_NO_COLOR": ""}).colour is True
        assert read({"TERM": "dumb"}).colour is False

    @pytest.mark.parametrize("value, reloaded", [("1", True), ("true", True), ("0", False), ("", False),
                                                 ("false", False)])
    def test_reloaded_is_what_the_value_says_not_merely_that_it_is_there(self, value, reloaded):
        assert read({"PYGIM_ENACT_RELOADED": value}).reloaded is reloaded


class TestWhereUserStoresLive:
    """The docstring says the new place is used "if it holds anything"; the code asked only whether the
    directory existed, so a stray empty `pygim/enact/` hid every store in `pygim/memory/`."""

    def test_an_empty_new_directory_does_not_hide_the_stores_in_the_old_one(self, tmp_path):
        (tmp_path / "data" / "pygim" / "enact").mkdir(parents=True)
        (tmp_path / "data" / "pygim" / "memory" / "global").mkdir(parents=True)
        where = read({"XDG_DATA_HOME": str(tmp_path / "data")}, home=tmp_path)
        assert where.user_data == tmp_path / "data" / "pygim" / "memory"

    def test_the_new_directory_wins_once_it_holds_a_store(self, tmp_path):
        (tmp_path / "data" / "pygim" / "enact" / "global").mkdir(parents=True)
        (tmp_path / "data" / "pygim" / "memory" / "global").mkdir(parents=True)
        where = read({"XDG_DATA_HOME": str(tmp_path / "data")}, home=tmp_path)
        assert where.user_data == tmp_path / "data" / "pygim" / "enact"


def test_the_names_of_the_variables_live_in_one_place():
    """`enact.py` spelled `PYGIM_ENACT_SESSION` and `PYGIM_ENACT_RELOADED` again, beside the module that
    owns them — a rename would have had to find both."""
    source = (Path(_config.__file__).parent / "_mcp" / "enact.py").read_text(encoding="utf-8")
    assert '"PYGIM_ENACT_SESSION"' not in source and '"PYGIM_ENACT_RELOADED"' not in source
