# -*- coding: utf-8 -*-
import pytest

from pygim.performance import dispatch

@pytest.mark.parametrize("args, expected", [
    ((1, 2), 3),
    ((1.0, 2.0), -1.0),
    (("a", "b"), "ab!"),
])
def test_dispatcher(args, expected):
    @dispatch
    def target_func(first, second):
        raise NotImplementedError()

    @target_func.register(int, int)
    def _(first: int, second: int):
        return first + second

    @target_func.register(float, float)
    def _(first: float, second: float):
        return first - second

    @target_func.register(str, str)
    def _(first: str, second: str):
        return first + second + "!"

    assert target_func(*args) == expected


def test_function_with_star_args():
    @dispatch
    def target_func(*args):
        raise NotImplementedError()

    @target_func.register(int, int)
    def _(first: int, second: int):
        return first + second


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, dispatch.__module__, coverage=False)
