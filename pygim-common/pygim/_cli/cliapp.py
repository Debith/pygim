# -*- coding: utf-8 -*-
"""
Command-Line Interface Application for Python Gimmicks.
"""

import sys
from pathlib import Path
from dataclasses import dataclass
import click
import pygim.typing as t
from pygim.kernel import PathSet


__all__ = ['GimmicksCliApp']

def _echo(msg: t.Text, quiet: bool) -> None:
    if not quiet:
        click.echo(msg)


@dataclass
class GimmicksCliApp:
    def clean_up(self, yes: bool, build_dirs: bool, pycache_dirs: bool, compiled_files: bool, quiet: bool, all: bool):
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
            if response == 'n':
                sys.exit("No? Maybe next time...")
            elif response == 'y':
                new.FS.delete_all()
                _echo("Excellent! You never see them again!", quiet)
