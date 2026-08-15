"""pathlike read(into=callable) — the factory contract.

pathlike stays ignorant of what the caller builds: into= is any callable
handed the decoded object; its return value becomes read()'s. Preconfigured
variants are functools.partial over the gimdict factory.
"""

from __future__ import annotations

import functools

import pytest

import pygim
from pygim import utils


@pytest.fixture()
def yaml_file(tmp_path):
    f = pygim.path(tmp_path / "sheet.yaml")
    f.write({"hp": 10, "speed": 30})
    return f


def test_into_defaults_off(yaml_file):
    assert yaml_file.read() == {"hp": 10, "speed": 30}


def test_into_any_callable(yaml_file):
    result = yaml_file.read(into=sorted)
    assert result == ["hp", "speed"]           # sorted(dict) -> sorted keys


def test_into_gimdict(yaml_file):
    d = yaml_file.read(into=utils.gimdict)
    assert isinstance(d, utils.gimmap)
    assert d["hp"] == 10


def test_into_preconfigured_layered(yaml_file):
    make = functools.partial(utils.gimdict, layers=True, int="sum")
    sheet = yaml_file.read(into=make)
    sheet.apply("race", "hp", 2)
    assert sheet["hp"] == 12
    sheet.remove("race")
    assert sheet["hp"] == 10


def test_into_must_be_callable(yaml_file):
    with pytest.raises(TypeError):
        yaml_file.read(into="gimdict")
