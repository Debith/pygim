# -*- coding: utf-8 -*-
"""
Dispatcher class internal implementation.
"""

from collections.abc import Callable
from dataclasses import dataclass, field
from .._utils._inspect import type_error_msg


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


@dataclass
class _Dispatcher:
    __callable: object
    __registry: dict = field(default_factory=dict)
    __args: tuple = None
    __start_index: int = 0

    @staticmethod
    def __no_default(*__a, **__kw):
        raise NotImplementedError(
            f"Argument types not supported: {','.join(type(a).__name__ for a in __a)}")

    @classmethod
    def no_default(cls):
        return cls(NotImplemented)

    def __post_init__(self):
        """
        Post-initialization method that sets the starting index for method calls
        if the callable object appears to be a method.
        """
        if self.__callable is NotImplemented:
            self.__callable = self.__no_default

        assert callable(self.__callable), type_error_msg(self.__callable, Callable)
        if "." in self.__callable.__qualname__ and self.__callable.__code__.co_argcount > 0:
            # This looks like a method.
            self.__start_index = 1

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
            assert self.__callable.__code__.co_argcount >= self.__start_index
            self.__registry[specs] = func
            return func
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
        # TODO: This code is ineffective and needs some extra magic to make it more performant.
        its_type = tuple(self.__args[i](args[i]) for i in range(len(self.__args)))
        if self.__start_index:
            args = (self.__instance,) + args
        try:
            return self.__registry[its_type](*args, **kwargs)
        except KeyError:
            return self.__callable(*args, **kwargs)