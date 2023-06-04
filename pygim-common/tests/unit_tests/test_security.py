# -*- coding: utf-8 -*-
#type: ignore
""" Test security functions. """
import pytest
from pygim.security import sha256sum


@pytest.mark.parametrize('input,expected_result', [
    ("test string", "d5579c46dfcc7f18207013e65b44e4cb4e2c2298f4ac457ba8f82743f31e930b"),
])
def test_shasum(input, expected_result):
    actual_result = sha256sum(input)

    if actual_result != expected_result:
        assert False, f"{actual_result} != {expected_result}"


if __name__ == '__main__':
    from pygim.testing import run_tests
    run_tests(__file__, sha256sum.__module__)