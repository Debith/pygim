# -*- coding: utf-8 -*-
"""Seeing what a project already has, against what its code reaches for.

The join is the whole point. Neither list is interesting alone — a package list is inert, an import
list is ordinary — and the gap between them is invisible in every individual file, which is why it
survived two careful reviews of the same file on 2026-09-23 (global memory #20).

`survey` is a pure function, so most of this is plain values with no machine arranged around them.
The gathering half gets a real directory, because that is the half whose mistakes are about paths.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

from _pygim import _inventory
from _pygim._inventory import Use, survey

OO = Path(sys.executable).parent / "oo"

STDLIB = frozenset({"pathlib", "json", "os"})
INSTALLED = {"click": "click", "pytest": "pytest", "numpy": "numpy"}


class TestTheJudgement:
    """No files, no environment, no machine — the part that decides, decided over plain values."""

    def test_a_shipped_module_nothing_imports_is_the_finding(self):
        found = survey(imports={"pathlib": Use(work=3)}, ships=["lib", "lib.paths"],
                       available=INSTALLED, stdlib=STDLIB)
        assert found.unused == ["lib", "lib.paths"]
        assert found.standard == {"pathlib": Use(work=3)}

    def test_use_by_a_test_or_an_example_is_not_use(self):
        """A component's own tests and examples are written *for* it. Counting them as adoption is
        what made pygim's container look used: three importers, all three examples of the container."""
        found = survey(imports={"lib.ioc": Use(shown=3), "lib.paths": Use(work=2, shown=9)},
                       ships=["lib.ioc", "lib.paths"], available=INSTALLED, stdlib=STDLIB)
        assert found.shown_only == ["lib.ioc"]
        assert found.used == [("lib.paths", Use(work=2, shown=9))]
        assert found.unused == []                       # it is imported; that is not the same thing

    def test_a_submodule_import_counts_for_the_package_when_only_the_package_is_shipped(self):
        found = survey(imports={"lib.deep.inner": Use(work=4)}, ships=["lib"],
                       available=INSTALLED, stdlib=STDLIB)
        assert found.ships == {"lib": Use(work=4)}

    def test_what_cannot_be_placed_is_said_rather_than_filed_as_a_stranger(self):
        """An answer silent about what it could not place cannot be told from a complete one
        (global memory #3). A missing install and a third-party library must not print alike."""
        found = survey(imports={"click": Use(work=2), "mystery": Use(work=1)},
                       ships=[], available=INSTALLED, stdlib=STDLIB)
        assert found.third_party == {"click": Use(work=2)}
        assert found.unresolved == {"mystery": Use(work=1)}

    def test_the_projects_own_private_packages_are_not_somebody_elses_library(self):
        """pygim's installed metadata claims `_pygim`, so without this every import of the project's
        own implementation package was filed under a third-party dependency — 40 of them."""
        found = survey(imports={"_lib.inner": Use(work=40)}, ships=["lib"], local={"_lib"},
                       available={"_lib": "lib", "click": "click"}, stdlib=STDLIB)
        assert found.local == {"_lib": Use(work=40)} and found.third_party == {}

    def test_a_declared_dependency_nothing_imports_is_named_and_an_imported_one_is_not(self):
        found = survey(imports={"click": Use(work=1)}, ships=[], available=INSTALLED,
                       declared=["click", "numpy", "pytest-xdist"], stdlib=STDLIB)
        assert found.declared_unused == ["numpy", "pytest-xdist"]

    def test_a_dash_in_a_distribution_name_is_the_underscore_in_the_import(self):
        found = survey(imports={"ruamel_yaml": Use(work=1)}, ships=[], available={},
                       declared=["ruamel-yaml"], stdlib=STDLIB)
        assert found.declared_unused == []


class TestWhatCountsAsUse:
    @pytest.mark.parametrize("path", ["tests/unittests/test_x.py", "docs/examples/ioc/basic.py",
                                      "benchmarks/run.py", "conftest.py", "src/lib/thing_test.py"])
    def test_a_file_written_for_a_component_is_a_demonstration(self, path):
        assert _inventory.is_shown(path)

    @pytest.mark.parametrize("path", ["src/lib/paths.py", "src/_lib/cli/app.py", "setup.py"])
    def test_a_file_that_builds_the_thing_is_work(self, path):
        assert not _inventory.is_shown(path)


class TestGatheringFromADirectory:
    """A tiny project on disk, because these are the mistakes that are about paths."""

    @pytest.fixture
    def project(self, tmp_path):
        lib = tmp_path / "src" / "lib"
        (lib / "deep").mkdir(parents=True)
        (lib / "__init__.py").write_text("from lib.paths import here\n", encoding="utf-8")
        (lib / "paths.py").write_text("import os\n", encoding="utf-8")
        (lib / "idle.py").write_text("", encoding="utf-8")
        (lib / "deep" / "__init__.py").write_text("", encoding="utf-8")
        private = tmp_path / "src" / "_lib"
        private.mkdir(parents=True)
        (private / "__init__.py").write_text("", encoding="utf-8")
        (private / "app.py").write_text("import lib.paths\nimport pathlib\n", encoding="utf-8")
        tests = tmp_path / "tests"
        tests.mkdir()
        (tests / "test_idle.py").write_text("import lib.idle\n", encoding="utf-8")
        build = tmp_path / "build" / "lib"
        build.mkdir(parents=True)
        (build / "copy.py").write_text("import lib.idle\n" * 50, encoding="utf-8")
        return tmp_path

    def test_it_finds_what_the_project_offers_an_importer(self, project):
        assert _inventory.ships_in(project) == ["lib", "lib.deep", "lib.idle", "lib.paths"]

    def test_a_package_importing_its_own_parts_is_not_a_customer(self, project):
        """`lib/__init__.py` importing `lib.paths` is the package being itself. Counting it would
        report every library as its own happiest user."""
        files = _inventory.python_files(project)
        counted = _inventory.imports_in(files, _inventory.ships_in(project))
        assert "lib.paths" in counted and counted["lib.paths"] == Use(work=1)   # from _lib only

    def test_build_output_is_not_the_project(self, project):
        """Fifty imports sit in build/, and they must not make an unused module look adopted."""
        files = _inventory.python_files(project)
        assert all("build/" not in relative for relative, _ in files)
        counted = _inventory.imports_in(files, _inventory.ships_in(project))
        assert counted["lib.idle"] == Use(shown=1)                              # the test, and nothing else

    def test_the_whole_survey_over_the_real_directory(self, project):
        found = _inventory.from_machine(project, distributions=[], stdlib=STDLIB)
        assert found.files == 7, [name for name, _ in _inventory.python_files(project)]
        assert found.shown_only == ["lib.idle"]
        assert found.unused == ["lib.deep"]
        assert dict(found.ships)["lib.paths"] == Use(work=1)
        assert found.standard == {"pathlib": Use(work=1), "os": Use(work=1)}

    def test_a_directory_named_after_the_package_it_ships_does_not_swallow_everything(self, tmp_path):
        """The trap this was written with: the repository directory is called `lib`, so matching the
        package by looking for `/lib/` anywhere in the path marked every file in the tree as being
        inside the package — and every import of it was skipped, reporting zero use of everything."""
        root = tmp_path / "lib"
        (root / "src" / "lib").mkdir(parents=True)
        (root / "src" / "lib" / "__init__.py").write_text("", encoding="utf-8")
        (root / "src" / "lib" / "paths.py").write_text("", encoding="utf-8")
        (root / "app").mkdir()
        (root / "app" / "main.py").write_text("import lib.paths\n", encoding="utf-8")
        found = _inventory.from_machine(root, distributions=[], stdlib=STDLIB)
        assert dict(found.ships)["lib.paths"] == Use(work=1)

    def test_a_project_with_no_manifest_declares_nothing_and_that_is_not_an_error(self, project):
        """Half the projects on this machine have no pyproject.toml — dnd imports pygim and declares
        nothing — so a design that starts from a manifest is dead on arrival."""
        assert _inventory.declared_in(project) == []


class TestTheCommandAHumanRuns:
    def test_it_prints_the_join_for_a_project_it_was_pointed_at(self, tmp_path):
        lib = tmp_path / "src" / "lib"
        lib.mkdir(parents=True)
        (lib / "__init__.py").write_text("", encoding="utf-8")
        (lib / "unused.py").write_text("", encoding="utf-8")
        env = {**os.environ, "NO_COLOR": "1", "HOME": str(tmp_path / "home")}
        done = subprocess.run([str(OO), "inventory", "--path", str(tmp_path)],
                              capture_output=True, text=True, env=env, timeout=120)
        assert done.returncode == 0, done.stderr
        assert "ships, and nothing imports it" in done.stdout
        assert "lib.unused" in done.stdout
        assert "2 of 2 shipped module(s) are not used" in done.stdout

    def test_an_application_that_ships_nothing_is_told_so_plainly(self, tmp_path):
        (tmp_path / "run.py").write_text("import json\n", encoding="utf-8")
        env = {**os.environ, "NO_COLOR": "1", "HOME": str(tmp_path / "home")}
        done = subprocess.run([str(OO), "inventory", "--path", str(tmp_path)],
                              capture_output=True, text=True, env=env, timeout=120)
        assert done.returncode == 0, done.stderr
        assert "ships nothing importable" in done.stdout

    def test_it_never_fails_on_what_it_finds(self, tmp_path):
        """A check that cries wolf is read once and then never again. This one reports; what should
        fail is decided after the exemptions are written down."""
        lib = tmp_path / "src" / "lib"
        lib.mkdir(parents=True)
        (lib / "__init__.py").write_text("", encoding="utf-8")
        env = {**os.environ, "NO_COLOR": "1", "HOME": str(tmp_path / "home")}
        done = subprocess.run([str(OO), "inventory", "--path", str(tmp_path)],
                              capture_output=True, text=True, env=env, timeout=120)
        assert done.returncode == 0


def test_the_check_over_this_project_finds_a_plausible_number_of_files():
    """A rule checked over nothing passes for the wrong reason, and this walk goes through the
    library it is testing: a PathSet that returned nothing would leave every band empty and the
    whole report cheerfully silent (global memory #20's one exemption)."""
    root = Path(__file__).resolve().parents[2]
    files = _inventory.python_files(root)
    assert len(files) > 40, f"only {len(files)} python files found under {root}"
    assert _inventory.ships_in(root) and "pygim" in _inventory.ships_in(root)
