import pytest
import tempfile
import pathlib


@pytest.fixture
def temp_dir():
    tdir = tempfile.TemporaryDirectory()
    __tdir = pathlib.Path(tdir.name)

    yield __tdir

    assert __tdir.exists(), "DO NOT DELETE TEMP DIR!"

    try:
        tdir.cleanup()
    except PermissionError:
        pass  # FIXME: This happens in CI. Why this happens with temporary folders?