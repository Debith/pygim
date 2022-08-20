import pytest
import tempfile
import pathlib


@pytest.fixture
def temp_dir():
    tdir = tempfile.TemporaryDirectory()

    yield pathlib.Path(tdir.name)

    tdir.cleanup()