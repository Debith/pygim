# -*- coding: utf-8 -*-
"""pygim.errors: every exception pygim raises, made once, in C++, each a GimError and the Python
exception its contract needs (docs/design/errors.md)."""

import ast
import inspect
import pathlib
import pickle
import subprocess
import sys

import pytest

import pygim.errors
from pygim import utils
from pygim.errors import (
    ArgumentError,
    BackendError,
    CircularDependencyError,
    DispatchError,
    DuplicateKeyError,
    GimError,
    GimException,
    GimFileExistsError,
    GimFileNotFoundError,
    GimNotADirectoryError,
    GimOptionError,
    GimOSError,
    GimOverflowError,
    GimPermissionError,
    IndexOutOfRangeError,
    InvalidStateError,
    InvalidValueError,
    MissingAttributeError,
    MissingKeyError,
    ParseError,
    SignatureError,
    UnrecognizedTypeError,
    UnsupportedColumnTypeError,
)

CLASSES = (
    GimException, GimError, MissingKeyError, GimOptionError, DispatchError, UnrecognizedTypeError,
    ArgumentError, SignatureError, InvalidValueError, ParseError, InvalidStateError, DuplicateKeyError,
    CircularDependencyError, UnsupportedColumnTypeError, BackendError, GimOSError, GimFileNotFoundError,
    GimFileExistsError, GimNotADirectoryError, GimPermissionError, GimOverflowError, IndexOutOfRangeError,
    MissingAttributeError,
)


@pytest.mark.parametrize(
    "cls, bases",
    [
        (GimError, (GimException,)),
        (MissingKeyError, (GimError, KeyError)),
        (GimOptionError, (GimError, ValueError)),
        (DispatchError, (GimError, TypeError)),
        (UnrecognizedTypeError, (GimError, TypeError)),
        (ArgumentError, (GimError, TypeError)),
        (SignatureError, (GimError, TypeError)),
        (InvalidValueError, (GimError, ValueError)),
        (ParseError, (GimError, ValueError)),
        (InvalidStateError, (GimError, RuntimeError)),
        (DuplicateKeyError, (GimError, ValueError)),
        (CircularDependencyError, (GimError, ValueError)),
        (UnsupportedColumnTypeError, (GimError, TypeError)),
        (BackendError, (GimError, RuntimeError)),
        (GimOSError, (GimError, OSError)),
        (GimFileNotFoundError, (GimOSError, FileNotFoundError)),
        (GimFileExistsError, (GimOSError, FileExistsError)),
        (GimNotADirectoryError, (GimOSError, NotADirectoryError)),
        (GimPermissionError, (GimOSError, PermissionError)),
        (GimOverflowError, (GimError, OverflowError)),
        (IndexOutOfRangeError, (GimError, IndexError)),
        (MissingAttributeError, (GimError, AttributeError)),
    ],
    ids=lambda v: v.__name__ if isinstance(v, type) else "",
)
def test_each_error_is_pygims_and_the_python_exception_its_contract_needs(cls, bases):
    assert cls.__bases__ == bases and issubclass(cls, GimError)


def test_a_file_error_is_made_and_said_as_pythons_own():
    """As pathlib raises it: errno, strerror and the file, said "[Errno 2] ...: 'x'"."""
    error = GimFileNotFoundError(2, "No such file or directory", "x.toml")
    assert (error.errno, error.strerror, error.filename) == (2, "No such file or directory", "x.toml")
    assert str(error) == str(FileNotFoundError(2, "No such file or directory", "x.toml"))


def test_the_module_names_every_class_and_each_names_the_module_its_home():
    assert pygim.errors.__all__ == tuple(c.__name__ for c in CLASSES)
    assert {c.__module__ for c in CLASSES} == {"pygim.errors"}


STUB = ast.parse(pathlib.Path(pygim.errors.__file__).with_name("errors.pyi").read_text(encoding="utf-8"))
STUB_CLASSES = {node.name: node for node in STUB.body if isinstance(node, ast.ClassDef)}
STUB_ALL = next(ast.literal_eval(node.value) for node in STUB.body if isinstance(node, ast.Assign))


def test_the_stub_names_every_class_in_the_module_s_order():
    assert STUB_ALL == tuple(STUB_CLASSES) == pygim.errors.__all__


@pytest.mark.parametrize("cls", CLASSES, ids=lambda c: c.__name__)
def test_the_stub_declares_each_class_as_the_module_makes_it(cls):
    """Its bases, its docstring and the keywords its constructor takes, so an editor or a type
    checker reading the stub sees the classes pygim.errors makes."""
    node = STUB_CLASSES[cls.__name__]
    assert [ast.unparse(base) for base in node.bases] == [base.__name__ for base in cls.__bases__]
    assert ast.get_docstring(node) == inspect.cleandoc(cls.__doc__)
    inits = [n for n in node.body if isinstance(n, ast.FunctionDef) and n.name == "__init__"]
    assert len(inits) == ("__init__" in vars(cls))
    for init in inits:
        assert isinstance(cls(**{parameter.arg: () for parameter in init.args.args[1:]}), cls)


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


@pytest.mark.parametrize(
    "args, said",
    [
        ((5, "a mapping"), "Expected a mapping, got `5 [int]`"),
        ((5, dict, "gimdict's initial"), "gimdict's initial: Expected to get type `dict`, got `5 [int]`"),
        ((5, "a callable", "provider"), "provider: Expected a callable, got `5 [int]`"),
    ],
    ids=["what was expected, said in words", "a subject first", "both"],
)
def test_an_unrecognized_type_may_say_what_it_expected_in_words_and_whose_it_was(args, said):
    """Not every expectation is a type (a callable, an iterable class), and the subject says which
    argument or member was given the value."""
    error = UnrecognizedTypeError(*args)
    assert (str(error), error.args) == (said, args)


def test_a_missing_key_is_said_as_a_key_error_says_it():
    assert str(MissingKeyError("x")) == str(KeyError("x")) == "'x'"


THIRTEEN_KEYS = {"host": 1, **{f"port{i}": i for i in range(12)}}


@pytest.mark.parametrize(
    "key, within, said",
    [
        ("hots", {"host": 1, "port": 2}, "Key ``hots`` not among: host, port"),
        ("hots", (), "Key ``hots`` not found"),
        ("hostt", THIRTEEN_KEYS, "Key ``hostt`` not among 13 keys; closest: host"),
        ("port1x", THIRTEEN_KEYS, "Key ``port1x`` not among 13 keys; closest: port1, port10, port11"),
        ("hots", THIRTEEN_KEYS, "Key ``hots`` not among 13 keys"),
    ],
    ids=["listed", "nothing to look in", "one close", "three close", "none close as Python judges"],
)
def test_a_missing_key_says_the_keys_it_was_not_among(key, within, said):
    """Up to ten keys are listed; past that, how many, and the closest by Python's own "Did you mean"
    measure (it judges "hots" too far from "host")."""
    error = MissingKeyError(key, within)
    assert (str(error), error.args, error.within) == (said, (key,), within)


def test_a_missing_key_reads_the_keys_only_when_said():
    keys = {"host": 1}
    error = MissingKeyError("port", keys)
    keys["port"] = 2
    assert str(error) == "Key ``port`` not among: host, port"


def test_a_missing_key_whose_keys_cannot_be_read_names_where_it_looked():
    class Unreadable:
        def __iter__(self):
            raise RuntimeError("no")

    assert str(MissingKeyError("port", Unreadable())) == "Key ``port`` not found in `Unreadable`"


def test_a_pickled_missing_key_keeps_its_key_and_drops_where_it_looked():
    """As a pickled AttributeError drops `obj`: the mapping may be large, or not picklable."""
    again = pickle.loads(pickle.dumps(MissingKeyError("port", {"host": 1})))
    assert (type(again), again.args, again.within, str(again)) == (MissingKeyError, ("port",), None, "'port'")


def test_gimdict_says_a_missing_key_as_pygims_error_with_where_it_looked():
    d = utils.gimdict({"host": 1, "port": 2})
    for miss in (lambda: d["hots"], lambda: d.__delitem__("hots")):
        with pytest.raises(MissingKeyError) as raised:
            miss()
        assert isinstance(raised.value, KeyError)
        assert (str(raised.value), raised.value.args, raised.value.within) == (
            "Key ``hots`` not among: host, port", ("hots",), d)


def test_a_core_error_reaches_python_as_its_pygim_class():
    """core_utils.h has no Python: it throws pygim::errors::option_error, and the translator that
    errors::load() registered raises GimOptionError, still a ValueError, from it."""
    with pytest.raises(GimOptionError) as raised:
        utils.calculate_rate(1, "XB", 1, "s")
    assert isinstance(raised.value, ValueError)
    assert raised.value.value == "XB" and "kb" in raised.value.options
    assert str(raised.value) == "Given input ``XB`` not among: " + ", ".join(raised.value.options)


@pytest.mark.parametrize(
    "error",
    [GimError("failed"), MissingKeyError("x"), GimOptionError("v", ["a", "b"]), UnrecognizedTypeError(2, str),
     UnrecognizedTypeError(5, "a callable", "provider"), GimFileNotFoundError(2, "No such file or directory", "x"),
     *(cls("said") for cls in (ArgumentError, SignatureError, InvalidValueError, ParseError, InvalidStateError,
                               DuplicateKeyError, CircularDependencyError, UnsupportedColumnTypeError, BackendError,
                               GimOverflowError, IndexOutOfRangeError, MissingAttributeError))],
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
