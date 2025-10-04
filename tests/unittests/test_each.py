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
    Test that accessing a missing attribute returns a list of AttributeError instances.
    """
    result = getattr(each_instance, "nonexistent")
    assert all(isinstance(e, AttributeError) for e in result)


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


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, pytest_args=["-v", "--tb=short"])
