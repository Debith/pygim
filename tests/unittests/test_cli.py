# -*- coding: utf-8 -*-
"""Tests for the CLI application."""

from pathlib import Path
import pytest
from _pygim._cli._cli_app import GimmicksCliApp


class TestCleanUp:
    """Tests for GimmicksCliApp.clean_up — verifies PathSet import works."""

    def test_clean_up_no_crash(self, temp_dir, monkeypatch):
        """clean_up() should not raise NameError for PathSet."""
        monkeypatch.chdir(temp_dir)
        app = GimmicksCliApp()
        # --yes skips interactive prompt, --quiet suppresses output
        app.clean_up(yes=True, build_dirs=False, pycache_dirs=True,
                     compiled_files=False, quiet=True, all=False)

    def test_clean_up_finds_pycache(self, temp_dir, monkeypatch):
        """clean_up with pycache_dirs=True should delete __pycache__ dirs."""
        monkeypatch.chdir(temp_dir)
        pycache = temp_dir / "__pycache__"
        pycache.mkdir()
        (pycache / "foo.pyc").touch()

        app = GimmicksCliApp()
        app.clean_up(yes=True, build_dirs=False, pycache_dirs=True,
                     compiled_files=False, quiet=True, all=False)
        assert not pycache.exists(), "__pycache__ should have been removed"

    def test_clean_up_all_flag(self, temp_dir, monkeypatch):
        """clean_up with --all should include build, pycache, and compiled."""
        monkeypatch.chdir(temp_dir)
        build = temp_dir / "build"
        build.mkdir()
        pycache = temp_dir / "__pycache__"
        pycache.mkdir()

        app = GimmicksCliApp()
        app.clean_up(yes=True, build_dirs=False, pycache_dirs=False,
                     compiled_files=False, quiet=True, all=True)
        assert not build.exists(), "build/ should have been removed"
        assert not pycache.exists(), "__pycache__ should have been removed"


class TestShowSupport:
    """Tests for show_support — verifies it runs without error."""

    def test_show_support_runs(self):
        app = GimmicksCliApp()
        app.show_support()
