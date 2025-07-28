import sys
import pytest
import tempfile
import pathlib

ROOT = pathlib.Path(__file__).parents[3]


@pytest.fixture
def temp_dir():
    tdir = tempfile.TemporaryDirectory()
    __tdir = pathlib.Path(tdir.name)

    yield __tdir

    assert __tdir.exists(), "DO NOT DELETE TEMP DIR!"

    tdir.cleanup()