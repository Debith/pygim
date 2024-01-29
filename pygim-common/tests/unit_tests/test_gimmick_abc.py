# -*- coding: utf-8 -*-
# type: ignore
import pytest
from dataclasses import dataclass

def test_interface_is_abstract():
    from pygim.gimmicks.abc import Interface, GimABCError

    class ExampleInterface(Interface):
        pass

    with pytest.raises(GimABCError, match="Can't instantiate interface!"):
        ExampleInterface()


def test_single_method_interface():
    from pygim.gimmicks.abc import Interface, GimABCError

    class ExampleInterface(Interface):
        def test_func(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_func"):
        ExampleInterface()


def test_single_property_interface():
    from pygim.gimmicks.abc import Interface, GimABCError

    class ExampleInterface(Interface):
        @property
        def test_prop(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_prop"):
        ExampleInterface()


def test_single_classmethod_interface():
    from pygim.gimmicks.abc import Interface, GimABCError

    class ExampleInterface(Interface):
        @classmethod
        def test_classmethod(cls): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_classmethod"):
        ExampleInterface()


def test_single_staticmethod_interface():
    from pygim.gimmicks.abc import Interface, GimABCError

    class ExampleInterface(Interface):
        @staticmethod
        def test_staticmethod(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_staticmethod"):
        ExampleInterface()


def test_with_multiple_methods_interface():
    from pygim.gimmicks.abc import Interface, GimABCError

    class ExampleInterface(Interface):
        def test_func(): pass

        @property
        def test_prop(): pass

        @classmethod
        def test_classmethod(cls): pass

        @staticmethod
        def test_staticmethod(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract "
                  "methods: test_classmethod, test_func, test_prop, test_staticmethod"):
        ExampleInterface()


def test_with_multiple_interfaces():
    from pygim.gimmicks.abc import Interface, GimABCError

    class Left(Interface):
        def test_func1(): pass

    class Right(Interface):
        def test_func2(): pass

    class ExampleInterface(Left, Right):
        pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with "
                  "abstract methods: test_func1, test_func2"):
        ExampleInterface()


def test_with_implementation_of_single_method_interface():
    from pygim.gimmicks.abc import Interface

    class ExampleInterface(Interface):
        def test_func(): pass

    class ExampleClass(ExampleInterface):
        def test_func(): pass

    ExampleClass()


def test_with_nested_interface_inheritance():
    from pygim.gimmicks.abc import Interface, GimABCError

    class First(Interface):
        def test_func1(): pass

    # TODO: How to deal nested interface inheritance?
    #       The issue is that it is quite difficult to discern
    #       whether the interface is nested or not.
    class Second(First, Interface):  # expect Interface to be declared.
        def test_func2(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``Second`` with abstract methods: test_func1, test_func2"):
        Second()


def test_with_multiple_interface_inheritance():
    from pygim.gimmicks.abc import Interface

    class First(Interface):
        def test_func1(): pass

    class Second(First):
        def test_func2(): pass

    class ExampleClass(Second):
        def test_func1(): pass

        def test_func2(): pass

    ExampleClass()


def test_interface_with_body_containing_implementation():
    from pygim.gimmicks.abc import Interface, GimABCError

    with pytest.raises(
        GimABCError,
        match="Interface functions are intended to be empty! "
              "Use ``pygim.gimmicks.abc.AbstractClass`` "
              "if you need function to contain body."):
        class ExampleInterface(Interface):
            def test_func():
                return 1


def test_interface_name_space_contains_abc_modules_methods():
    from pygim.gimmicks.abc import Interface

    try:
        class ExampleInterface(Interface):
            @abstractmethod
            def method(self): pass
    except Exception:
        pytest.fail("Failed to create Interface with ``abstractmethod`` decorator!")


def test_abstract_class_with_body_containing_implementation():
    from pygim.gimmicks.abc import AbstractClass

    class ExampleAbstractClass(AbstractClass):
        def test_func():
            return 1

    assert ExampleAbstractClass.test_func() == 1


def test_interface_with_value_override_with_attribute_with_default():
    from pygim.gimmicks.abc import Interface

    class ExampleInterface(Interface):
        @property
        def test_prop_with_default(self):
            pass
    
    
    @dataclass
    class ExampleImpl(ExampleInterface):
        test_prop_with_default: int = 42

    
    example = ExampleImpl()
    assert example.test_prop_with_default == 42


def test_interface_with_value_override_with_attribute_without_default():
    from pygim.gimmicks.abc import Interface

    class ExampleInterface(Interface):
        @property
        def test_prop_with_default(self):
            pass
    
    
    @dataclass
    class ExampleImpl(ExampleInterface):
        test_prop: int

    
    example = ExampleImpl(42)
    assert example.test_prop == 42


def test_interface_with_override_with_class_attribute_throwing_an_error():
    from pygim.gimmicks.abc import Interface, GimABCError

    class ExampleInterface(Interface):
        @property
        def test_prop_with_default(self):
            pass
    

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``Second`` with abstract methods: test_func1"):
        class ExampleImpl(ExampleInterface):
            test_prop_with_default = 42

    
    example = ExampleImpl()
    assert example.test_prop_with_default == 42

if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, None, coverage=False)
