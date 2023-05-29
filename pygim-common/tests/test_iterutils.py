#type: ignore
import pytest

import numpy as np
from pygim.utils.fast_iterable import tuplify

@pytest.mark.parametrize("input,expected_result", [
    ((1, 2, 3), (1, 2, 3)),  # Tuple remains unchanged
    ([1, 2, 3], (1, 2, 3)),  # List gets converted to tuple
    (set([1, 2, 3]), (1, 2, 3)),  # Set gets converted to tuple
    (range(1, 4), (1, 2, 3)),  # Range gets converted to tuple
    ("123", ("123",)),  # String remains as single-element tuple
    (b"123", (b"123",)),  # Byte string remains as single-element tuple
    (123, (123,)),  # Integer remains as single-element tuple
    (123.456, (123.456,)),  # Float remains as single-element tuple
    (None, (None,)),  # None remains as single-element tuple
    (True, (True,)),  # Boolean remains as single-element tuple
    ({"a": 1}, (("a", 1),)),  # Dictionary remains as single-element tuple
    (complex(1, 2), (complex(1, 2),)),  # Complex number remains as single-element tuple
    (iter([1, 2, 3]), (1, 2, 3)),  # Iterable gets converted to tuple
    ((i for i in range(1, 4)), (1, 2, 3)),  # Generator gets converted to tuple
])
def test_flatten_with_various_types(input, expected_result):
    actual_result = tuplify(input)

    if actual_result != expected_result:
        assert False, f"Results differ:\n  ACTUAL: {actual_result}\nEXPECTED: {expected_result} "


if __name__ == "__main__":
    import pytest
    pytest.main([__file__])
