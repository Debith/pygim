# -*- coding: utf-8 -*-
"""
Dispatcher class internal implementation.
"""

from functools import wraps
from collections.abc import Callable
from itertools import product
from dataclasses import dataclass, field
from .._exceptions import NoArgumentsError
from .._static import auto
from .._exceptions import GimError
from .._utils._inspect import class_names
from .._error_msgs import type_error_msg


def _arg_identifier(arg):
    """
    Determine the type of the given argument.

    Parameters
    ----------
    arg : any
        Argument for which type needs to be identified.

    Returns
    -------
    function
        A function that can be used to identify a value's type.
    """
    if isinstance(arg, type):
        return type
    return lambda v: v


def _is_method(func):
    """
    Determine if the given function is a method within a class.

    This function checks whether the given function object is defined within
    a class (i.e., is a method) by examining its qualified name and argument
    count. The function's qualified name contains a dot if it's a method within
    a class, and its code object's argument count is greater than 0, ensuring
    that it accepts at least one argument (typically `self` for instance methods).

    Parameters
    ----------
    func : callable
        The function object to inspect.

    Returns
    -------
    bool
        True if the given function is a method within a class, False otherwise.
    """
    return "." in func.__qualname__ and func.__code__.co_argcount > 0


@dataclass
class _MethodWrapper:
    _function: callable
    _instance: object = None

    def __get__(self, __instance, __class):
        self._instance = __instance


@dataclass
class _FunctionWrapper:
    _function: callable

    def __get__(self):
        pass


# TODO: May not be needed
def _default_unregistered_function(*args, **kwargs):
    raise NotImplementedError

@dataclass
class _Dispatcher:
    __callable: object
    __registry: dict = field(default_factory=dict)
    __args: tuple = None
    __start_index: int = 0

    @classmethod
    def default_unregistered_function(cls, *, is_method=auto):
        """
        Returns a dispatch function with a default behavior for unregistered types.

        This method returns a dispatch function that raises a NotImplementedError
        when called with unregistered argument types. It's intended to be used
        when you want to ensure that only specific, registered types are handled
        by the dispatch function.

        Returns
        -------
        dispatch_function : callable
            A dispatch function that raises NotImplementedError for unregistered types.

        Examples
        --------
        >>> dispatch_by_type = dispatch.default_unregistered_function()
        >>> dispatch_by_type(some_unregistered_type)
        NotImplementedError: Argument types not supported: some_unregistered_type

        Notes
        -----
        The returned function raises an exception with a clear error message, providing
        the names of the unsupported argument types. This can be useful for debugging
        and ensures strict type handling.

        See Also
        --------
        register : Method to register a function with a specific type.
        """
        return cls(NotImplemented)

    @property
    def __is_default_generated(self):
        return self.__callable is _default_unregistered_function

    def __post_init__(self):
        """
        Post-initialization method that sets the starting index for method calls
        if the callable object appears to be a method.
        """
        assert callable(self.__callable), type_error_msg(self.__callable, Callable)
        wraps(self.__callable)(self)

    def __repr__(self):
        _template = "<{qualname} {kind} dispatcher at {address} for: {types}"
        _qualnames = [f.__qualname__ for f in self.__registry.values()]
        _args = dict(
            qualname=_qualnames[0],
            kind="method",
            address=f"{hex(id(self))}",
            types=", ".join(s[0].__name__ for s in self.__registry),
            )

        return _template.format_map(_args)

    @property
    def supported_types(self):
        return list(self.__registry)

    def register(self, *specs):
        """
        Register a function for specific argument types.

        Parameters
        ----------
        specs : `tuple` of any
            Specific argument types for which the function is registered.

        Returns
        -------
        function
            Decorator function that registers the given function for specific argument types.
        """
        if not self.__args:
            # Allow registering functions based on value and type.
            self.__args = tuple(_arg_identifier(a) for a in specs)

        # TODO: verify length
        def __inner_register(func):
            #assert self.__callable.__code__.co_argcount >= self.__start_index
            if self.__is_default_generated and _is_method(func):
                self.__start_index = 1
            self.__registry[specs] = func
            return self
        return __inner_register

    def __get__(self, __instance, __class):
        """
        Get method that sets the dispatcher instance and returns it.
        """
        self.__instance = __instance
        return self

    def __call__(self, *args, **kwargs):
        """
        Method that routes the call to the correct function
        based on argument types.

        Parameters
        ----------
        *args : positional arguments
            Arguments passed to the function.
        **kwargs : keyword arguments
            Keyword arguments passed to the function.

        Returns
        -------
        object
            Result of the function call.
        """
        if not args and not kwargs:
            raise NoArgumentsError("No arguments given!")

        # TODO: This code is ineffective and needs some extra magic to make it more performant.
        its_type = tuple(self.__args[i](args[i]) for i in range(len(self.__args)))
        if its_type not in self.__registry:
            prod_type = [t.__mro__[:-1] for t in its_type]
            prods = set(product(*prod_type))
            common = set(prods).intersection(self.__registry)

            if len(common) > 1:
                raise GimError(f"Multiple base class combinations: {class_names(common)}")

            # Missing common arguments means there is no handler for those types,
            # therefore, need to call the default function.
            if not common:
                return self.__callable(*args, **kwargs)

            func = self.__registry[list(common)[0]]

            for key in prods - common:
                if object in key:
                    continue
                self.__registry[key] = func

        if self.__start_index:
            args = (self.__instance,) + args
        try:
            return self.__registry[its_type](*args, **kwargs)
        except KeyError:
            return self.__callable(*args, **kwargs)


def dispatch(func=None):
    return _Dispatcher(func or _default_unregistered_function)