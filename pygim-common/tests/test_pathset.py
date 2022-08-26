# -*- coding: utf-8 -*-
""" Tests PathSet. """

import pytest

from pygim.kernel.pathset import PathSet


@pytest.fixture()
def filled_temp_dir(temp_dir):
    test_files = PathSet.prefixed(['first.txt', 'second.txt', 'third.txt'], prefix=temp_dir)
    assert not any(f.is_file() for f in test_files)

    [f.touch() for f in test_files]
    assert all(f.is_file() for f in test_files)

    yield test_files


def test_basics(filled_temp_dir):
    assert len(PathSet([])) == 0
    assert bool(PathSet([])) is False
    assert len(filled_temp_dir) == 3
    assert bool(filled_temp_dir) is True
    assert filled_temp_dir.clone() == filled_temp_dir


if __name__ == "__main__":
    from pygim.utils.testing import run_tests
    run_tests(__file__, PathSet.__module__, coverage=True)