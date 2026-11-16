import types

import pytest

from pygim.each import each


class Dummy:
    def __init__(self, value):
        self.value = value

    def multiply(self, factor=2):
        return self.value * factor

    @property
    def text(self):
        return f"Value is {self.value}"


@pytest.fixture
def dummy_list():
    return [Dummy(i) for i in range(5)]


@pytest.fixture
def each_instance(dummy_list):
    return each(dummy_list)


@pytest.mark.parametrize(
    "attr_name,expected",
    [
        ("value", [0, 1, 2, 3, 4]),
        (
            "text",
            ["Value is 0", "Value is 1", "Value is 2", "Value is 3", "Value is 4"],
        ),
    ],
)
def test_attribute_access(each_instance, attr_name, expected):
    """
    Test that accessing attributes via each returns a list of attribute values from all items.
    """
    result = getattr(each_instance, attr_name)
    assert result == expected


@pytest.mark.parametrize(
    "method_name,args,kwargs,expected",
    [
        ("multiply", (), {}, [0, 2, 4, 6, 8]),
        ("multiply", (3,), {}, [0, 3, 6, 9, 12]),
        ("multiply", (), {"factor": 4}, [0, 4, 8, 12, 16]),
    ],
)
def test_method_call(each_instance, method_name, args, kwargs, expected):
    """
    Test that calling methods via each calls the method on all items with given arguments and returns list of results.
    """
    method = getattr(each_instance, method_name)
    result = method(*args, **kwargs)
    assert result == expected


def test_missing_attribute(each_instance):
    """
    Test that accessing an attribute missing from any element raises
    AttributeError immediately (documented Proxy contract), instead of
    smuggling exception instances into the result list.
    """
    with pytest.raises(AttributeError, match="nonexistent"):
        getattr(each_instance, "nonexistent")


@pytest.mark.parametrize(
    "iterable",
    [
        [],
        [Dummy(1)],
        [Dummy(i) for i in range(100)],
    ],
)
def test_each_with_various_iterables(iterable):
    """
    Test that each works correctly with different iterable sizes, including empty and large.
    """
    e = each(iterable)
    if iterable:
        assert getattr(e, "value") == [item.value for item in iterable]
    else:
        assert getattr(e, "value") is None


@pytest.mark.parametrize(
    "iterable",
    [
        [],
        [Dummy(1)],
    ],
)
def test_call_without_method(iterable):
    """
    Test that calling each instance without a method set raises an error.
    """
    e = each(iterable)
    with pytest.raises(TypeError):
        e()


@pytest.mark.parametrize(
    "iterable",
    [
        [],
        [Dummy(1)],
    ],
)
def test_call_after_method_access(iterable):
    """
    Test that calling after method access works and returns expected results.
    """
    e = each(iterable)
    if iterable:
        method = getattr(e, "multiply")
        result = method(3)
        assert result == [item.multiply(3) for item in iterable]
    else:
        assert getattr(e, "multiply") is None


def test_each_as_descriptor(dummy_list):
    """The descriptor form (each = each() in a class body) broadcasts per instance.

    Guards the fix for __set_name__ testing whether the class *object* was
    iterable -- which no ordinary class is -- instead of whether it defines
    __iter__ for its instances. Attribute and method broadcasts through the
    descriptor must behave exactly like the each(iterable) factory form.
    """
    class Fleet:
        each = each()

        def __init__(self, items):
            self._items = items

        def __iter__(self):
            return iter(self._items)

    fleet = Fleet(dummy_list)

    assert fleet.each.value == [0, 1, 2, 3, 4]
    assert fleet.each.multiply(3) == [0, 3, 6, 9, 12]


def test_each_descriptor_rejects_class_without_iter():
    """Placing the descriptor on a class without __iter__ fails at class creation.

    The early __set_name__ check is the guard rail that makes descriptor
    misuse a definition-time error (where the mistake is visible) rather
    than a confusing attribute error at first access. Python < 3.12 wraps
    exceptions raised in __set_name__ in a RuntimeError; 3.12+ propagates
    the TypeError unchanged, so both are accepted here.
    """
    with pytest.raises((TypeError, RuntimeError)):

        class NotIterable:
            each = each()


def test_each_broadcast_over_builtin_iterables():
    """Broadcasting works over arbitrary iterables of builtin types.

    Nothing in the proxy may assume user-defined classes or list inputs:
    tuples of str and a range of ints must broadcast method calls just the
    same, with results collected in iteration order.
    """
    assert each(("a", "bb")).upper() == ["A", "BB"]
    assert each(range(3)).bit_length() == [0, 1, 2]


def _a_fleet():
    """A class holding the descriptor, with instances that iterate."""
    class Fleet:
        each = each()

        def __iter__(self):
            return iter(())

    return Fleet


def test_the_descriptor_refuses_a_generator():
    """A generator is single-pass, and the proxy reads its iterable twice, so a generator is refused
    rather than drained. Only the descriptor's own __get__ can be handed one: no class makes its
    instances generators."""
    Fleet = _a_fleet()
    with pytest.raises(TypeError, match="^`each` cannot be used on generator instances$"):
        vars(Fleet)["each"].__get__((item for item in [Dummy(1)]), Fleet)


class _SaysItIsAGenerator:
    """Iterable, and a generator by isinstance() — which believes __class__ — though not by its type."""

    @property
    def __class__(self):
        return types.GeneratorType

    def __iter__(self):
        return iter(())


def test_the_descriptor_refuses_what_isinstance_calls_a_generator():
    """What counts as a generator is isinstance(x, types.GeneratorType), Python's own answer (as
    inspect.isgenerator gives it), and that believes an object's __class__."""
    Fleet = _a_fleet()
    assert isinstance(_SaysItIsAGenerator(), types.GeneratorType)
    with pytest.raises(TypeError, match="^`each` cannot be used on generator instances$"):
        vars(Fleet)["each"].__get__(_SaysItIsAGenerator(), Fleet)


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, pytest_args=["-v", "--tb=short"])
