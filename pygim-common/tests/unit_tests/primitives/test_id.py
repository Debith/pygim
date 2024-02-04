import pytest
from pygim.primitives import ID


def test_two_ids_are_same():
    _id1 = ID(123)
    _id2 = ID(123)
    assert _id1 == _id2
    assert _id1 is not _id2  # TODO: in future, this will be True
    assert hash(_id1) == hash(_id2)


def test_ids_can_created_pseudo_randomly():
    _id1 = ID.random()
    _id2 = ID.random()

    assert _id1 != _id2


def test_multiple_random_numbers_are_still_unique():
    ids = [ID(i) for i in range(100_000)]
    assert len(ids) == len(set(ids))


# FIXME: True and False are converted to numbers?
@pytest.mark.parametrize("arg", [123.123, "123", None])
def test_must_be_integer_number(arg):
    with pytest.raises(TypeError):
        ID(arg)


if __name__ == "__main__":
    pytest.main([__file__])