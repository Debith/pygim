# -*- coding: utf-8 -*-
"""Tests PathSet."""

import pytest

from pygim.pathset import PathSet


@pytest.fixture()
def temp_files(temp_dir):
    test_files = [
        temp_dir / "readme.txt",
        temp_dir / "readme.rst",
        temp_dir / "AUTHORS.rst",
    ]

    assert not any(f.is_file() for f in test_files)

    [f.touch() for f in test_files]
    assert all(f.is_file() for f in test_files)

    yield test_files


def test_basics(temp_files):
    assert len(PathSet([])) == 0
    assert bool(PathSet([])) is False
    assert len(PathSet(temp_files)) == 3
    assert bool(PathSet(temp_files)) is True


def test_equality(temp_files):
    _temp_files = PathSet(temp_files)
    assert _temp_files == _temp_files
    assert _temp_files == PathSet(temp_files)
    assert _temp_files != PathSet([])


def test_substraction(temp_dir, temp_files):
    temp_files = PathSet(temp_files)
    temp_files -= str(temp_dir / "readme.txt")
    assert len(temp_files) == 2


def test_cloning(temp_files):
    temp_files = PathSet(temp_files)
    cloned = temp_files.clone()
    assert cloned == temp_files
    assert id(temp_files) != id(cloned)


def test_modification_after_cloning(temp_dir, temp_files):
    temp_files = PathSet(temp_files)
    cloned = temp_files.clone()
    temp_files -= str(temp_dir / "readme.txt")
    assert len(temp_files) == 2
    assert len(cloned) == 3


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, PathSet.__module__, coverage=False)
