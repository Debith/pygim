# -*- coding: utf-8 -*-
"""What the program reads from outside itself, read once where it is wired (`_pygim._config`).

`read` takes the machine as arguments — the variables, the working directory, the home directory —
so every case here is plain values, with no machine arranged around it. That was the module's
promise, and until 2026-09-23 it reached past its own arguments to the real machine.
"""
from __future__ import annotations

from pathlib import Path

from _pygim import _config

HOME, CWD = Path("/pretend/home"), Path("/pretend/cwd")


def read(variables, **kw):
    return _config.read(variables, cwd=kw.pop("cwd", CWD), home=kw.pop("home", HOME), **kw)


class TestPathsAreReadAgainstWhatWasGiven:
    """`expanduser()` asks the process for $HOME and `resolve()` asks it for the working directory,
    whatever `home` and `cwd` the caller handed in — so a test that named its own world got the
    developer's, and so did any caller that moved `cwd`."""

    def test_a_tilde_means_the_home_that_was_given(self):
        assert read({}).path("~/stores/global") == HOME / "stores" / "global"

    def test_a_relative_path_means_the_directory_that_was_given(self):
        assert read({}).path("stores/global") == CWD / "stores" / "global"

    def test_another_directory_reads_relative_paths_under_it(self):
        assert read({}).at(Path("/elsewhere")).path("x") == Path("/elsewhere/x")


class TestTheSwitches:
    def test_no_color_counts_only_when_it_is_not_empty(self):
        """no-color.org: "when present and not an empty string (regardless of its value)"."""
        assert read({"NO_COLOR": "1"}).colour is False
        assert read({"NO_COLOR": ""}).colour is True
        assert read({"PYGIM_NO_COLOR": ""}).colour is True
        assert read({"TERM": "dumb"}).colour is False

    def test_the_command_line_wins(self):
        assert read({"NO_COLOR": "1"}, colour=True).colour is True
