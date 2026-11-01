# -*- coding: utf-8 -*-
"""Tests for the CLI application."""

import subprocess
import sys

import pytest

from _pygim._cli import _cli_app
from _pygim._cli._cli_app import GimmicksCliApp


class TestCleanUp:
    """Tests for GimmicksCliApp.clean_up — verifies PathSet import works."""

    def test_clean_up_no_crash(self, temp_dir, monkeypatch):
        """clean_up() should not raise NameError for PathSet."""
        monkeypatch.chdir(temp_dir)
        app = GimmicksCliApp()
        # --yes skips interactive prompt, --quiet suppresses output
        app.clean_up(
            yes=True,
            build_dirs=False,
            pycache_dirs=True,
            compiled_files=False,
            quiet=True,
            all=False,
        )

    def test_clean_up_finds_pycache(self, temp_dir, monkeypatch):
        """clean_up with pycache_dirs=True should delete __pycache__ dirs."""
        monkeypatch.chdir(temp_dir)
        pycache = temp_dir / "__pycache__"
        pycache.mkdir()
        (pycache / "foo.pyc").touch()

        app = GimmicksCliApp()
        app.clean_up(
            yes=True,
            build_dirs=False,
            pycache_dirs=True,
            compiled_files=False,
            quiet=True,
            all=False,
        )
        assert not pycache.exists(), "__pycache__ should have been removed"

    def test_clean_up_all_flag(self, temp_dir, monkeypatch):
        """clean_up with --all should include build, pycache, and compiled."""
        monkeypatch.chdir(temp_dir)
        build = temp_dir / "build"
        build.mkdir()
        pycache = temp_dir / "__pycache__"
        pycache.mkdir()

        app = GimmicksCliApp()
        app.clean_up(
            yes=True,
            build_dirs=False,
            pycache_dirs=False,
            compiled_files=False,
            quiet=True,
            all=True,
        )
        assert not build.exists(), "build/ should have been removed"
        assert not pycache.exists(), "__pycache__ should have been removed"


    def test_clean_up_never_walks_into_history_or_an_environment(self, temp_dir, monkeypatch):
        """D92 (Debith, 2026-11-01): .git, .venv, venv and node_modules hold what is installed, not
        what a build left: a virtual environment's compiled modules were offered for deletion."""
        monkeypatch.chdir(temp_dir)
        kept = [temp_dir / ".venv" / "lib" / "ext.so", temp_dir / "venv" / "build" / "x.py",
                temp_dir / ".git" / "objects" / "__pycache__" / "y.pyc", temp_dir / "node_modules" / "m" / "z.c"]
        removed = [temp_dir / "src" / "ext.so", temp_dir / "build" / "x.py", temp_dir / "src" / "__pycache__" / "y.pyc"]
        for f in kept + removed:
            f.parent.mkdir(parents=True, exist_ok=True)
            f.touch()
        GimmicksCliApp().clean_up(yes=True, build_dirs=False, pycache_dirs=False, compiled_files=False, quiet=True, all=True)
        assert [f.relative_to(temp_dir).as_posix() for f in kept if not f.exists()] == []
        assert [f.relative_to(temp_dir).as_posix() for f in removed if f.exists()] == []


class TestShowTestCoverage:
    """GimmicksCliApp.show_test_coverage: the tests here under coverage, then the report. Found
    2026-10-31: it ran the first python on PATH, hid the tests' errors with their output, and
    succeeded whatever either run said."""

    @pytest.fixture
    def runs(self, monkeypatch):
        calls, codes = [], []

        def run(command, **kwargs):
            calls.append((command, kwargs.get("stdout")))
            return subprocess.CompletedProcess(command, codes.pop(0) if codes else 0)

        monkeypatch.setattr(_cli_app.subprocess, "run", run)
        return calls, codes

    def test_it_runs_the_tests_then_the_report_by_this_interpreter(self, runs):
        calls, _ = runs
        GimmicksCliApp().show_test_coverage()
        assert calls == [
            ([sys.executable, "-m", "coverage", "run", "-m", "pytest"], subprocess.DEVNULL),   # errors still shown
            ([sys.executable, "-m", "coverage", "report", "-m"], None),
        ]

    def test_failing_tests_fail_the_command_after_the_report(self, runs):
        calls, codes = runs
        codes.extend([1, 0])
        with pytest.raises(SystemExit) as exited:
            GimmicksCliApp().show_test_coverage()
        assert exited.value.code == 1
        assert len(calls) == 2

    def test_a_report_that_fails_fails_the_command(self, runs):
        _, codes = runs
        codes.extend([0, 2])
        with pytest.raises(SystemExit) as exited:
            GimmicksCliApp().show_test_coverage()
        assert exited.value.code == 2


class TestShowSupport:
    """Tests for show_support — verifies it runs without error."""

    def test_show_support_runs(self):
        app = GimmicksCliApp()
        app.show_support()
