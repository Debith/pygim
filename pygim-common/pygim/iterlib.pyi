from typing import Iterable, Any, Callable, Tuple, Generator

def split(
    iterable: Iterable[Any], condition: Callable[[Any], bool]
) -> Tuple[Iterable[Any], Iterable[Any]]: ...
def is_container(obj: Any) -> bool:
    """
    Determine whether an object is a container.

    A container is considered an object that contains other objects. This
    function returns `False` for strings, bytes, and types, even though they
    implement the iterator protocol.

    Parameters
    ----------
    obj : `object`
        The object to check.

    Returns
    -------
    `bool`
        `True` if `obj` is a container, `False` otherwise.

    Examples
    --------
    >>> from pygim.iterables import is_container
    >>> is_container("text")
    False

    >>> is_container(tuple())
    True
    """
    ...

def flatten(items: Iterable[Any]) -> Generator[Any, None, None]:
    """
    Flatten a nested iterable into a single list.

    This function flattens nested iterables such as lists, tuples, and sets
    into a single list. It can handle deeply nested and irregular structures.

    Parameters
    ----------
    iterable : `iterable`
        The nested iterable to flatten.

    Yields
    ------
    `object`
        The flattened objects from the nested iterable.

    Examples
    --------
    Flatten a list of lists:
    >>> from pygim.iterables import flatten
    >>> list(flatten([[1, 2], [3, 4]]))
    [1, 2, 3, 4]

    Flatten a deeply nested irregular list:
    >>> list(flatten([[[1, 2]], [[[3]]], 4, 5, [[6, [7, 8]]]]))
    [1, 2, 3, 4, 5, 6, 7, 8]

    Flatten a list of strings:
    >>> list(flatten(["one", "two", ["three", "four"]]))
    ['one', 'two', 'three', 'four']
    """