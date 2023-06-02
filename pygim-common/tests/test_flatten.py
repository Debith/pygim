#type: ignore
import pytest

import numpy as np
from _pygim._utils._iterable._iterable import flatten, flatten_simple
from pygim.utils.performance import quick_timer

"""
numbers = [[n] for n in range(1, 3)]
conv = lambda x: x

with quick_timer("copy"):
    r0 = list(numbers)

with quick_timer("comprehension"):
    r1 = [l[0] for l in numbers]

with quick_timer("flatten_simple"):
    r2 = flatten_simple(numbers)

with quick_timer("flatten"):
    r3 = list(flatten(numbers))

with quick_timer("flatten_slow"):
    r4 = list(flatten_slow(numbers))
"""


#assert r1 == r2 == r3 == r4




@pytest.mark.parametrize("input,expected_result", [
    ("keep as is", ["keep as is"]),
    #(b"keep as is", [b"keep as is"]),
    #(memoryview(b"keep as is"), [b"keep as is"]),
    #(1, [1]),
    ([], []),
    ([1, 2, 3], [1, 2, 3]),
    (tuple([1, 2, 3]), [1, 2, 3]),
    (set([1, 2, 3]), [1, 2, 3]),
    ([1, [2, [3, [4, [5, [6]]]]]], [1, 2, 3, 4, 5, 6]),
    ([1, [], 2], [1, 2]),
    ([[[[[[[[[[[1]]]]]]]]]]], [1]),
    ((1,2,(3,4)), [1,2,3,4]),
    ((1,2,set([3,4])), [1,2,3,4]),
    ((1,2,np.array([3,4])), [1,2,3,4]),
    (range(4), [0,1,2,3]),
])
def test_flatten_with_various_types(input, expected_result):
    r = flatten(input)
    #assert inspect.isgenerator(r)
    actual_result = list(r)

    if actual_result != expected_result:
        assert False, f"Results differ:\n  ACTUAL: {list(flatten(input))}\nEXPECTED: {expected_result} "


if __name__ == "__main__":
    import pytest
    pytest.main([__file__])
