#type: ignore
import pytest

from pygim.utils.fast_iterable import tuplify, is_container


class CustomIterableObject:
    def __iter__(self):
        return []

class CustomNonIterableObject:
    pass


@pytest.mark.parametrize("input,expected_result", [
    ((1, 2, 3),                  True),     # Tuple is a container
    ([1, 2, 3],                  True),     # List is a container
    (set([1, 2, 3]),             True),     # Set is a container
    (range(1, 4),                True),     # Range is a container
    ("123",                      False),    # String is not considered a container
    (b"123",                     False),    # Byte string is not considered a container
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
    actual_result = is_container(input)

    if actual_result != expected_result:
        assert False, f"Results differ:\n  ACTUAL: {actual_result}\nEXPECTED: {expected_result} "


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
    (iter([1, 2, 3]),           (1, 2, 3)),           # Iterable gets converted to tuple
    ((i for i in range(1, 4)),  (1, 2, 3)),           # Generator gets converted to tuple
])
def test_tuplify_with_various_types(input, expected_result):
    actual_result = tuplify(input)

    if actual_result != expected_result:
        assert False, f"Results differ:\n  ACTUAL: {actual_result}\nEXPECTED: {expected_result} "


if __name__ == "__main__":
    import pytest
    pytest.main([__file__])
