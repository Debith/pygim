from dataclasses import dataclass, field


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
    """
    A dispatcher that routes calls to different functions depending on the type of arguments.

    Parameters
    ----------
    callable : `object`
        A callable object for which a dispatcher is needed.
    registry : `dict`, optional
        A dictionary of functions mapped to specific argument types.
    args : `tuple`, optional
        A tuple of functions (argument type identifiers) for the dispatcher.
    start_index : `int`, optional
        An integer that defines the starting index of the method call.

    Methods
    -------
    register(*specs)
        Register a function with specific argument types.
    """

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




dispatch = _Dispatcher