# -*- coding: utf-8 -*-
"""pygim's naming rules, src/_pygim_fast/utils/python_names.h, held to Python's own.

Every build proves the rules at compile time: tests/static/python_names_proofs.cpp, and
tests/static/python_names_callspace_proofs.cpp, rendered from the model in python_names_callspace.py
with Python's answers. These check that the rendered proof is still the model's, and that the rule
Python can call, pygim.utils.is_dunder, answers as Python's own rule, enum._is_dunder, does
(global #113) over the model's whole name domain.
"""

import enum

import pytest

import python_names_callspace
from pygim import utils


def test_the_every_build_proof_is_the_model_s():
    """tests/static/python_names_callspace_proofs.cpp is rendered from python_names_callspace."""
    assert python_names_callspace.PROOF.read_text(encoding="utf-8") == python_names_callspace.render_proof(), (
        "stale: run python tests/unittests/python_names_callspace.py")


@pytest.mark.parametrize("name", python_names_callspace.NAMES.expand())
def test_is_dunder_answers_as_python_does(name):
    assert utils.is_dunder(name) == enum._is_dunder(name)


@pytest.mark.parametrize("value", [None, 1])
def test_is_dunder_refuses_what_is_not_text(value):
    with pytest.raises(TypeError):
        utils.is_dunder(value)
