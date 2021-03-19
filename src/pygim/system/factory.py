"""
This contains a generic factory that works in open-closed principle.
"""

from functools import partial
from typing import MutableMapping, Mapping, Callable, Any, Union
from collections import defaultdict as ddict
import inspect

from ..system.exceptions import FactoryMethodRegisterationException

__all__ = ('Factory', 'FactoryMethodRegisterationException')

_CREATE_PREFIX = "create_"


class NameMeta(type):
    _cache = {}

    @classmethod
    def precache(cls, name, name_obj):
        cls._cache[name] = name_obj

    def _create_sublcass_instance(self, *args, **kwargs):
        try:
            instance = super().__call__(*args, **kwargs)
        except TypeError:
            instance = super().__call__(*kwargs)
        return instance

    def _identify_and_create(self, name):
        if isinstance(name, str):
            name_obj = ValidName(name)
        elif inspect.isroutine(name):
            name_obj = ValidName(name.__name__)
        elif inspect.isclass(name):
            name_obj = ValidName(name.__name__)
        else:
            raise TypeError(f"Can't identify name for {self}")
        return name_obj

    def __call__(self, name=None) -> Any:
        # Return the cached item immediately!
        try:
            return self._cache[name]
        except KeyError:
            pass

        # Sometimes it is possible to call this function providing another
        # name object. That can be returned back right away.
        if isinstance(name, Name):
            return name

        if self is not Name:
            instance = self._create_sublcass_instance(name)
            self._cache[name] = instance
            return instance

        # Caching happens automatically when the subclass is created.
        return self._identify_and_create(name)


class Name(metaclass=NameMeta):
    """ Base class for everything. """


class NoName(Name):
    """ Does almost nothing. """
    def __len__(self):
        return 0

    def __repr__(self):
        return "Name(None)"

    def __str__(self):
        return ""

    def __hash__(self):
        return 0

    def __eq__(self, _):
        return False

    def __add__(self, other):
        assert isinstance(other, (str, Name))
        if str(other):
            return Name(other)
        return self

    def __or__(self, other):
        assert isinstance(other, (str, Name))
        return Name(other)

    def startswith(self, *_):
        return False

    @property
    def namespace(self):
        return self

    def has_namespace(self):
        pass


class ValidName(Name):
    def __init__(self, name):
        self._name = name

    def __len__(self):
        return len(self._name)

    def __repr__(self):
        return f'Name(name="{self._name}")'  #pragma:nocover

    def __str__(self):
        return self._name

    def __hash__(self):
        return hash(self._name)

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return False
        return self._name == other._name

    def __add__(self, other):
        assert isinstance(other, (str, Name))
        if not self._name:
            return other
        if not str(other):
            return self
        return self.__class__(f"{self._name}.{str(other)}")

    def __or__(self, other):
        assert isinstance(other, (str, Name))
        if self._name:
            return self
        return other

    def startswith(self, text):
        return self._name.startswith(text)

    @property
    def with_prefix(self):
        if self._name.startswith(_CREATE_PREFIX):
            return self
        return self.__class__(f'{_CREATE_PREFIX}{self._name}')

    @property
    def without_prefix(self):
        if self._name.startswith(_CREATE_PREFIX):
            return self.__class__(self._name.replace(_CREATE_PREFIX, ''))
        return self

    @property
    def namespace(self):
        if not self.has_namespace():
            return Name()
        return Name(self._name.split(".")[0])


    def has_namespace(self):
        return "." in self._name

_no_name = NoName()
NameMeta.precache("", _no_name)
NameMeta.precache(None, _no_name)

DEFAULT_NAMESPACE = Name('__main__')


class FactoryMeta(type):
    __factories: MutableMapping[str, Any] = ddict(dict)

    @classmethod
    def _resolve_factory_name(cls, name, qualname, enforced_type, kwargs):
        """
        Resolve the factory name based on the given arguments. Namespace is included in the name.
        """
        name = Name(name)
        qualname = Name(qualname)
        typename = Name(enforced_type)
        module_name = Name(getattr(enforced_type, '__module__', None))
        namespace = Name(kwargs.pop("namespace", None))

        partialname = qualname + typename + name
        namespace = namespace | typename.namespace | module_name

        factory_name = namespace + partialname

        if not factory_name.has_namespace():
            raise Exception()
        return factory_name

    def _register_factories(self, factory_callables, factory_instance):
        for name, factory_callable in factory_callables.items():
            if factory_instance._obj_type and not isinstance(factory_callable, factory_instance._obj_type):
                raise Exception("Failed do the right thing!")
            self._register_type(factory_callable, factory_instance, alias=name, alias_only=False)

    @staticmethod
    def _to_dict(name: Name, func, mapping, override):
        assert isinstance(name, Name)
        try:
            existing_func = mapping[name]
            if id(existing_func) == id(func):
                return

            if not override:
                raise FactoryMethodRegisterationException('Override with new')
        except KeyError:
            pass

        mapping[name] = func

    def _register_type(self, object_factory, factory_instance, *, alias=None, override=False, alias_only=True):
        object_factory_name = Name(object_factory)
        if inspect.isroutine(object_factory) and not object_factory_name.startswith(_CREATE_PREFIX):
            raise FactoryMethodRegisterationException('Fail')

        if not alias or alias and not alias_only:
            self._to_dict(object_factory_name.without_prefix, object_factory, factory_instance._objects, override)
            self._to_dict(object_factory_name.with_prefix,    object_factory, factory_instance._methods, override)

        if not alias:
            return

        alias = Name(alias)
        if alias != object_factory_name.without_prefix:
            self._to_dict(alias, object_factory, factory_instance._objects, override)

    def __getitem__(self, factory_name):
        factory_name = Name(factory_name)
        return self.__factories[factory_name.namespace][factory_name]

    def _acquire(self, factory_name: Name, enforced_type: Any, factory_callables: Mapping):
        """ Retrieve factory instance based on the name. """
        assert isinstance(factory_name, Name)
        assert not enforced_type or inspect.isclass(enforced_type), f"Enforced type must be class, got f{type(enforced_type)}"

        try:
            factory = self.__factories[factory_name.namespace][factory_name]

        except KeyError:
            factory = super().__call__()
            factory._name = factory_name
            factory._obj_type = enforced_type
            factory._objects = dict()
            factory._methods = dict()
            self._register_factories(factory_callables, factory)
            self.__factories[factory_name.namespace][factory_name] = factory

        return factory

    def __call__(self, name: str=None, enforced_type: Any = None, **kwargs: Any) -> Any:
        # TODO: Remove this when testing done.
        # if self is Factory:
        qualname = None if self.__qualname__ == "Factory" else self.__qualname__
        factory_name = self._resolve_factory_name(name, qualname, enforced_type, kwargs)
        factory = self._acquire(factory_name, enforced_type, kwargs)
        return factory


class Factory(metaclass=FactoryMeta):
    """
    This a base factory class that can be used to create objects of any kind.

    To use this class, there are two different approaches, direct instantanation
    or subclassing. Subclassing approach is the recommended approach, as it makes
    your factory explicit, and therefore less error-prone and more readable.

    An example use as a class:
    ```python
    class MyFactory(Factory):
        def create_my_object(self, *args, **kwargs):
            return MyObject(*args, **kwargs)  # Obviously you can put additional logic here!

    my_factory = MyFactory()
    my_object.create_my_object()  # Creates an instance of MyObject class
    ```

    There are couple of gotchas with the usage of this class. First of all, using `create_`
    prefix for factory methods (or any function registered to this factory) is a requirement.
    The reason is simple, it creates an ubiquituos language within your codebase, which in turn
    simplifies communication and readability of the code. It also allows additional magic put
    into the factory, which allows creating objects by their name. The name of the object is
    derived from the function name by removing the `create_`-prefix. This can be very useful,
    when creating objects and calling them by their name, allowing you to parameterize the
    creation even further. For example:

    ```python
    object_name = 'my_object'
    my_factory[object_name]()  # Creates an instance of MyObject class
    ```
    """
    def register(self, func=None, **kwargs):
        """
        Registers factory method into this factory. This function works also as a decorator.
        """
        def inner_func(func: Union[type, Callable], *, alias=None, override=False, alias_only=True):
            self.__class__._register_type(func, self, alias=alias, override=override, alias_only=alias_only)
            return func

        if callable(func):
            return inner_func(func, **kwargs)

        return partial(inner_func, **kwargs)

    def __getattribute__(self, name: str) -> Any:
        try:
            return super().__getattribute__(name)
        except AttributeError:
            try:
                return self._methods[Name(name)]
            except KeyError:
                raise AttributeError('Epic Fail')

    def __getitem__(self, key):
        return self._objects[Name(key)]

    def __repr__(self):
        return f'{self.__class__.__name__}("{self._name}")'  #pragma:nocover
