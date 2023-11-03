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
    ...


def tuplify(obj: Any) -> Tuple[Any, ...]:
    """
    Tuplify an Object

    Convert the given object into a tuple representation. If the object is a dictionary,
    it is converted into a tuple of key-value pairs. If the object is an iterable container,
    it is converted into a tuple. Otherwise, the object is returned as a single-element tuple.

    Parameters
    ----------
    obj : `object`
        The object to be tuplified.

    Returns
    -------
    tuple
        A tuple representation of the object.

    Notes
    -----
    - If the object is a dictionary, the resulting tuple will contain key-value pairs,
      where each pair is represented as a tuple (key, value).
    - If the object is an iterable container, such as a list or set, the resulting tuple
      will contain the elements of the container.
    - If the object is not a dictionary or an iterable container, it will be returned
      as a single-element tuple.

    Examples
    --------
    Example usage of `tuplify` with a dictionary:
    ```python
    d = {"a": 1, "b": 2, "c": 3}
    result = tuplify(d)  # (("a", 1), ("b", 2), ("c", 3))
    ```

    Example usage of `tuplify` with an iterable container:
    ```python
    lst = [1, 2, 3, 4]
    result = tuplify(lst)  # (1, 2, 3, 4)
    ```

    Example usage of `tuplify` with a non-dictionary and non-iterable object:
    ```python
    value = 10
    result = tuplify(value)  # (10,)
    ```
    """
    ...