import pytest

from pygim.magic import null_object


@pytest.mark.parametrize('name', [
#    ('namespace.test', Unset, Unset, "namespace.test"),
])
def test_valid_naming_combinations(name):
    pass


def test_class_is_singleton():
    Empty = null_object.NullClassFactoryMeta("Empty")
    _Empty = null_object.NullClassFactoryMeta("Empty")

    assert id(Empty) == id(_Empty)


def test_instance_is_singleton():
    Empty = null_object.NullClassFactoryMeta("Empty")
    _Empty = null_object.NullClassFactoryMeta("Empty")

    empty1 = Empty()
    empty2 = Empty()
    empty3 = _Empty()
    empty4 = _Empty()

    assert id(empty1) == id(empty2) == id(empty3) == id(empty4)

if __name__ == "__main__":
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, null_object, coverage=False)