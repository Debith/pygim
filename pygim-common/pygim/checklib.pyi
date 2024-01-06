from typing import Iterable, Any, Union, Tuple, TypeAlias, Callable

def has_instances(
    iterable: Iterable[Any],
    types: Union[TypeAlias, Tuple[TypeAlias]],
    how: Callable[[Iterable[object]], bool] = ...,
) -> bool:
    """
    Check if all or any items in an iterable are instances of a specified type.

    Parameters
    ----------
    iterable : iterable
        The iterable to check.
    types : type or tuple of types
        The expected type(s) of the items.
    how : callable, optional
        A callable that will be used to aggregate the results of the checks
        (e.g. `all` to check if all items are instances of the specified type(s),
        `any` to check if any items are instances of the specified type(s)).
        Defaults to `all`.

    Returns
    -------
    bool
        True if all/any items in the iterable are instances of the specified type(s),
        False otherwise.

    Examples
    --------
    >>> has_instances([1,2,3], int)
    True
    >>> has_instances([1,2,'3'], int)
    False
    >>> has_instances([1,2,'3'], int, how=any)
    True
    """
    ...

def is_subset(maybe_subset: Iterable[Any], fullset: Iterable[Any]) -> bool:
    """
    Check if an iterable is a subset of another iterable.

    Parameters
    ----------
    iterable : iterable
        The iterable to check.
    other : iterable
        The iterable to check against.

    Returns
    -------
    bool
        True if `iterable` is a subset of `other`, False otherwise.

    Examples
    --------
    >>> is_subset([1, 2], [1, 2, 3])
    True
    >>> is_subset([1, 2, 3], [1, 2])
    False
    """
    ...
