import pytest
import tempfile
import pathlib


@pytest.fixture
def temp_dir():
    tdir = tempfile.TemporaryDirectory()
    __tdir = pathlib.Path(tdir.name)

    yield __tdir

    __tdir.mkdir(exist_ok=True, parents=True)  # If tests removed, recreate.
    tdir.cleanup()

    assert not __tdir.exists()