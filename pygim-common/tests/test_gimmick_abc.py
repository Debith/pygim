# -*- coding: utf-8 -*-
import pytest

def test_interface_is_abstract():
    from pygim.gimmicks.abc import interface, GimABCError

    class ExampleInterface(interface):
        pass

    with pytest.raises(GimABCError, match="Can't instantiate interface!"):
        ExampleInterface()


def test_single_method_interface():
    from pygim.gimmicks.abc import interface, GimABCError

    class ExampleInterface(interface):
        def test_func(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_func"):
        ExampleInterface()


def test_single_property_interface():
    from pygim.gimmicks.abc import interface, GimABCError

    class ExampleInterface(interface):
        @property
        def test_prop(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_prop"):
        ExampleInterface()


def test_single_classmethod_interface():
    from pygim.gimmicks.abc import interface, GimABCError

    class ExampleInterface(interface):
        @classmethod
        def test_classmethod(cls): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_classmethod"):
        ExampleInterface()


def test_single_staticmethod_interface():
    from pygim.gimmicks.abc import interface, GimABCError

    class ExampleInterface(interface):
        @staticmethod
        def test_staticmethod(): pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with abstract methods: test_staticmethod"):
        ExampleInterface()


def test_with_multiple_methods_interface():
    from pygim.gimmicks.abc import interface, GimABCError

    class ExampleInterface(interface):
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
    from pygim.gimmicks.abc import interface, GimABCError

    class Left(interface):
        def test_func1(): pass

    class Right(interface):
        def test_func2(): pass

    class ExampleInterface(Left, Right):
        pass

    with pytest.raises(
            GimABCError,
            match="Can't instantiate interface ``ExampleInterface`` with "
                  "abstract methods: test_func1, test_func2"):
        ExampleInterface()


def test_with_implementation_of_single_method_interface():
    from pygim.gimmicks.abc import interface

    class ExampleInterface(interface):
        def test_func(): pass

    class ExampleClass(ExampleInterface):
        def test_func(): pass

    ExampleClass()


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, None, coverage=False)
