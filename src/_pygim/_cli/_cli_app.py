# -*- coding: utf-8 -*-
"""
Command-Line Interface Application for Python Gimmicks.
"""

from subprocess import Popen, DEVNULL
import sys
import shutil
import functools
from pathlib import Path
from dataclasses import dataclass
from importlib import import_module
import click

__all__ = ["GimmicksCliApp", "flag_opt"]


def _echo(msg, quiet):
    if not quiet:
        click.echo(msg)


flag_opt = functools.partial(click.option, is_flag=True, default=False)


@dataclass
class GimmicksCliApp:
    def clean_up(self, yes, build_dirs, pycache_dirs, compiled_files, quiet, all):
        root = Path.cwd()
        _echo(f"Starting clean up in `{root}`", quiet)
        targets = []
        pycache_dirs = pycache_dirs or not build_dirs and not compiled_files

        if all or build_dirs:
            targets.extend(p for p in root.rglob("build") if p.is_dir())

        if all or pycache_dirs:
            targets.extend(p for p in root.rglob("__pycache__") if p.is_dir())

        if all or compiled_files:
            targets.extend(p for p in root.rglob("*.c") if p.is_file())
            targets.extend(p for p in root.rglob("*.so") if p.is_file())

        if targets and not yes:
            print("\n".join([str(t) for t in targets]))
            response = input(f"Remove all {len(targets)} files/folders (Y/N)? ")
            if response.lower() == "n":
                sys.exit("No? Maybe next time...")
            elif response.lower() != "y":
                return

        if yes or targets:
            for t in targets:
                if t.is_dir():
                    shutil.rmtree(t)
                elif t.is_file():
                    t.unlink()
            _echo(f"Removed {len(targets)} items.", quiet)

    def show_test_coverage(self):
        # TODO: Make this nicer
        Popen(
            "python -m coverage run -m pytest".split(" "),
            stdout=DEVNULL,
            stderr=DEVNULL,
        ).wait()
        Popen("python -m coverage report -m".split(" ")).wait()

    def ai(self, text):
        print("AI is not implemented yet!")

    def show_support(self):
        rows = []
        try:
            _ = import_module("pygim._persistence")
            rows.append(("persistence extension", True))
            rows.append(("odbc", True))
            rows.append(("arrow (c++)", True))
        except ImportError:
            rows.append(("persistence extension", False))
        click.echo("Feature support:")
        for name, supported in rows:
            status = "supported" if supported else "missing"
            click.echo(f"- {name}: {status}")
