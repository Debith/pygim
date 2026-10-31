# -*- coding: utf-8 -*-
"""pygim.core.explib: pygim's exception classes and message helpers, as raised and as read."""

import pathlib
import pickle

import pytest

from pygim.core import explib
from pygim.core.explib import (
    DispatchError,
    GimError,
    GimException,
    GimOptionError,
    ShaSumTargetNotFoundError,
    UnrecognizedTypeError,
    file_error_msg,
    type_error_msg,
)


def test_an_error_without_a_message_says_its_default():
    """Found 2026-10-31: an assert refused the default, None, so GimException() raised
    AssertionError (and, under python -O, did not)."""
    assert str(GimException()) == "Gim Error."
    assert str(DispatchError()) == "Dispatch error."


def test_a_message_is_its_text_and_parts_are_joined():
    assert str(GimError("broken")) == "broken"
    assert str(GimError(["one", "two"])) == "one\ntwo"
    assert str(GimError(("one", 2), sep=", ")) == "one, 2"


def test_a_message_of_another_type_is_refused():
    with pytest.raises(TypeError, match=r"Expected to get type `str, list, tuple`, got `3 \[int\]`"):
        GimError(3)


def test_every_error_is_a_gim_exception_and_an_exception():
    for name in explib.__all__:
        value = getattr(explib, name)
        if isinstance(value, type):
            assert issubclass(value, GimException) and issubclass(value, Exception), name


def test_a_missing_sha_target_is_a_file_not_found_error():
    assert issubclass(ShaSumTargetNotFoundError, FileNotFoundError)


@pytest.mark.parametrize("error", [
    GimException(), GimError("x"), GimError(["a", "b"], sep="; "), DispatchError(),
    GimOptionError(1, [2, 3]), UnrecognizedTypeError(int, (str, None)),
], ids=lambda e: type(e).__name__)
def test_an_error_survives_pickling(error):
    """What a worker process raises reaches its parent as itself."""
    back = pickle.loads(pickle.dumps(error))
    assert type(back) is type(error)
    assert str(back) == str(error)


def test_an_option_error_names_the_value_and_the_choices():
    assert str(GimOptionError("c", ["a", "b"])) == "Given input ``c`` not among: a, b"


def test_an_unrecognized_type_names_both_types():
    assert str(UnrecognizedTypeError(int, (str, None))) == "Expected to get type `str, None`, got `int`"


def test_type_error_msg_names_a_value_and_its_type():
    assert type_error_msg(2, str) == "Expected to get type `str`, got `2 [int]`"
    assert type_error_msg([], (tuple, list)) == "Expected to get type `tuple, list`, got `[] [list]`"


def test_file_error_msg_names_the_absolute_file(tmp_path):
    assert file_error_msg(tmp_path / "x.txt") == f"Expected filename `{(tmp_path / 'x.txt').resolve()}` not found!"
    assert file_error_msg(str(tmp_path / "x.txt"), "unreadable") == (
        f"Expected filename `{(tmp_path / 'x.txt').resolve()}` unreadable!")


def test_file_error_msg_resolves_a_relative_name(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    assert file_error_msg(pathlib.Path("y.txt")) == f"Expected filename `{tmp_path.resolve() / 'y.txt'}` not found!"
