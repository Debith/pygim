# -*- coding: utf-8 -*-
"""src/pygim/pathlike.pyi is what an editor and a type checker see of pygim.pathlike: every class and
function, each public member with the binding's parameters and docstring. help() shows the binding's
text and an editor the stub's, so they are one text. Until 2026-11-01 nothing held them together, and
42 docstrings and 8 signatures had drifted apart."""

import ast

import pytest

from pygim import pathlike
from stub_parity import Stub

STUB = Stub(pathlike)
CLASSES = ("path", "PathSet", "Filter", "Query", "FileContent", "PathStore", "jsonpath", "jsonlpath", "tomlpath", "yamlpath")
# What pybind11 gives every class, or what the stub leaves to object's.
NOT_STUBBED = {"__new__", "__repr__", "__module__", "__doc__", "__dict__", "__weakref__", "_pybind11_conduit_v1_"}


def members(cls):
    """A class's public members as bound, and __init__ where the stub declares one (a class with no
    constructor of its own gets pybind11's, which refuses)."""
    return [n for n in vars(getattr(pathlike, cls)) if n not in NOT_STUBBED
            and (not n.startswith("_") or n == "__init__" and "__init__" in STUB.methods(cls))]


def bound(cls, name):
    """What the class holds under *name*: a property as itself, anything else as an attribute (a
    static method's function, not its staticmethod wrapper)."""
    held = vars(getattr(pathlike, cls))[name]
    return held if isinstance(held, property) else getattr(getattr(pathlike, cls), name)


FUNCTIONS = sorted(n for n, v in vars(pathlike).items() if not n.startswith("_") and callable(v)
                   and type(v).__name__ == "builtin_function_or_method")


def test_it_names_every_class_and_function_of_the_module():
    assert {n for n in vars(pathlike) if not n.startswith("_")} - STUB.names() == set()


@pytest.mark.parametrize("cls", CLASSES)
def test_every_public_member_is_stubbed(cls):
    assert sorted(set(members(cls)) - set(STUB.methods(cls))) == []


@pytest.mark.parametrize("cls, name", [(c, n) for c in CLASSES for n in members(c)])
def test_each_member_takes_the_bindings_parameters_and_docstring(cls, name):
    stubbed, held = STUB.methods(cls)[name], bound(cls, name)
    if not isinstance(held, property):
        is_static = isinstance(vars(getattr(pathlike, cls))[name], staticmethod)
        assert Stub.params(stubbed, method=not is_static) == Stub.binding_params(held)
    assert Stub.same_text(ast.get_docstring(stubbed), Stub.binding_doc(held)), Stub.binding_doc(held)


@pytest.mark.parametrize("name", FUNCTIONS)
def test_each_function_takes_the_bindings_parameters_and_docstring(name):
    stubbed, held = STUB.function(name), getattr(pathlike, name)
    assert Stub.params(stubbed, method=False) == Stub.binding_params(held)
    assert Stub.same_text(ast.get_docstring(stubbed), Stub.binding_doc(held)), Stub.binding_doc(held)


@pytest.mark.parametrize("cls", CLASSES)
def test_each_class_docstring_is_the_bindings(cls):
    assert Stub.same_text(ast.get_docstring(STUB.cls(cls)), getattr(pathlike, cls).__doc__)
