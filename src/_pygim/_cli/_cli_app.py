# -*- coding: utf-8 -*-
"""
Command-Line Interface Application for Python Gimmicks.
"""

from subprocess import Popen, DEVNULL
import sys
import functools
from pathlib import Path
from dataclasses import dataclass
import click

__all__ = ["GimmicksCliApp", "flag_opt"]


def _echo(msg, quiet):
    if not quiet:
        click.echo(msg)


flag_opt = functools.partial(click.option, is_flag=True, default=False)


@dataclass
class GimmicksCliApp:
    def clean_up(self, yes, build_dirs, pycache_dirs, compiled_files, quiet, all):
        # TODO: clean up!
        _echo(f"Starting clean up in `{Path.cwd()}`", quiet)
        pth = PathSet()
        new = PathSet([])
        pycache_dirs = pycache_dirs or not build_dirs and not compiled_files

        if all or build_dirs:
            new += pth.dirs(name="build")

        if all or pycache_dirs:
            new += pth.dirs(name="__pycache__")

        if all or compiled_files:
            new += pth.files(suffix=(".c", ".so"))

        if new and not yes:
            print("\n".join([str(n) for n in new]))
            response = input(f"Remove all {len(new)} files/folders (Y/N)? ")
            if response == "n":
                sys.exit("No? Maybe next time...")
            elif response == "y":
                new.FS.delete_all()
                _echo("Excellent! You never see them again!", quiet)

    def show_test_coverage(self):
        # TODO: Make this nicer
        Popen("python -m coverage run -m pytest".split(' '), stdout=DEVNULL, stderr=DEVNULL).wait()
        Popen("python -m coverage report -m".split(' ')).wait()

    def ai(self, text):
        print("AI is not implemented yet!")