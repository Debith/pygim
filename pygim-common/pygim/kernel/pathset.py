# -*- coding: utf-8 -*-
"""
This module contains implementation of PathSet class.
"""

import shutil
from pathlib import Path
from dataclasses import dataclass
from typing import Iterable

from pygim.utils import is_container, flatten
import pygim.typing as t



def _flatten_paths(paths):
    for path in flatten(paths):
        path = Path(path)

        if path.is_dir():
            yield path
            for p in _flatten_paths(path.glob("*")):
                yield p
        else:
            yield path


class _FileSystemOps:
    """ Functionality to manipulate the filesystem. """
    def __get__(self, __instance, _):
        self.__pathset = __instance
        return self

    def delete(self, path):
        if path.is_file():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path)

    def delete_all(self):
        """ Delete Path object from the file system. """
        assert isinstance(self.__pathset, PathSet)
        for p in self.__pathset:
            self.delete(p)


@dataclass(frozen=True)
class PathSet:
    """ This class encapsulates manipulation of multiple path objects at once.

    Overview (further info in function docs):
        - len(PathSet()) provides total number of files and directories read recursively.
        - list(PathSet()) provides list of all Path objects in the list.
        - bool(PathSet()) tells whether there is any Path objects in the list.
        - repr(PathSet()) provides nice string representation of this object.
        - PathSet.prefixed()
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
    _paths: Path = None  # type: ignore    # this is invariant
    _pattern: str = "*"
    FS = _FileSystemOps()          # File system

    def __post_init__(self):
        paths = self._paths

        if paths is None:
            paths = Path.cwd()

        # We just handled the optional part, let's make mypy happy.
        assert paths is not None

        super().__setattr__("_paths", frozenset(_flatten_paths([paths])))
        assert all([isinstance(p, Path) for p in self._paths])
        assert isinstance(self._paths, frozenset)

    @classmethod
    def prefixed(cls, paths, *, prefix=None):
        if prefix is None:
            prefix = Path.cwd()
        prefix = Path(prefix)  # Ensure pathlike object is Path.

        return cls([prefix.joinpath(p) for p in paths])

    def __len__(self):
        assert self._paths is not None
        return len(self._paths)

    def __iter__(self):
        assert self._paths is not None
        yield from self._paths

    def __bool__(self):
        assert self._paths is not None
        return bool(self._paths)

    def __repr__(self):  # pragma: no cover
        assert self._paths is not None
        return f"{self.__class__.__name__}({list(str(p) for p in self._paths)})"

    def clone(self, paths=None):
        """Create copy of the object.

        Args:
            paths (t.MaybePathLikes, optional):
                Override paths in the clone. Defaults to None.

        Returns:
            PathSet: New Pathset collection.
        """
        paths = self._paths if paths is None else paths
        instance = self.__class__([])  # type: ignore # Types are managed in constructor.
        super(self.__class__, instance).__setattr__("_paths", frozenset(Path(p) for p in paths))
        return instance

    def filter(self, **filters):
        """ Filter paths based on their properties, where those matching filters are kept.

        Args:
            filters (PathFilters):
                Filters in this function has following functions:

                    - KEYs must always be valid attribute names for underlying
                      path objects. The KEY can be attribute, property or function.
                      In case of function, the function is automatically invoked.
                      However, functions requiring arguments are not supported.

                    - VALUEs represents the expected results of corresponding
                      attributes or return values of the functions accessed by
                      the KEY. VALUE can be a single value, or iterable of multiple
                      different values. For latter case, if any of the VALUEs is
                      satisfied, the corresponding Path object qualifies.

        Yields:
            Iterator[PathGenerator]: Qualifying paths.

        Examples:
            >>> names = ["readme.txt", "readme.rst", "readme.md"]
            >>> paths = PathSet(names)                      # A set of paths
            >>> new_paths = paths.filter(suffix=".rst")     # Filter based on pathlib.Path.suffix property.
            >>> [p.name for p in new_paths]                 # Show the names in filtered path set.
            ['readme.rst']

            >>> new_paths = paths.filter(suffix=[".rst", ".md"])    # This time we accept multiple suffixes.
            >>> [p.name for p in sorted(new_paths)]                 # Show the names in filtered path set.
            ['readme.md', 'readme.rst']
        """
        assert filters, "No filters given!"
        assert self._paths is not None

        for p in self._paths:
            for func, value in filters.items():
                value = value if is_container(value) else [value]
                obj = getattr(p, func)
                obj = obj() if callable(obj) else obj

                if obj in value:
                    yield p
                    break

    def drop(self, **filters):
        """ Filter paths based on their properties, where those NOT matching filters are kept.

        Args:
            filters (PathFilters):
                Filters in this function has following functions:

                    - KEYs must always be valid attribute names for underlying
                      path objects. The KEY can be attribute, property or function.
                      In case of function, the function is automatically invoked.
                      However, functions requiring arguments are not supported.

                    - VALUEs represents the expected results of corresponding
                      attributes or return values of the functions accessed by
                      the KEY. VALUE can be a single value, or iterable of multiple
                      different values. For latter case, if any of the VALUEs is
                      satisfied, the corresponding Path object qualifies.

        Yields:
            Iterator[PathGenerator]: Not-Qualifying paths.

        Examples:
            >>> names = ["readme.txt", "readme.rst", "readme.md"]
            >>> paths = PathSet(names)                      # A set of paths
            >>> new_paths = paths.drop(suffix=".rst")       # Filter based on pathlib.Path.suffix property.
            >>> [p.name for p in sorted(new_paths)]         # Show the names in filtered path set.
            ['readme.md', 'readme.txt']

            >>> new_paths = paths.drop(suffix=[".rst", ".md"])      # This time we accept multiple suffixes.
            >>> [p.name for p in new_paths]                         # Show the names in filtered path set.
            ['readme.txt']
        """
        assert filters, "No filters given!"
        assert self._paths is not None

        for p in self._paths:
            for func, value in filters.items():
                value = value if is_container(value) else [value]
                obj = getattr(p, func)
                obj = obj() if callable(obj) else obj

                if obj not in value:
                    yield p
                    break

    def filtered(self, **filters):
        """ As filter() but returns new object. """
        return self.clone(self.filter(**filters)) if filters else self

    def dropped(self, **filters):
        """ As drop() but returns new object. """
        return self.clone(self.drop(**filters)) if filters else self

    def dirs(self, **filters):
        """ A common filter to return only dirs. See filter() for more details. """
        return self.filtered(is_dir=True).filtered(**filters)

    def files(self, **filters):
        """ A common filter to return only files. See filter() for more details. """
        return self.filtered(is_file=True).filtered(**filters)

    def by_suffix(self, *suffix):
        """ A common filter to return files and folders by suffix. """
        return self.filtered(suffix=suffix)

    def __add__(self, other):
        """ Combine paths together. """
        assert isinstance(other, self.__class__)
        return self.clone(set(self._paths) | set(other._paths))


if __name__ == '__main__':
    import doctest
    doctest.testmod()