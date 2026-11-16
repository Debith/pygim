# -*- coding: utf-8 -*-
"""pygim.errors: every exception pygim raises, made once, in C++, each a GimError and the Python
exception its contract needs (docs/design/exception_boundaries.md)."""

import pickle
import subprocess
import sys

import pytest

import pygim.errors
from pygim import utils
from pygim.errors import (
    DispatchError,
    GimError,
    GimException,
    GimOptionError,
    MissingKeyError,
    UnrecognizedTypeError,
)

CLASSES = (GimException, GimError, MissingKeyError, GimOptionError, DispatchError, UnrecognizedTypeError)


@pytest.mark.parametrize(
    "cls, bases",
    [
        (GimError, (GimException,)),
        (MissingKeyError, (GimError, KeyError)),
        (GimOptionError, (GimError, ValueError)),
        (DispatchError, (GimError,)),
        (UnrecognizedTypeError, (DispatchError,)),
    ],
    ids=lambda v: v.__name__ if isinstance(v, type) else "",
)
def test_each_error_is_pygims_and_the_python_exception_its_contract_needs(cls, bases):
    assert cls.__bases__ == bases and issubclass(cls, GimError)


def test_the_module_names_every_class_and_each_names_the_module_its_home():
    assert pygim.errors.__all__ == tuple(c.__name__ for c in CLASSES)
    assert {c.__module__ for c in CLASSES} == {"pygim.errors"}


def test_an_option_error_says_the_value_and_its_options():
    error = GimOptionError("scoped", ["transient", "singleton"])
    assert str(error) == "Given input ``scoped`` not among: transient, singleton"
    assert (error.value, error.options) == ("scoped", ("transient", "singleton"))
    assert error.args == ("scoped", ("transient", "singleton"))


def test_an_option_error_takes_its_values_by_name_and_its_options_from_any_iterable():
    error = GimOptionError(value=3, options=range(2))
    assert (error.value, error.options, str(error)) == (3, (0, 1), "Given input ``3`` not among: 0, 1")


@pytest.mark.parametrize(
    "given, expected, said",
    [
        (2, str, "Expected to get type `str`, got `2 [int]`"),
        ([], (tuple, list), "Expected to get type `tuple, list`, got `[] [list]`"),
        (int, (str, None), "Expected to get type `str, None`, got `int`"),
    ],
)
def test_an_unrecognized_type_says_what_was_given_and_what_was_expected(given, expected, said):
    error = UnrecognizedTypeError(given, expected)
    assert (str(error), error.args) == (said, (given, expected))


def test_a_missing_key_is_said_as_a_key_error_says_it():
    assert str(MissingKeyError("x")) == str(KeyError("x")) == "'x'"


@pytest.mark.parametrize(
    "error",
    [GimError("failed"), MissingKeyError("x"), GimOptionError("v", ["a", "b"]), UnrecognizedTypeError(2, str)],
    ids=lambda e: type(e).__name__,
)
def test_an_error_is_the_same_error_after_pickling(error):
    again = pickle.loads(pickle.dumps(error))
    assert (type(again), again.args, str(again)) == (type(error), error.args, str(error))


def test_an_extension_that_raises_pygims_errors_imports_on_its_own():
    """In a fresh interpreter, where nothing has imported pygim.errors before the extension's load
    takes its table: here it always has been."""
    imported = subprocess.run([sys.executable, "-c", "import pygim.utils, sys; print('pygim.errors' in sys.modules)"],
                              capture_output=True, text=True)
    assert (imported.returncode, imported.stdout.strip(), imported.stderr) == (0, "True", "")


@pytest.mark.parametrize("strategy", ["", "AVG"])
def test_an_extension_raises_the_class_pygim_errors_made(strategy):
    with pytest.raises(GimOptionError) as raised:
        utils.gimdict().set_default_strategy(strategy)
    assert (raised.value.value, raised.value.options) == (strategy, ("sum", "max", "min", "replace"))
