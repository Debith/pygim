# -*- coding: utf-8 -*-
import pytest

from _pygim._magic._routine import Routine

def _function(): pass
def _function_with_nested():
    def nested_function():
        pass
    return nested_function
_nested = _function_with_nested()
def _function_with_deep_nested_functions():
    def nested1():
        def nested2():
            def nested3(): pass
            return nested3
        return nested2
    return nested1
_deep_nested = _function_with_deep_nested_functions()


class _Example:
    @staticmethod
    def static_method():
        pass

    @classmethod
    def class_method(cls):
        pass

    def method(self):
        pass

def _function_with_inner_class():
    class _Example:
        @staticmethod
        def static_method():
            pass

        @classmethod
        def class_method(cls):
            pass

        def method(self):
            pass
    return _Example
_inner_Example = _function_with_inner_class()


@pytest.mark.parametrize('function, expected_result', [
    (lambda: None, False),
    (_function, False),
    (_nested, False),
    (_deep_nested, False),
    (_Example.static_method, True),
    (_Example.class_method, True),
    (_Example.method, True),
    (_Example().static_method, True),
    (_Example().class_method, True),
    (_Example().method, True),
    (_inner_Example.static_method, True),
    (_inner_Example.class_method, True),
    (_inner_Example.method, True),
    (_inner_Example().static_method, True),
    (_inner_Example().class_method, True),
    (_inner_Example().method, True),
])
def test_routine_has_parent_class(function, expected_result):
    routine = Routine(function)
    if routine.has_parent_class() != expected_result:
        assert False, f"got {routine.has_parent_class()} while expecting {expected_result}"


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, Routine.__module__, coverage=False)
