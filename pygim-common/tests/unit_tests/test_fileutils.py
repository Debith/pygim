# -*- coding: utf-8 -*-
import pytest

from _pygim._utils._fileutils import flatten_paths
from pygim.testing import diff


def test_flatten_paths_on_flat_dir(filled_temp_dir):
    files = list(flatten_paths(filled_temp_dir, pattern="*"))
    files = sorted([d.name for d in files[1:]])

    assert files == ['AUTHORS.rst', 'readme.rst', 'readme.txt']


def test_flatten_paths_on_deep_dir(filled_temp_dir):
    t_dir_1 = filled_temp_dir / "test1"
    t_dir_2 = filled_temp_dir / "test2"

    t_dir_1.mkdir()
    t_dir_2.mkdir()

    (t_dir_1 / "test.txt").touch()
    (t_dir_2 / "test.txt").touch()

    files = list(flatten_paths(filled_temp_dir, pattern="*"))
    files = sorted([d.name for d in files[1:]])

    expected = ['AUTHORS.rst', 'readme.rst', 'readme.txt',
                'test.txt', 'test.txt', 'test1', 'test2',
                ]

    assert files == expected, diff(files, expected, start="\n")


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, flatten_paths.__module__, coverage=False)
