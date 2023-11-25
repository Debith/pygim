# type: ignore
from pygim.gimmicks.abc import interface


class ExampleInterface(interface):
    def test_func(): pass

    @property
    def test_prop(): pass


class ExampleClass(ExampleInterface):
    def test_func(): pass


ExampleClass()