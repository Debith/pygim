# -*- coding: utf-8 -*-
""" Tests PathSet. """


from pathlib import Path
import pytest

from pygim.pathset import PathSet


@pytest.fixture()
def temp_files(temp_dir):
    test_files = [
        temp_dir / 'readme.txt',
        temp_dir / 'readme.rst',
        temp_dir / 'AUTHORS.rst',
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


def test_cloning(temp_files):
    temp_files = PathSet(temp_files)
    assert temp_files.clone() == temp_files


def test_adding(temp_files):
    temp_dir = list(temp_files)[0].parent
    more_files = PathSet.prefixed(['fourth.txt', 'fifth.txt', 'sixth.txt'], prefix=temp_dir)

    new_paths = temp_files + more_files

    assert new_paths != temp_files
    assert new_paths != more_files
    assert new_paths == PathSet.prefixed([
        'readme.txt', 'readme.rst', 'AUTHORS.rst', 'fourth.txt', 'fifth.txt', 'sixth.txt'], prefix=temp_dir)


def test_delete_all(temp_files):
    assert len(temp_files) == 3
    assert [f.is_file() for f in temp_files] == [True, True, True]

    temp_files.FS.delete_all()

    assert len(temp_files) == 3
    assert [f.is_file() for f in temp_files] == [False, False, False]


def test_delete_all_with_folders(temp_dir):
    sub_dir = temp_dir / "sub"
    sub_dir.mkdir()
    test_files = PathSet.prefixed(['readme.txt', 'readme.rst', 'AUTHORS.rst'], prefix=temp_dir)
    sub_files = PathSet.prefixed(['readme.txt', 'readme.rst', 'AUTHORS.rst'], prefix=sub_dir)

    [p.touch() for p in test_files + sub_files]

    paths = PathSet(temp_dir).dropped(name=temp_dir.name)
    assert len(paths.dirs()) == 1
    assert len(paths.files()) == 6
    assert len(paths) == 7

    paths.FS.delete_all()

    assert not any([p.exists() for p in paths])


def test_current_working_dir(temp_dir):
    import os
    old_cwd = os.curdir
    os.chdir(temp_dir)

    _cur_dir = Path(os.curdir).resolve()
    assert list(PathSet())[0].name == list(PathSet(_cur_dir))[0].name

    actual_path = list(PathSet.prefixed(['readme.txt']))[0]
    expected_path = list(PathSet.prefixed(['readme.txt'], prefix=_cur_dir))[0]

    assert actual_path.parts[-2:] == expected_path.parts[-2:]

    os.chdir(old_cwd)


def test_basic_filters(temp_files):
    temp_dir = list(temp_files)[0].parent
    assert temp_files.by_suffix('.txt') == PathSet.prefixed(['readme.txt'], prefix=temp_dir)
    assert temp_files.dirs() == PathSet.prefixed([], prefix=temp_dir)
    assert temp_files.files() == PathSet.prefixed(['readme.txt', 'readme.rst', 'AUTHORS.rst'], prefix=temp_dir)


def test_drop_files_based_on_filter(temp_files):
    temp_dir = list(temp_files)[0].parent
    new_paths = temp_files.drop(suffix='.rst')

    assert temp_files != new_paths
    assert list(new_paths) == list(PathSet.prefixed(['readme.txt'], prefix=temp_dir))

    new_paths = temp_files.drop(suffix=('.rst', '.txt'))

    assert temp_files != new_paths
    assert list(new_paths) == []


def test_dropped_files_based_on_filter(temp_files):
    temp_dir = list(temp_files)[0].parent
    new_paths = temp_files.dropped(suffix='.rst')

    assert temp_files != new_paths
    assert new_paths == PathSet.prefixed(['readme.txt'], prefix=temp_dir)

    new_paths = temp_files.dropped(suffix=('.rst', '.txt'))

    assert temp_files != new_paths
    assert new_paths == PathSet([])


if __name__ == "__main__":
    from pygim.core.testing import run_tests
    run_tests(__file__, PathSet.__module__, coverage=False)