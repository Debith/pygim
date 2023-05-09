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


def to_module(relative_path):
    relative_path = pathlib.Path(relative_path).with_suffix("")
    if relative_path.name == "__init__":
        relative_path = relative_path.parent
    module_path = str(relative_path).replace("/", ".")
    if '-' in module_path[:10]:
        module_path = module_path.split('.', 1)[-1]
    return module_path

@pytest.fixture()
def importer():
    def importer(relative_path, *, execute=True, store=False):
        path = ROOT / relative_path
        spec = importlib.util.spec_from_file_location(to_module(relative_path), path)
        module = importlib.util.module_from_spec(spec)

        if store:
            sys.modules[module.__name__] = module

        if execute:
            spec.loader.exec_module(module)

        return module
    return importer
