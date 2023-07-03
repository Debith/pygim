# -*- coding: utf-8 -*-
""" Tests PathSet. """

import platform
from pathlib import Path
import pytest

from pygim.fileio import PathSet
from pygim.fileio.pathset import flatten_paths

# FIXME: TemporaryDirectory().cleanup() fails due to some weird
#        PermissionError in Windows environment in GitHub.
#        Therefore, Windows is relieved from testing duty for now.
if platform.uname().system != "Windows":
    _FILES = ['readme.txt', 'readme.rst', 'AUTHORS.rst']

    @pytest.fixture()
    def filled_temp_dir(temp_dir):
        test_files = PathSet.prefixed(_FILES, prefix=temp_dir)
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
        assert id(filled_temp_dir.clone()) != id(filled_temp_dir)


    def test_cloning_with_override(filled_temp_dir):
        assert filled_temp_dir.clone([]) == PathSet([])
        assert filled_temp_dir.clone(['readme.txt']) == PathSet(['readme.txt'])



    def test_adding(filled_temp_dir):
        temp_dir = list(filled_temp_dir)[0].parent
        more_files = PathSet.prefixed(['fourth.txt', 'fifth.txt', 'sixth.txt'], prefix=temp_dir)

        new_paths = filled_temp_dir + more_files

        assert new_paths != filled_temp_dir
        assert new_paths != more_files
        assert new_paths == PathSet.prefixed([
            'readme.txt', 'readme.rst', 'AUTHORS.rst', 'fourth.txt', 'fifth.txt', 'sixth.txt'], prefix=temp_dir)


    def test_delete_all(filled_temp_dir):
        assert len(filled_temp_dir) == 3
        assert [f.is_file() for f in filled_temp_dir] == [True, True, True]

        filled_temp_dir.FS.delete_all()

        assert len(filled_temp_dir) == 3
        assert [f.is_file() for f in filled_temp_dir] == [False, False, False]


    def test_delete_all_with_folders(temp_dir):
        sub_dir = temp_dir / "sub"
        sub_dir.mkdir()
        test_files = PathSet.prefixed(_FILES, prefix=temp_dir)
        sub_files = PathSet.prefixed(_FILES, prefix=sub_dir)

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


    def test_basic_filters(filled_temp_dir):
        temp_dir = list(filled_temp_dir)[0].parent
        assert filled_temp_dir.by_suffix('.txt') == PathSet.prefixed(['readme.txt'], prefix=temp_dir)
        assert filled_temp_dir.dirs() == PathSet.prefixed([], prefix=temp_dir)
        assert filled_temp_dir.files() == PathSet.prefixed(_FILES, prefix=temp_dir)


    def test_drop_files_based_on_filter(filled_temp_dir):
        temp_dir = list(filled_temp_dir)[0].parent
        new_paths = filled_temp_dir.drop(suffix='.rst')

        assert filled_temp_dir != new_paths
        assert list(new_paths) == list(PathSet.prefixed(['readme.txt'], prefix=temp_dir))

        new_paths = filled_temp_dir.drop(suffix=('.rst', '.txt'))

        assert filled_temp_dir != new_paths
        assert list(new_paths) == []


    def test_dropped_files_based_on_filter(filled_temp_dir):
        temp_dir = list(filled_temp_dir)[0].parent
        new_paths = filled_temp_dir.dropped(suffix='.rst')

        assert filled_temp_dir != new_paths
        assert new_paths == PathSet.prefixed(['readme.txt'], prefix=temp_dir)

        new_paths = filled_temp_dir.dropped(suffix=('.rst', '.txt'))

        assert filled_temp_dir != new_paths
        assert new_paths == PathSet([])


def test_flatten_paths(temp_dir):
    pass


if __name__ == "__main__":
    from pygim.testing import run_tests
    run_tests(__file__, PathSet.__module__, coverage=False)