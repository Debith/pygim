import pytest
import inspect
from pygim.utils.iterable import flatten


@pytest.mark.parametrize("input,expected_result", [
    ("keep as is", ["keep as is"]),
    (b"keep as is", [b"keep as is"]),
    (memoryview(b"keep as is"), [b"keep as is"]),
    (1, [1]),
    ([], []),
    ([1, 2, 3], [1, 2, 3]),
    (range(4), [0,1,2,3]),
])
def test_flatten_with_various_types(input, expected_result):
    r = flatten(input)
    assert inspect.isgenerator(r)
    actual_result = list(r)

    if actual_result != expected_result:
        assert False, f"Results differ:\n  ACTUAL: {list(flatten(input))}\nEXPECTED: {expected_result} "


if __name__ == "__main__":
    import pytest
    pytest.main([__file__])