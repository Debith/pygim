# -*- coding: utf-8 -*-
"""
Command-Line Interface Application for Python Gimmicks.
"""

import sys
from pathlib import Path
from dataclasses import dataclass
import shutil
import click
import pygim.typing as t
from pygim.utils import is_container

__all__ = ['GimmicksCliApp']

def _echo(msg, quiet):
    if not quiet:
        click.echo(msg)


@dataclass(frozen=True)
class PathList:
    _paths: t.Optional[t.FrozenSet[Path]] = None
    _pattern: str = "*"

    def __post_init__(self):
        if self._paths is None:
            super().__setattr__("_paths", Path.cwd())

        if isinstance(self._paths, Path):
            super().__setattr__("_paths", frozenset([fname for fname in self._paths.rglob(self._pattern)]))

        if not isinstance(self._paths, frozenset):
            super().__setattr__("_paths", frozenset(self._paths))

        assert all(isinstance(p, Path) for p in self._paths)

    def __len__(self):
        return len(self._paths)

    def __iter__(self):
        yield from self._paths

    def __bool__(self):
        return bool(self._paths)

    def __repr__(self):
        return f"{self.__class__.__name__}({list(self._paths)})"

    def clone(self, paths):
        return self.__class__(frozenset(paths))

    def filter(self, **filters):
        for p in self._paths:
            for func, value in filters.items():
                value = value if is_container(value) else [value]
                obj = getattr(p, func)
                obj = obj() if callable(obj) else obj

                if obj in value:
                    yield p
                    break

    def drop(self, **filters):
        for p in self._paths:
            for func, value in filters.items():
                value = value if is_container(value) else [value]
                obj = getattr(p, func)
                obj = obj() if callable(obj) else obj

                if obj not in value:
                    yield p
                    break

    def filtered(self, **filters):
        return self.clone(self.filter(**filters)) if filters else self

    def dirs(self, **filters):
        return self.filtered(is_dir=True).filtered(**filters)

    def files(self, **filters):
        return self.filtered(is_file=True).filtered(**filters)

    def by_suffix(self, *suffix):
        return self.filtered(suffix=suffix)

    def __add__(self, other):
        assert isinstance(other, self.__class__)
        return self.clone(list(self._paths) + list(other._paths))  # type: ignore

    def delete_all(self):
        for p in self:
            if p.is_file():
                p.unlink()
            elif p.is_dir():
                shutil.rmtree(p)


@dataclass
class GimmicksCliApp:
    def clean_up(self, yes: bool, build_dirs: bool, pycache_dirs: bool, compiled_files: bool, quiet: bool, all: bool):
        # TODO: clean up!
        _echo(f"Starting clean up in `{Path.cwd()}`", quiet)
        pth = PathList()
        new = PathList([])
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
                new.delete_all()
                _echo("Excellen! You never see them again!", quiet)
