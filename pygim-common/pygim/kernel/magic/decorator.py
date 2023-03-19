import abc
from typing import Any, Callable, TypeVar

T = TypeVar('T', bound=Callable[..., Any])


class DecoratorMeta(abc.ABCMeta):
    def __call__(self, *args: Any, **kwargs: Any) -> Any:
        if hasattr(self, "_decorator_"):
            return super().__call__(*args, **kwargs)

        if not args:
            _class_name = f"_DecCls{self.__name__}"
            _bases = (self, _NoFuncDecorator)
            _namespace = self.__dict__.copy()
            _namespace["_decorator_"] = True
            _DecCls = type(_class_name, _bases, _namespace)
            return _DecCls(*args, **kwargs)

        if not callable(args[0]):
            _class_name = f"_DecCls{self.__name__}"
            _bases = (self, _NoFuncDecorator)
            _namespace = self.__dict__.copy()
            _namespace["_decorator_"] = True
            _DecCls = type(_class_name, _bases, _namespace)
            return _DecCls(*args, **kwargs)

        elif callable(args[0]):
            _class_name = f"_DecCls{self.__name__}"
            _bases = (self, _FuncDecorator)
            _namespace = self.__dict__.copy()
            _namespace["_decorator_"] = True
            _DecCls = type(_class_name, _bases, _namespace)
            return _DecCls(args[0])
        else:
            raise TypeError("Unknown")


class Decorator(metaclass=DecoratorMeta):
    @abc.abstractmethod
    def decorate(self, func: T, *args: Any, **kwargs: Any) -> Any:
        """ Subclasses of GenericDecorator must implement the decorate method. """


decorator = Decorator


class _NoFuncDecorator(abc.ABC):
    def __init__(self, *args: Any, **kwargs: Any):
        self.args = args
        self.kwargs = kwargs

    def __call__(self, func: T) -> T:
        def decorated(*args: Any, **kwargs: Any) -> Any:
            return self.decorate(func, *args, **kwargs)

        return decorated


class _FuncDecorator(abc.ABC):
    def __init__(self, func, *args: Any, **kwargs: Any):
        self.func = func
        self.args = args
        self.kwargs = kwargs

    def __call__(self, *args, **kwargs) -> T:
        return self.decorate(self.func, *args, **kwargs)
