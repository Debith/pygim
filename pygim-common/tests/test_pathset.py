# -*- coding: utf-8 -*-
""" Tests PathSet. """

import pytest

from pygim.kernel.pathset import PathSet

@pytest.fixture()
def filled_temp_dir(temp_dir):
    test_files = PathSet.prefixed(['first.txt', 'second.txt', 'third.txt'], prefix=temp_dir)
    assert not any(test_files.each.is_file())

    test_files.each.touch()
    assert all(test_files.each.is_file())

    yield test_files


def test_files(filled_temp_dir):
    print(filled_temp_dir)





if __name__ == "__main__":
    pytest.main([__file__])