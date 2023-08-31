# -*- coding: utf-8 -*-
"""
This module contains implementation of PathSet class.
"""

import shutil
from pathlib import Path
from dataclasses import dataclass

from pygim.iterlib import is_container
from _pygim._utils._fileutils import flatten_paths

__all__ = ["PathSet"]


class _FileSystemOps:
    """Functionality to manipulate the filesystem."""

    def __get__(self, __instance, _):
        self.__pathset = __instance
        return self

    def delete(self, path):
        if path.is_file():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path)

    def delete_all(self):
        """Delete Path object from the file system."""
        assert isinstance(self.__pathset, PathSet)
        for p in self.__pathset:
            self.delete(p)


@dataclass(frozen=True)
class PathSet:
    """
    A class for manipulating multiple Path objects at once.

    Methods
    -------
    prefixed(paths: iterable, *, prefix : str) -> PathSet
        Returns a new PathSet with `prefix` added to each path.
    clone() -> PathSet
        Returns a new PathSet that is a copy of this one.
    filter(filter : callable) -> Generator
        Yields paths from this set that pass a filter function.
    drop(filter : callable) -> Generator
        Yields paths from this set that do not pass a filter function.
    filtered(filter : callable) -> PathSet
        Returns a new PathSet of paths from this set that pass a filter function.
    dirs() -> List
        Returns a list of directories in this set.
    files() -> List
        Returns a list of files in this set.
    by_suffix(suffix : str) -> PathSet
        Returns a new PathSet of paths from this set with a given suffix.
    FS.delete_all() -> None
        Deletes all paths in this set from the file system.
    transform(container_type : type = list, path_type : type = str) -> container_type
        Returns a new container of `container_type` with elements of `path_type`.

    Examples
    --------
    >>> paths = PathSet([Path('path1'), Path('path2')])
    >>> len(paths)
    2
    >>> bool(paths)
    True
    >>> [p.stem for p in sorted(paths)]
    ['path1', 'path2']
    >>> repr(paths)
    "PathSet(['path1', 'path2'])"
    >>> paths.prefixed(["file1.txt", "file2.txt"], prefix="/root_folder")
    PathSet(['/root_folder/file1.txt', '/root_folder/file2.txt'])
    """

    # TODO: This class could allow multiple different path types (not just pathlib.Path).
    _paths: Path = None  # type: ignore    # this is invariant
    _pattern: str = "*"
    FS = _FileSystemOps()  # File system

    def __post_init__(self):
        paths = self._paths

        if paths is None:
            paths = Path.cwd()

        # We just handled the optional part, let's make mypy happy.
        assert paths is not None

        super().__setattr__("_paths", frozenset(flatten_paths(paths, pattern=self._pattern)))
        assert all([isinstance(p, Path) for p in self._paths])
        assert isinstance(self._paths, frozenset)

    @classmethod
    def prefixed(cls, paths, *, prefix=None):
        """
        Create a new PathSet object with a specified prefix for each path.

        Parameters
        ----------
        paths : `iterable` [path-like object]
            Iterable of path-like objects.
        prefix : path-like object, optional
            The prefix to add to each path in the input `paths`. Defaults to the current working directory.

        Returns
        -------
        PathSet
            New PathSet object with the specified prefix for each path.
        """
        assert is_container(paths), f"Paths must be a container, not {type(paths)}."

        if prefix is None:
            prefix = Path.cwd()
        prefix = Path(prefix)  # Ensure path-like object is Path.

        return cls([prefix.joinpath(p) for p in paths])

    @classmethod
    def from_parent(cls, filename):
        filename = Path(filename)
        return cls(filename.parent)

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
        return f"{self.__class__.__name__}({sorted(str(p) for p in self._paths)})"

    def __contains__(self, filename):
        filename = Path(filename)
        return bool(list(self.filter(name=filename.name)))

    def clone(self, paths=None):
        """
        Create a copy of the object.

        Parameters
        ----------
        paths : `iterable` [`pathlib.Path`], optional
            Override paths in the clone. Defaults to None.

        Returns
        -------
        PathSet
            New PathSet collection.
        """
        paths = self._paths if paths is None else paths
        instance = self.__class__([])
        super(self.__class__, instance).__setattr__("_paths", frozenset(Path(p) for p in paths))
        return instance

    def filter(self, **filters):
        """
        Filter paths based on their properties, where those matching filters are kept.

        Parameters
        ----------
        filters : `dict`
            Filters in this function have the following properties:

                - KEYs must always be valid attribute names for the underlying
                path objects. The KEY can be an attribute, property, or function.
                In the case of a function, the function is automatically invoked.
                However, functions requiring arguments are not supported.

                - VALUEs represent the expected results of the corresponding
                attributes or return values of the functions accessed by
                the KEY. VALUE can be a single value or an iterable of multiple
                different values. In the latter case, if any of the VALUEs is
                satisfied, the corresponding Path object qualifies.

        Yields
        ------
        `pathlib.Path`
            Qualifying paths.

        Examples
        --------
        >>> names = ["readme.txt", "readme.rst", "readme.md"]
        >>> paths = PathSet(names)                      # A set of paths
        >>> new_paths = paths.filter(suffix=".rst")     # Filter based on pathlib.Path.suffix property.
        >>> [p.name for p in new_paths]                 # Show the names in the filtered path set.
        ['readme.rst']

        >>> new_paths = paths.filter(suffix=[".rst", ".md"])    # This time we accept multiple suffixes.
        >>> [p.name for p in sorted(new_paths)]                 # Show the names in the filtered path set.
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
        """
        Filter paths based on their properties, where those NOT matching filters are kept.

        Parameters
        ----------
        filters : `dict`
            Filters in this function have the following properties:

                - KEYs must always be valid attribute names for the underlying
                path objects. The KEY can be an attribute, property, or function.
                In the case of a function, the function is automatically invoked.
                However, functions requiring arguments are not supported.

                - VALUEs represent the expected results of the corresponding
                attributes or return values of the functions accessed by
                the KEY. VALUE can be a single value or an iterable of multiple
                different values. In the latter case, if any of the VALUEs is
                satisfied, the corresponding Path object qualifies.

        Yields
        ------
        `pathlib.Path`
            Non-qualifying paths.

        Examples
        --------
        >>> names = ["readme.txt", "readme.rst", "readme.md"]
        >>> paths = PathSet(names)                      # A set of paths
        >>> new_paths = paths.drop(suffix=".rst")       # Filter based on pathlib.Path.suffix property.
        >>> [p.name for p in sorted(new_paths)]         # Show the names in the filtered path set.
        ['readme.md', 'readme.txt']

        >>> new_paths = paths.drop(suffix=[".rst", ".md"])      # This time we accept multiple suffixes.
        >>> [p.name for p in new_paths]                         # Show the names in the filtered path set.
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
        """As filter() but returns new object."""
        return self.clone(self.filter(**filters)) if filters else self

    def dropped(self, **filters):
        """As drop() but returns new object."""
        return self.clone(self.drop(**filters)) if filters else self

    def dirs(self, **filters):
        """A common filter to return only dirs. See filter() for more details."""
        return self.filtered(is_dir=True).filtered(**filters)

    def files(self, **filters):
        """A common filter to return only files. See filter() for more details."""
        return self.filtered(is_file=True).filtered(**filters)

    def by_suffix(self, *suffix):
        """A common filter to return files and folders by suffix."""
        return self.filtered(suffix=suffix)

    def __add__(self, other):
        """Combine paths together."""
        assert isinstance(other, self.__class__)
        return self.clone(set(self._paths) | set(other._paths))

    def transform(self, container_type=list, path_type=str):
        """
        Transform the container and elements of the instance to specified types.

        This function transforms the elements of the instance using the
        `path_type` argument, and then packs them into a new container
        specified by the `container_type` argument.

        Parameters
        ----------
        container_type : type, optional
            The type of the output container (default is `list`). This should
            be a type (like `list` or `set`), not an instance of a type (like `[]` or `{}`).
        path_type : type, optional
            The type to convert each path in the instance (default is `str`).
            This should be a callable that takes a path as input and returns
            a new path of the desired type.

        Returns
        -------
        container_type
            The container filled with `path_type` objects.

        Examples
        --------
        Given a class `PathSet` that holds a list of `Path` objects:

        >>> paths = PathSet([Path('path1'), Path('path2')])
        >>> transformed = paths.transform(container_type=set, path_type=str)
        >>> print(sorted(transformed))
        ['path1', 'path2']
        """
        return container_type(path_type(p) for p in self)


if __name__ == "__main__":
    import doctest

    doctest.testmod()
