import pytest
import tempfile
import pathlib


@pytest.fixture
def temp_dir():
    tdir = tempfile.TemporaryDirectory()
    __tdir = pathlib.Path(tdir.name)

    yield __tdir

    assert __tdir.exists(), "DO NOT DELETE TEMP DIR!"

    tdir.cleanup()


@pytest.fixture()
def filled_temp_dir(temp_dir):
    _FILES = ['readme.txt', 'readme.rst', 'AUTHORS.rst']
    test_files = [temp_dir / f for f in _FILES]
    assert not any(f.is_file() for f in test_files)

    [f.touch() for f in test_files]
    assert all(f.is_file() for f in test_files)

    yield temp_dir
