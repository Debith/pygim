# -*- coding: utf-8 -*-
"""
This module provides utilities for working with attributes.
"""

try:
    from _pygim.common_fast import is_container
except ImportError:
    from _pygim._iterlib import is_container
from .._magic._dispatcher import _Dispatcher


__all__ = ["safedelattr", "smart_getattr", "mgetattr"]

UNDEFINED = object()
EllipsisType = type(...)

def safedelattr(obj, name):
    try:
        delattr(obj, name)
    except AttributeError:
        pass  # It is already deleted and we are fine with it.


def smart_getattr(obj, name, *, autocall=True, default=UNDEFINED, args=(), kwargs=None):
    """
    Get the value of an attribute from an object, with optional autocall.

    Parameters
    ----------
    obj : object
        The object from which to retrieve the attribute.
    name : str
        The name of the attribute to retrieve.
    autocall : bool, optional
        If True, automatically call the attribute if it is callable, by default True.
    default : object, optional
        The default value to return if the attribute does not exist, by default UNDEFINED,
        which raises an AttributeError if the attribute does not exist.
    args : tuple, optional
        Positional arguments to pass to the attribute if it is callable, by default ().
    kwargs : dict, optional
        Keyword arguments to pass to the attribute if it is callable, by default None.

    Returns
    -------
    object
        The value of the attribute, or the default value if the attribute does not exist.

    Raises
    ------
    AttributeError
        If the attribute does not exist and no default value is provided.
    """
    kwargs = kwargs or {}
    if not hasattr(obj, name):
        if default is UNDEFINED:
            raise AttributeError(f"{obj!r} has no attribute {name!r}")
        return default
    value = getattr(obj, name)
    if autocall and callable(value):
        return value(*args, **kwargs)
    return value

DROPPED = object()

def mgetattr(iterable, name, factory=None, *, with_obj=False,
                                              autocall=True,
                                              default=UNDEFINED,
                                              args=(),
                                              kwargs=None,
                                              ):
    """
    Get attribute from the objects in the iterable.

    Parameters
    ----------
    iterable : iterable
        The iterable containing objects from which the attribute will be retrieved.
    name : str
        The name of the attribute to retrieve.
    factory : callable, optional
        A callable used to create the output container. If not provided, a list is used.
    with_obj : bool, optional
        If True, the output will contain tuples of (object, attribute_value). If False, only the attribute values are returned.
    autocall : bool, optional
        If True, callable attributes are automatically called. If False, they are returned as is.
    default : object, optional
        The default value to return if the attribute is not found. If ``Ellipsis`` or ``...`` is provided
        then the item is dropped, altering the length of the iterable. If not provided, an exception is raised.
    args : tuple, optional
        Positional arguments to pass to callable attributes.
    kwargs : dict, optional
        Keyword arguments to pass to callable attributes.

    Returns
    -------
    iterable or factory(iterable)
        The output container containing the attribute values.
    """
    kwargs = kwargs or {}
    factory = factory or list
    with_obj = with_obj or factory is dict
    default = DROPPED if isinstance(default, EllipsisType) else default

    def _do_iterable():
        for obj in iterable:
            result = smart_getattr(obj, name, autocall=autocall,
                                                default=default,
                                                args=args,
                                                kwargs=kwargs,
                                                )
            if result is DROPPED:
                continue

            if with_obj:
                yield obj, result
            else:
                yield result

    if isinstance(factory, EllipsisType):
        return _do_iterable()
    else:
        return factory(_do_iterable())


class MultiCall:
    def __init__(self, /, objs=None,
                          func_name=None,
                          factory=list,
                        *,
                          with_obj=False,
                          autocall=True,
                          default=UNDEFINED,
                          ):
        self.__objs = objs
        self.__func_name = func_name
        self.__factory = factory
        self.__with_obj = with_obj or factory is dict
        self.__autocall = autocall
        self.__default = default if default is not ... else DROPPED

    def __iter_attributes(self):
        for obj in self.__objs:
            value = getattr(obj, self.__func_name, self.__default)

            if value is UNDEFINED:
                raise AttributeError(f"{obj!r} has no attribute {self.__func_name!r}")
            elif value is DROPPED:
                continue

            yield obj, value

    def __iter_values(self, args, kwargs):
        for obj, value in self.__iter_attributes():
            if self.__autocall and callable(value):
                value = value(*args, **kwargs)
            if self.__with_obj:
                value = obj, value
            yield value

    @_Dispatcher
    def __call__(self, *args, **kwargs):
        if len(args) >= 2 and is_container(args[0]) and isinstance(args[1], str):
            self.__objs = args[0]
            self.__func_name = args[1]
            args = args[2:]
        if isinstance(self.__factory, EllipsisType):
            return self.__iter_values(args, kwargs)
        else:
            return self.__factory(self.__iter_values(args, kwargs))

    @__call__.register(is_container, str)
    def _(self, objects, func_name):
        return self(objects, func_name)


mgetattr = MultiCall(with_obj=False)