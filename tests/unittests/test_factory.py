import pytest
from pygim.factory import Factory

class DummyInterface:
    pass

class DummyImpl(DummyInterface):
    def __init__(self, x):
        self.x = x

def test_register_and_create():
    factory = Factory()
    def creator(x):
        return x * 2
    factory.register('double', creator)
    assert factory.create('double', 3) == 6

    # Decorator usage
    @factory.register('triple')
    def triple(x):
        return x * 3
    assert factory.create('triple', 4) == 12

    # Override
    def triple2(x):
        return x * 30
    with pytest.raises(RuntimeError):
        factory.register('triple', triple2)
    factory.register('triple', triple2, override=True)
    assert factory.create('triple', 2) == 60

def test_missing():
    factory = Factory()
    with pytest.raises(RuntimeError):
        factory.create('nope')

def test_interface_enforcement():
    import types
    # Use Python type as interface
    factory = Factory(interface=DummyInterface)
    @factory.register('ok')
    def ok(x):
        return DummyImpl(x)
    assert isinstance(factory.create('ok', 5), DummyInterface)

    @factory.register('bad')
    def bad(x):
        return object()
    with pytest.raises(RuntimeError):
        factory.create('bad', 1)

if __name__ == "__main__":
    from pygim.core.testing import run_tests
    run_tests(__file__, Factory.__module__, coverage=False)