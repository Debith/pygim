# -*- coding: utf-8 -*-
"""
This module contains implementation of PathSet class.
"""

import shutil
from pathlib import Path
from dataclasses import dataclass

from pygim.utils import is_container
from .magic.each import Each
import pygim.typing as t


@dataclass(frozen=True)
class PathSet:
    """ This class encapsulates manipulation of multiple path objects at once.

    Overview (further info in function docs):
        - len(PathSet()) provides total number of files and directories read recursively.
        - list(PathSet()) provides list of all Path objects in the list.
        - bool(PathSet()) tells whether there is any Path objects in the list.
        - repr(PathSet()) provides nice string representation of this object.
        - PathSet() + PathSet() creates new object contains Path objects from both sets.
        - PathSet.prefixed() create new PathSet with another path as prefix (e.g. folder+files).
        - PathSet().clone() creates identical copy of the list.
        - PathSet().filter() generator that yields Path objects whose properties match the filters.
        - PathSet().drop() generator that yields Path objects whose properties do NOT match the filters.
        - PathSet().filtered() as above but returns a new PathSet object.
        - PathSet().dirs() a shorthand for list of dirs.
        - PathSet().files() a shorthand for list of files.
        - PathSet().by_suffix() a shorthand for filtering by suffix(es).
        - PathSet().delete_all() deletes all contained Path objects from the file-system.

    TODO: This class could allow multiple different path types (not just pathlib.Path).
    """
    _paths: t.Optional[t.Iterable[Path]] = None
    _pattern: str = "*"

    each = Each(Path)

    def __post_init__(self) -> None:
        paths: t.Optional[t.Iterable[Path]] = self._paths

        if paths is None:
            super().__setattr__("_paths", Path.cwd())

        # We just handled the optional part, let's make mypy happy.
        assert paths is not None

        if isinstance(paths, Path):
            super().__setattr__(
                "_paths",
                frozenset([fname for fname in paths.rglob(self._pattern)]),
                )

        if not isinstance(paths, frozenset):
            super().__setattr__("_paths", frozenset(paths))

        assert all(isinstance(p, Path) for p in paths)

    @classmethod
    def prefixed(cls,  # type:ignore
            paths: t.Iterable[t.PathLike],
            *,
            prefix: t.Optional[t.PathLike] = None,
        ):
        if prefix is None:
            prefix = Path.cwd()
        prefix = Path(prefix)  # Ensure pathlike object is Path.

        return cls([prefix.joinpath(p) for p in paths])

    def __len__(self) -> int:
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
        return self.clone(set(self._paths) | set(other._paths))  # type: ignore

    def delete_all(self):
        for p in self:
            if p.is_file():
                p.unlink()
            elif p.is_dir():
                shutil.rmtree(p)


if __name__ == '__main__':
    import doctest
    doctest.testmod()