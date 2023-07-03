# -*- coding: utf-8 -*-
import pytest

from _pygim._utils._fileutils import flatten_paths


def test_flatten_paths_on_flat_dir(filled_temp_dir):
    files = list(flatten_paths(filled_temp_dir, "*"))
    files = [d.name for d in files[1:]]

    assert files == ['AUTHORS.rst', 'readme.txt', 'readme.rst']


def test_flatten_paths_on_deep_dir(filled_temp_dir):
    t_dir_1 = filled_temp_dir / "test1"
    t_dir_2 = filled_temp_dir / "test2"

    t_dir_1.mkdir()
    t_dir_2.mkdir()

    (t_dir_1 / "test.txt").touch()
    (t_dir_2 / "test.txt").touch()

    files = list(flatten_paths(filled_temp_dir, "*"))
    files = [d.name for d in files[1:]]

    assert files == ['AUTHORS.rst', 'test1', 'readme.txt', 'readme.rst',
                     'test2', 'test.txt', 'test.txt']


if __name__ == '__main__':
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, flatten_paths.__module__, coverage=False)
