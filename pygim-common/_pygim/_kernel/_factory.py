# -*- coding: utf-8 -*-
"""
This contains a generic factory that works in open-closed principle.
"""

import typing as t

from .._magic._entangled import EntangledClassMeta
from .._magic._support import classproperty
from .._error_msgs import type_error_msg


class FactoryMeta(EntangledClassMeta):
    @classmethod
    def __prepare__(cls, _, bases):
        mapping = super().__prepare__(_, bases)
        mapping["__registered_classes"] = {}  # Nasty trickery avoiding mangling.
        return mapping

GENERATED = object()

class Factory(metaclass=FactoryMeta):
    """
    This is a generic factory that works in open-closed principle.

    This factory expects that all of the registered objects are callable.
    Based on the name of the object, you can get the object by using the
    dictionary syntax or alternatively, you can call generated function
    starting with ``create_`` and ending with the name of the object.

    Furthermore, this factory is entangled, meaning that you can define
    additional methods and properties to the factory and they will be
    available to that particular factory class. This is highly convenient
    as you can define a single factory class for the project and then
    extend it in different modules.

    Examples
    --------

    Registering a class:
    ```python
    from pygim.factory import Factory

    class MyFactory(Factory):
        pass
        
    @MyFactory.register
    class MyClass:
        pass
        
    assert MyFactory.registered == "['MyClass']"
    assert MyFactory["MyClass"] == MyClass
    assert MyFactory.create_my_class() == MyClass()
    ```
    Above example demonstrates how to register a class to the factory. It
    is useful as there is a way to get the class by using the dictionary
    syntax, instead of normal function call syntax. Furthermore, you can
    create a class instance by using the generated function, making the
    code more readable.    
    """
    @classmethod
    def clear(cls):
        pass

    @classmethod
    def __getitem__(self, object_name: str):
        pass

    @classmethod
    def register(cls, factory, *, obj_name: str = GENERATED, func_name: str = GENERATED):
        if not callable(factory):
            raise TypeError(type_error_msg(factory, t.Callable))
        
        if obj_name is GENERATED:
            obj_name = factory.__name__
        
        if func_name is GENERATED:
            func_name = f"create_{obj_name.lower()}"

        cls.__registered_classes[obj_name] = factory
        setattr(cls, func_name, factory)

    @classproperty
    def registered(cls):
        return str(getattr(cls, "__registered_classes").keys())