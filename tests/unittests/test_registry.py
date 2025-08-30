import pytest
from pygim.registry import Registry


@pytest.fixture
def registry():
    return Registry()


def test_register_and_getitem(registry):
    def foo():
        return 1

    registry.register("foo", foo)
    assert "foo" in registry
    assert callable(registry["foo"])
    assert registry["foo"]() == 1


def test_len_and_registered_names(registry):
    def foo():
        return 1

    def bar():
        return 2

    registry.register("foo", foo)
    registry.register("bar", bar)
    # assert set(registry.registered_names()) == {"foo", "bar"}
    assert len(registry) == 2


def xtest_contains_and_remove(registry):
    def foo():
        return 1

    registry.register("foo", foo)
    assert "foo" in registry
    del registry["foo"]
    assert "foo" not in registry
    # deleting a nonexistent key should not raise
    del registry["nonexistent"]
    assert "nonexistent" not in registry


def xtest_register_duplicate_without_override(registry):
    def foo():
        return 1

    def bar():
        return 2

    registry.register("foo", foo)
    # duplicate without override should fail
    with pytest.raises(RuntimeError):
        registry.register("foo", bar)
    # override=True should succeed
    registry.register("foo", bar, override=True)
    assert registry["foo"]() == 2


def xtest_decorator_usage(registry):
    @registry.register("foo")
    def foo():
        return "bar"

    assert registry["foo"]() == "bar"

    # decorator duplicate without override should fail
    with pytest.raises(RuntimeError):

        @registry.register("foo")
        def foo2():
            return "baz"

    # decorator with override=True
    @registry.register("foo", override=True)
    def foo3():
        return "baz"

    assert registry["foo"]() == "baz"


def test_missing_key_raises(registry):
    with pytest.raises(RuntimeError):
        _ = registry["missing"]


def test_repr(registry):
    # repr should not crash even when empty
    repr(registry)


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, Registry.__module__, coverage=False)
