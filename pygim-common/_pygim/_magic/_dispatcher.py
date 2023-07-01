# -*- coding: utf-8 -*-
"""
Dispatcher class internal implementation.
"""

from itertools import product
from functools import wraps
from dataclasses import dataclass, field
from .._exceptions import GimError
from .._utils._inspect import class_names


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

    def __post_init__(self):
        """
        Post-initialization method that sets the starting index for method calls
        if the callable object appears to be a method.
        """
        if "." in self.__callable.__qualname__ and self.__callable.__code__.co_argcount > 0:
            # This looks like a method.
            self.__start_index = 1
        wraps(self.__callable)(self)

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
        if its_type not in self.__registry:
            prod_type = [t.__mro__[:-1] for t in its_type]
            prods = set(product(*prod_type))
            common = set(prods).intersection(self.__registry)

            if len(common) > 1:
                raise GimError(f"Multiple base class combinations: {class_names(common)}")

            if not common:
                return self.__callable(*args, **kwargs)

            func = self.__registry[list(common)[0]]

            for key in prods - common:
                if object in key:
                    continue
                self.__registry[key] = func

        if self.__start_index:
            args = (self.__instance,) + args

        return self.__registry[its_type](*args, **kwargs)
