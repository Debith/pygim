# -*- coding: utf-8 -*-
import pytest
from pygim import EntangledClass, encls


@pytest.mark.parametrize('left, right, expected_result', [
    ("test1", "test2", False),
    ("test1", "test1", True),
])
def test_entangled_class_equality(left, right, expected_result):
    left_cls = EntangledClass[left]
    right_cls = EntangledClass[right]

    actual_result = id(left_cls) == id(right_cls)

    if actual_result != expected_result:
        assert False, f'{actual_result} != {expected_result}'


if __name__ == "__main__":
    from pygim.utils.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, EntangledClass.__module__, coverage=False)
