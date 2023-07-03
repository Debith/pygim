#type: ignore
import pytest

from _pygim.common_fast import (
    tuplify as tuplify_fast,
    is_container as is_container_fast)
from _pygim._iterlib import is_container, tuplify


class CustomIterableObject:
    def __iter__(self):
        return []


class CustomNonIterableObject:
    pass

'''
SPLIT_TESTS = [
    ([1, 2, 3, 4], lambda v: v % 2, ([1, 3], [2, 4])),
    ([1, 2, 3, 4], lambda v: v <= 2, ([1, 2], [3, 4])),
    ([], lambda v: v <= 2, ([], [])),
]

@pytest.mark.parametrize("input,func,expected_result", SPLIT_TESTS)
def test_split(input, func, expected_result):

    actual_result = split(input, func)
    if not equals(actual_result, expected_result):
        assert False, f"{actual_result} != {expected_result}"

'''

@pytest.mark.parametrize("input,expected_result", [
    (str,                       False),     # str-type is not a container
    (bytes,                     False),     # bytes-type is not a container
    (bytearray,                 False),     # bytearray-type is not a container
    (memoryview,                False),     # memoryview-type is not a container
    (range,                     False),     # range-type is not a container
    (list,                      False),     # list-type is not a container
    (tuple,                     False),     # tuple-type is not a container
    (int,                       False),     # int-type is not a container
    (float,                     False),     # float-type is not a container
    (complex,                   False),     # complex-type is not a container
    (set,                       False),     # set-type is not a container
    (frozenset,                 False),     # frozenset-type is not a container
    (dict,                      False),     # dict-type is not a container

    ((1, 2, 3),                  True),     # Tuple is a container
    ([1, 2, 3],                  True),     # List is a container
    (set([1, 2, 3]),             True),     # Set is a container
    (range(1, 4),                True),     # Range is a container
    ("123",                      False),    # String is not considered a container
    (b"123",                     False),    # Byte string is not considered a container
    (bytearray(122),             True),     # Byte array is not considered a container
    (123,                        False),    # Integer is not a container
    (123.456,                    False),    # Float is not a container
    (None,                       False),    # None is not a container
    (True,                       False),    # Boolean is not a container
    ({"a": 1},                   True),     # Dictionary is a container
    (complex(1, 2),              False),    # Complex number is not a container
    (iter([1, 2, 3]),            True),     # Iterator is a container
    ((i for i in range(1, 4)),   True),     # Generator is a container
    (memoryview(b"123"),         True),     # Memoryview is a container
    (CustomIterableObject(),     True),     # Custom iterable object is a container
    (CustomNonIterableObject(),  False),    # Custom non-iterable object is not a container
    ([[1, 2, 3]],                True),     # List of lists is a container
    ({'a': set([1, 2, 3])},      True),     # Dictionary of sets is a container
])

def test_is_container_with_various_types(input, expected_result):
    actual_result_fast = is_container_fast(input)
    actual_result = is_container(input)

    if not (actual_result == actual_result_fast == expected_result):
        assert False, "\n".join([
            f"Results differ for `{input}`:",
            f"       ACTUAL: {actual_result}",
            f"ACTUAL (fast): {actual_result_fast}",
            f"     EXPECTED: {expected_result} ",
            ])


@pytest.mark.parametrize("input,expected_result", [
    ((1, 2, 3),                 (1, 2, 3)),           # Tuple remains unchanged
    ([1, 2, 3],                 (1, 2, 3)),           # List gets converted to tuple
    (set([1, 2, 3]),            (1, 2, 3)),           # Set gets converted to tuple
    (range(1, 4),               (1, 2, 3)),           # Range gets converted to tuple
    ("123",                     ("123",)),            # String remains as single-element tuple
    (b"123",                    (b"123",)),           # Byte string remains as single-element tuple
    (123,                       (123,)),              # Integer remains as single-element tuple
    (123.456,                   (123.456,)),          # Float remains as single-element tuple
    (None,                      (None,)),             # None remains as single-element tuple
    (True,                      (True,)),             # Boolean remains as single-element tuple
    ({"a": 1},                  (("a", 1),)),         # Dictionary remains as single-element tuple
    (complex(1, 2),             (complex(1, 2),)),    # Complex number remains as single-element tuple
])
def test_tuplify_with_various_types(input, expected_result):
    actual_result = tuplify(input)
    actual_result_fast = tuplify_fast(input)

    if not (actual_result == actual_result_fast == expected_result):
        assert False, "\n".join([
            f"Results differ for `{input}`:",
            f"       ACTUAL: {actual_result}",
            f"ACTUAL (fast): {actual_result_fast}",
            f"     EXPECTED: {expected_result} ",
            ])


def test_tuplify_with_generators():
    assert tuplify(iter([1, 2, 3])) == tuplify_fast(iter([1, 2, 3])) == (1,2,3)
    assert tuplify(i for i in range(1, 4)) == tuplify_fast(i for i in range(1, 4)) == (1,2,3)



if __name__ == "__main__":
    import pytest
    pytest.main([__file__])
