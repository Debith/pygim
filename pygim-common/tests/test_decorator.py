# -*- coding: utf-8 -*-
# type: ignore
""" Test utility functions. """

import pytest

from pygim.kernel.magic.decorator import Decorator

class MyDecorator(Decorator):
    def decorate(self, func, *args, **kwargs):
        bonus = self.kwargs.get('bonus', 1)

        if self.args:
            bonus = self.args[0]
        return func(*args, **kwargs) + bonus


def test_decorator_can_be_used_as_one():
    @MyDecorator
    def function():
        return 1

    assert function() == 2


def test_decorator_can_be_initialized_without_arguments():
    @MyDecorator()
    def function():
        return 1

    assert function() == 2


def test_decorator_can_be_initialized_with_positional_arguments():
    @MyDecorator(4)
    def function():
        return 1

    assert function() == 5


def test_decorator_can_be_initialized_with_keyword_arguments():
    @MyDecorator(bonus=3)
    def function():
        return 1

    assert function() == 4



if __name__ == "__main__":
    pytest.main([__file__])