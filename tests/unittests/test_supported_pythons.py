# -*- coding: utf-8 -*-
"""The Pythons pygim supports, said once: what pip is told, what the classifiers claim, what CI
tests and what a release builds wheels for are one list. Each is written in a file of its own, so
one could drift from the others — a classifier claiming a Python no job tests, a wheel built for
one pip would refuse."""

import re

import pytest

from pygim import path

# Debith, 2026-10-31: "We can drop 3.9 support" (end of life since October 2025).
SUPPORTED = ["3.10", "3.11", "3.12", "3.13", "3.14"]

REPO = path(__file__).resolve().parent.parent.parent


def read(relative):
    file = REPO / relative
    if not file.is_file():
        pytest.skip(f"{relative} is not here: the repository's files are not")
    return file.read()


def test_pip_is_told_the_oldest_python():
    assert read("pyproject.toml")["project"]["requires-python"] == f">={SUPPORTED[0]}"


def test_the_classifiers_claim_each_supported_python():
    classifiers = read("pyproject.toml")["project"]["classifiers"]
    claimed = [c.rsplit(" :: ", 1)[1] for c in classifiers if re.fullmatch(r"Programming Language :: Python :: 3\.\d+", c)]
    assert claimed == SUPPORTED


def test_ci_tests_each_supported_python_on_each_system():
    matrix = read(".github/workflows/python-packages.yml")["jobs"]["build"]["strategy"]["matrix"]
    assert matrix["python-version"] == SUPPORTED
    assert "include" not in matrix and "exclude" not in matrix   # every system, every Python


def test_a_release_builds_a_wheel_for_each_supported_python():
    matrix = read(".github/workflows/release.yml")["jobs"]["wheels"]["strategy"]["matrix"]
    assert matrix["python"] == ["cp" + version.replace(".", "") for version in SUPPORTED]
