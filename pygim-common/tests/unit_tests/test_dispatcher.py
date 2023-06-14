# -*- coding: utf-8 -*-
import pytest

from pygim.performance.dispatch import dispatch


def test_dispatcher_without_default_function():
    do_something = dispatch(NotImplemented)

    @do_something.register(int)
    def do_something_with_int(integer):
        return integer

    assert do_something(1) == 1
    try:
        do_something(None)
    except NotImplementedError:
        pass


if __name__ == '__main__':
    from pygim.testing import run_tests
    run_tests(__file__, dispatch.__module__)
