import pytest
from pygim.factory import Factory


@pytest.fixture
def factory():
    return Factory()


@pytest.fixture
def dummy_interface():
    class DummyInterface:
        pass

    return DummyInterface


@pytest.fixture
def dummy_impl(dummy_interface):
    class DummyImpl(dummy_interface):
        def __init__(self, x):
            self.x = x

    return DummyImpl


def test_register_and_getitem(factory):
    def foo(x):
        return x

    factory.register("foo", foo)
    # __getitem__ should return the same callable
    assert callable(factory["foo"])
    assert factory["foo"] is foo


def test_register_invalid_callable(factory):
    # Passing a non‐callable should raise TypeError immediately
    with pytest.raises(TypeError):
        factory.register("bad", 123)


def test_decorator_usage(factory):
    @factory.register("double")
    def double(x):
        return x * 2

    assert factory.create("double", 3) == 6


def test_create_with_kwargs(factory):
    def adder(x, y=0):
        return x + y

    factory.register("adder", adder)
    assert factory.create("adder", 2, y=3) == 5


def test_register_duplicate_without_override(factory):
    def foo(x):
        return x

    factory.register("foo", foo)

    def bar(x):
        return x + 1

    # registering again without override should fail
    with pytest.raises(RuntimeError):
        factory.register("foo", bar)
    # but override=True should succeed
    factory.register("foo", bar, override=True)
    assert factory.create("foo", 1) == 2


def test_interface_enforcement(dummy_interface, dummy_impl):
    f = Factory(interface=dummy_interface)

    @f.register("ok")
    def ok(x):
        return dummy_impl(x)

    # should pass the interface check
    assert isinstance(f.create("ok", 7), dummy_interface)

    @f.register("bad")
    def bad(x):
        return object()

    # should raise since object() doesn’t implement DummyInterface
    with pytest.raises(RuntimeError):
        f.create("bad", 1)


def test_missing(factory):
    # creating a non‐existent entry should raise
    with pytest.raises(RuntimeError):
        factory.create("nope")


def test_registered_callables(factory):
    factory.register("a", lambda: 1)
    factory.register("b", lambda: 2)
    names = factory.registered_callables()
    assert set(names) == {"a", "b"}


def test_use_module_success(factory):
    # built‑in module always present
    try:
        factory.use_module("math")
    except Exception as e:
        pytest.fail(f"Unexpected exception raised: {e}")


def test_use_module_failure(factory):
    with pytest.raises(ModuleNotFoundError):
        factory.use_module("definitely_not_a_module_123")


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, Factory.__module__, coverage=False)
