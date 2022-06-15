# -*- coding: utf-8 -*-

from ast import Call
import pytest

from pygim.primitives.callable_list import Callable, Pipeline
from pygim.utils.iterable import flatten

############################################################
# PARAMETERLESS TESTS
############################################################


def test_callable_captures_callable_elegantly():
    def example():
        return "hello"

    cmd = Callable(example)
    assert cmd() == "hello"


def test_callable_can_override_its_call_method():
    class MyCallable(Callable):
        def __call__(self):
            return "overridden hello!"

    cmd = MyCallable()
    assert cmd() == "overridden hello!"


def test_creating_chained_callables():
    cmd = Callable(lambda: "hello", lambda: "chained!")
    assert cmd() == ["hello", "chained!"]


def test_nested_callables_1():
    chain = [
        lambda: "first",
        lambda: "second",
        [
            lambda: "third-a",
            lambda: "third-b",
            lambda: "third-c",
        ],
        lambda: "fourth",
    ]
    cmd = Callable(chain)

    assert cmd() == ['first', 'second', ['third-a', 'third-b', 'third-c'], 'fourth']


def test_nested_callables_1():
    chain = [[[[[lambda: "deeply nested"]]]]]
    cmd = Callable(chain)

    assert cmd() == 'deeply nested'


def module_func(): NotImplemented
class CallableClass:
    def __call__(self):
        NotImplemented

def f(): NotImplemented


@pytest.mark.parametrize("func, expected_repr", [
    [lambda: None, "Callable(<lambda>)"],
    [module_func, "Callable(module_func)"],
    [CallableClass(), "Callable(CallableClass)"],
    [(lambda: None, lambda: None), "Callable(\n    Callable(<lambda>)\n    Callable(<lambda>))"],
    [(f, f, [f, f, [f, [f]], f, f], f, f), 'Callable(\n    Callable(f)\n    Callable(f)\n    Callable(\n        Callable(f)\n        Callable(f)\n        Callable(\n            Callable(f)\n            Callable(\n                Callable(f)))\n        Callable(f)\n        Callable(f))\n    Callable(f)\n    Callable(f))'],
])
def test_reprs(func, expected_repr):
    cmd1 = Callable(func, flatten=False)
    if repr(cmd1) != expected_repr:
        assert False, f"{cmd1} != {expected_repr}"


def test_flattened_calls():
    chain = [f, f, [f, [f, f]], f]
    func2 = Pipeline(chain, flatten=True)

    a = [f() for f in func2]
    e = [c() for c in flatten(chain)]

    assert a == e


############################################################
# PARAMETER TESTS
############################################################

def pf(arg):
    return arg + arg


def test_nested_arg_call():
    chain = [pf, pf, [pf, [pf, pf]], pf]
    func = Callable(chain, flatten=False)

    result = func(21)
    assert result == [42, 42, [42, [42, 42]], 42]


def test_functions_as_a_pipe():
    chain = [pf, pf, pf]
    func = Pipeline(chain)

    result = func(1)
    assert result == 16



if __name__ == "__main__":
    pytest.main([__file__])