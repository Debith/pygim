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

import importlib.util
import sys

ROOT = pathlib.Path(__file__).parents[3]


@pytest.fixture()
def importer():
    def importer(relative_path):
        path = ROOT / relative_path
        spec = importlib.util.spec_from_file_location(str(relative_path).replace("/", "."), path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    return importer
