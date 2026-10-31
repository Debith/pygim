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
import sysconfig
from pathlib import Path

import pytest

from _pygim import _inventory
from _pygim._inventory import Use, survey

# The installed command, where pip put it for this interpreter: bin/oo, or Scripts\oo.exe on Windows.
OO = Path(sysconfig.get_path("scripts")) / ("oo.exe" if os.name == "nt" else "oo")

STDLIB = frozenset({"pathlib", "json", "os"})
INSTALLED = {"click": "click", "pytest": "pytest", "numpy": "numpy"}


# Every way an import is written, for imports_in against the ast walk it replaced.
IMPORT_FORMS = {
    "plain, dotted and aliased": "import os, a.b as c\n",
    "from a module": "from x.y import z, w as v\n",
    "relative ones are the package itself": "from . import q\nfrom .m import n\nfrom ..up import u\n",
    "in a function, a class and branches": ("def f():\n    import inner\n\nclass C:\n    from k import v\n\n"
                                            "if X:\n    import maybe\ntry:\n    import fast\nexcept ImportError:\n    import slow\n"),
    "a star and the future": "from __future__ import annotations\nfrom s import *\n",
    "one name twice, and the package's own": "import twice\nimport twice\nimport lib.paths\nfrom lib import x\n",
    "a shipped submodule from its package": "from lib import sub\nfrom lib import sub, x\nfrom lib import *\n",
}


def imports_by_ast(files, ships):
    """imports_in as it was written over ast until 2026-10-30, with its reading of `from lib import
    sub` since: the oracle the faster one is held to."""
    import ast

    tops = {name.split(".")[0] for name in ships}
    counted = {}
    for relative, text in files:
        tree = ast.parse(text, filename=relative)
        inside = next((part for part in relative.split("/")[:-1] if part in tops), None)
        one = Use(shown=1) if _inventory.is_shown(relative) else Use(work=1)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                named = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                subs = [f"{node.module}.{alias.name}" for alias in node.names if f"{node.module}.{alias.name}" in ships]
                named = subs + ([node.module] if len(subs) < len(node.names) else [])
            else:
                continue
            for name in named:
                if name.split(".")[0] != inside:
                    counted[name] = counted.get(name, Use()).plus(one)
    return counted

class Dist:
    """What `importlib.metadata` is asked of a distribution here, as plain values; `asked` counts
    the reads of each file."""

    def __init__(self, name, files=(), groups=(), metadata=None):
        self.files, self.asked = list(files), {}
        self.entry_points = [type("EntryPoint", (), {"group": g})() for g in groups]
        self._texts = {"METADATA": f"Metadata-Version: 2.1\nName: {name}\nVersion: 1.0\n\nName: in the description\n"
                       if metadata is None else metadata,
                       "RECORD": "".join(f"{f},sha256=x,1\n" for f in self.files)}   # a path, its hash and its size a row

    def read_text(self, filename):
        self.asked[filename] = self.asked.get(filename, 0) + 1
        return self._texts.get(filename)

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

    def test_a_submodule_import_counts_for_the_nearest_package_that_is_shipped(self):
        """Every import under `pygim.core` was credited to `pygim`, so `pygim.core` — imported by eight
        test files — was reported as "nothing imports it, not even a test" (2026-10-02)."""
        found = survey(imports={"lib.deep.inner": Use(shown=2)}, ships=["lib", "lib.deep"],
                       available=INSTALLED, stdlib=STDLIB)
        assert found.ships == {"lib": Use(), "lib.deep": Use(shown=2)}

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


class TestDependenciesAreToldApartByHowTheyAreUsed:
    """Debith, 2026-09-24: "Test import should be separate." PyYAML is the answer key pathlike's YAML
    engine is checked against — imported by tests and a benchmark only — and the report said
    "declared as a dependency, never imported": `pyyaml` in pyproject.toml and `PyYAML` in its
    metadata were compared as two different names."""

    def test_a_declared_name_matches_its_distribution_whatever_the_case(self):
        found = survey(imports={"yaml": Use(shown=2)}, ships=[], available={"yaml": "PyYAML"},
                       declared=["pyyaml", "Ruamel.YAML"], stdlib=STDLIB)
        assert found.declared_unused == ["Ruamel.YAML"]

    def test_a_library_only_tests_import_is_kept_apart_from_what_the_work_uses(self):
        found = survey(imports={"click": Use(work=2, shown=1), "yaml": Use(shown=2), "pytest": Use(shown=30)},
                       ships=[], available={"click": "click", "yaml": "PyYAML", "pytest": "pytest"},
                       stdlib=STDLIB)
        assert [name for name, _ in found.third_party_in_work] == ["click"]
        assert [name for name, _ in found.third_party_shown_only] == ["pytest", "yaml"]
        assert found.provider("yaml") == "PyYAML" and found.provider("click") == "click"

    def test_a_declared_dependency_is_unused_or_run_as_a_command_or_not_installed_here(self):
        """Three different facts were one line: mypy and pytest-xdist are not installed in the
        environment that ran the report, and ruff is a program, never imported by design."""
        found = survey(imports={}, ships=[], available={"numpy": "numpy"}, stdlib=STDLIB,
                       declared=["numpy", "ruff", "mypy"], installed={"numpy", "ruff"}, commands={"ruff"})
        assert found.declared_unused == ["numpy"]
        assert found.declared_run == ["ruff"]
        assert found.declared_absent == ["mypy"]

    def test_a_distribution_that_puts_a_program_on_the_path_or_plugs_into_pytest_is_a_command(self):
        dists = [Dist("ruff", files=["../../../bin/ruff"]), Dist("pytest_xdist", groups=["pytest11"]),
                 Dist("numpy", files=["numpy/__init__.py"])]
        assert _inventory.commands_of(dists) == {"ruff", "pytest-xdist"}
        assert _inventory.installed_names(dists) == {"ruff", "pytest-xdist", "numpy"}

    @pytest.mark.parametrize("recorded, program", [
        (["pkg/bin/tool"], True), (["..\\..\\Scripts\\tool.exe"], True), (['"../../bin/a,b"'], True),
        (["pkg/binary.py", "pkg/bin.py", "bin.txt"], False), (["pkg/Scripts.py", "robin/x.py"], False),
    ], ids=["a bin folder deep down", "Windows' Scripts", "a quoted name with a comma", "bin in a name",
            "Scripts in a name, bin ending a folder's"])
    def test_a_program_is_a_file_in_a_bin_or_scripts_folder(self, recorded, program):
        """The folder a file is in, not a name that holds the word: RECORD rows as csv writes them."""
        assert _inventory.commands_of([Dist("d", files=recorded)]) == ({"d"} if program else set())

    def test_the_modules_of_a_distribution_without_top_level_txt_are_named_as_they_are_imported(self):
        """A top-level extension module is imported by the name before its first dot: `_yaml`, not
        `_yaml.cpython-312-x86_64-linux-gnu.so`; a dist-info, a .data folder or a program is no module."""
        dist = Dist("PyYAML", files=["yaml/__init__.py", "yaml/composer.py", "_yaml.cpython-312-x86_64-linux-gnu.so",
                                     "_win.cp312-win_amd64.pyd", "single.py", "PyYAML-6.0.dist-info/METADATA",
                                     "PyYAML-6.0.data/scripts/run.py", "../../../bin/yaml", "__pycache__/single.cpython-312.pyc"])
        assert _inventory.installed([dist]) == {name: "PyYAML" for name in ("yaml", "_yaml", "_win", "single")}

    def test_the_survey_reads_each_file_of_a_distribution_once(self, tmp_path):
        """METADATA was parsed three times a distribution and RECORD read twice, once for its modules
        and once for its programs: a seventh of every session's start (2026-10-30)."""
        dist = Dist("PyYAML", files=["yaml/__init__.py", "../../../bin/yaml"])
        _inventory.from_machine(tmp_path, distributions=[dist], stdlib=STDLIB)
        assert dist.asked == {"METADATA": 1, "RECORD": 1, "top_level.txt": 1}

    @pytest.mark.parametrize("metadata, name", [
        ("Name: crlf\r\nVersion: 1\r\n\r\nName: no\r\n", "crlf"), ("name:  lower  \n", "lower"),
        ("Version: 1\n\nName: in the description\n", None), ("", None),
    ], ids=["CRLF", "any case, spaces around", "only in the description", "no METADATA"])
    def test_a_name_is_read_off_the_headers(self, metadata, name):
        assert _inventory.name_of(Dist("unused", metadata=metadata)) == name

    def test_every_name_is_the_one_importlib_parses(self):
        """name_of reads the Name header where importlib's `.metadata` parses the whole METADATA, the
        description too, at five times the read (2026-10-30): held to it over every distribution here."""
        from importlib import metadata

        dists = list(metadata.distributions())
        assert dists and [_inventory.name_of(d) for d in dists] == [d.metadata["Name"] for d in dists]


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

    @pytest.mark.parametrize("form", IMPORT_FORMS)
    def test_the_imports_found_are_the_ones_ast_finds(self, form):
        """pygim's own parser finds them, at a sixteenth of ast's time on pygim's tree (2026-10-30):
        held to what the ast walk it replaced found, form by form, in the work and in a test."""
        files = [("lib/mod.py", IMPORT_FORMS[form]), ("tests/test_mod.py", IMPORT_FORMS[form])]
        assert _inventory.imports_in(files, ["lib", "lib.sub"]) == imports_by_ast(files, ["lib", "lib.sub"])

    def test_a_module_in_newer_syntax_than_the_running_python_is_still_read(self):
        """pygim.code reads Python 3.14 whatever Python runs it; ast left such a module out with a
        SyntaxError, and its imports with it (here: 3.14's except without parentheses)."""
        files = [("run.py", "try:\n    import fast\nexcept ImportError, OSError:\n    import slow\n")]
        assert _inventory.imports_in(files, []) == {"fast": Use(work=1), "slow": Use(work=1)}

    def test_a_module_that_is_not_python_is_passed_over(self):
        assert _inventory.imports_in([("bad.py", "import seen\ndef (:\n"), ("good.py", "import ok\n")], []) == {"ok": Use(work=1)}

    def test_build_output_is_not_the_project(self, project):
        """Fifty imports sit in build/, and they must not make an unused module look adopted."""
        files = _inventory.python_files(project)
        assert all("build/" not in relative for relative, _ in files)
        counted = _inventory.imports_in(files, _inventory.ships_in(project))
        assert counted["lib.idle"] == Use(shown=1)                              # the test, and nothing else

    def test_what_is_left_out_is_never_read(self, project):
        """CPython's tokenizer tests are Latin-1 on purpose. A copy of one under build/ (a sanitizer
        run's seeds, 2026-10-27) made python_files raise for the whole project: every file was read,
        and build/ left out only after."""
        (project / "build" / "lib" / "latin1.py").write_bytes(b"# -*- coding: latin-1 -*-\nx = '\xe9'\n")
        (project / "node_modules").mkdir()
        (project / "node_modules" / "gyp.py").write_bytes(b"\xff\xfe")
        assert _inventory.from_machine(project, distributions=[], stdlib=STDLIB).files == 7   # and nothing raised

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

    def test_a_manifest_is_read_on_a_python_without_tomllib(self, project, monkeypatch):
        """`tomllib` arrived in Python 3.11; a None in sys.modules is what an import finds on 3.10."""
        (project / "pyproject.toml").write_text(
            '[project]\nname = "lib"\ndependencies = ["click>=8"]\n\n'
            '[project.optional-dependencies]\ntest = ["pytest", "tomli; python_version < \'3.11\'"]\n',
            encoding="utf-8")
        monkeypatch.setitem(sys.modules, "tomllib", None)
        assert _inventory.declared_in(project) == ["click", "pytest", "tomli"]

    # A member the walk lists and the read skips — a folder, a tracked file deleted from disk, a broken
    # link — once made both lists misalign, and every file was dropped: the whole class, tested here.
    def test_outside_git_the_text_files_are_found_beside_folders(self, project):
        found = dict(_inventory.text_files(project))
        assert "src/lib/paths.py" in found and "tests/test_idle.py" in found
        assert not any(name.startswith("build/") for name in found)

    def test_a_tracked_file_deleted_from_disk_is_left_out_not_everything(self, project):
        git = lambda *args: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args],
                                           cwd=project, capture_output=True, text=True, check=True)
        git("init", "-q")
        git("add", "src", "tests")
        git("commit", "-q", "-m", "x")
        (project / "src" / "lib" / "idle.py").unlink()
        found = dict(_inventory.text_files(project))
        assert "src/lib/paths.py" in found and "src/lib/idle.py" not in found

    @pytest.mark.skipif(os.name == "nt", reason="symbolic links need a privilege on Windows")
    def test_a_broken_link_named_like_python_is_left_out_not_everything(self, project):
        (project / "src" / "lib" / "gone.py").symlink_to(project / "nowhere.py")
        found = dict(_inventory.python_files(project))
        assert "src/lib/paths.py" in found and "src/lib/gone.py" not in found

    def test_a_manifest_that_is_not_toml_declares_nothing_rather_than_failing(self, project):
        (project / "pyproject.toml").write_text("[project\nname = ", encoding="utf-8")
        assert _inventory.declared_in(project) == []


class TestTheCommandAHumanRuns:
    def test_a_manifest_is_read_where_python_has_no_tomllib(self, tmp_path, monkeypatch):
        """`tomllib` arrived in Python 3.11. On 3.10 (and 3.9, supported then) reading a
        pyproject.toml crashed the inventory and the map every session starts with — found
        2026-10-02 by running it under this machine's py39 environment, while CI's 3.9 and 3.10 jobs
        stayed green: no test read a real manifest. None in sys.modules stands in for such a Python: an import of tomllib fails. Asked
        through the application, as `oo inventory` and `oo inventory --map` pass it on: two `oo`
        processes took 0.6 s, and their PYTHONPATH ran an installed pygim, not the one under test."""
        from _pygim import _config
        from _pygim._cli._cli_app import GimmicksCliApp

        project = tmp_path / "proj"
        project.mkdir()
        (project / "run.py").write_text("import json\n", encoding="utf-8")
        (project / "pyproject.toml").write_text(
            '[project]\nname = "proj"\ndependencies = ["no-such-distribution-here"]\n\n'
            '[project.scripts]\nproj = "proj:main"\n', encoding="utf-8")
        monkeypatch.setitem(sys.modules, "tomllib", None)
        where = _config.read({"NO_COLOR": "1", "HOME": str(tmp_path / "home")}, cwd=project,
                             home=tmp_path / "home")
        said = GimmicksCliApp._capture(lambda: GimmicksCliApp().inventory(where=where, path=str(project)))
        assert "no-such-distribution-here" in said                  # declared, and not installed here
        mapped = GimmicksCliApp._capture(lambda: GimmicksCliApp().inventory(where=where, path=str(project), brief=True))
        assert "run as: proj" in mapped

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
        (tmp_path / "build").mkdir()
        (tmp_path / "build" / "bad_coding.py").write_bytes(b"# -*- coding: latin-1 -*-\nx = '\xe9'\n")   # left out, so never read
        again = subprocess.run([str(OO), "inventory", "--path", str(tmp_path)],
                               capture_output=True, text=True, env=env, timeout=120)
        assert again.returncode == 0 and again.stdout == done.stdout, again.stderr
        assert "2 of 2 shipped module(s) are not used" in done.stdout

    def test_an_application_that_ships_nothing_is_told_so_plainly(self, tmp_path):
        (tmp_path / "run.py").write_text("import json\n", encoding="utf-8")
        env = {**os.environ, "NO_COLOR": "1", "HOME": str(tmp_path / "home")}
        done = subprocess.run([str(OO), "inventory", "--path", str(tmp_path)],
                              capture_output=True, text=True, env=env, timeout=120)
        assert done.returncode == 0, done.stderr
        assert "ships nothing importable" in done.stdout

    def test_a_subpackage_its_tests_import_through_a_module_is_printed_as_shown(self, tmp_path):
        """`pygim.core` was printed under "nothing imports it — not even a test" while eight test files
        imported `pygim.core.<module>`. Asked through the application, as `oo inventory` passes it on."""
        from _pygim import _config
        from _pygim._cli._cli_app import GimmicksCliApp

        sub = tmp_path / "src" / "lib" / "sub"
        sub.mkdir(parents=True)
        for made in (sub.parent / "__init__.py", sub / "__init__.py", sub / "mod.py"):
            made.write_text("", encoding="utf-8")
        (tmp_path / "tests").mkdir()
        (tmp_path / "tests" / "test_mod.py").write_text("from lib.sub.mod import x\n", encoding="utf-8")
        where = _config.read({"NO_COLOR": "1", "HOME": str(tmp_path / "home")}, cwd=tmp_path,
                             home=tmp_path / "home")
        said = GimmicksCliApp._capture(lambda: GimmicksCliApp().inventory(where=where, path=str(tmp_path)))
        section = lambda heading: said.partition(heading)[2].partition("\n\n")[0]
        assert "lib.sub" not in section("ships, and nothing imports it"), said
        assert "lib.sub" in section("ships, and only its own tests and examples import it"), said

    def test_a_submodule_its_tests_import_from_its_package_is_printed_as_shown(self, tmp_path):
        """`from pygim import similarity` imports pygim.similarity as `import pygim.similarity` does,
        yet only pygim was credited: similarity was printed under "nothing imports it — not even a
        test" while its tests, benchmarks and example imported it so (2026-10-31)."""
        from _pygim import _config
        from _pygim._cli._cli_app import GimmicksCliApp

        lib = tmp_path / "src" / "lib"
        lib.mkdir(parents=True)
        for made in (lib / "__init__.py", lib / "sub.py"):
            made.write_text("", encoding="utf-8")
        (tmp_path / "tests").mkdir()
        (tmp_path / "tests" / "test_sub.py").write_text("from lib import sub\n", encoding="utf-8")
        where = _config.read({"NO_COLOR": "1", "HOME": str(tmp_path / "home")}, cwd=tmp_path,
                             home=tmp_path / "home")
        said = GimmicksCliApp._capture(lambda: GimmicksCliApp().inventory(where=where, path=str(tmp_path)))
        section = lambda heading: said.partition(heading)[2].partition("\n\n")[0]
        assert "lib.sub" not in section("ships, and nothing imports it"), said
        assert "lib.sub" in section("ships, and only its own tests and examples import it"), said

    def test_a_library_only_the_tests_import_is_printed_apart(self, tmp_path):
        (tmp_path / "run.py").write_text("import click\n", encoding="utf-8")
        (tmp_path / "tests").mkdir()
        (tmp_path / "tests" / "test_run.py").write_text("import pytest\n", encoding="utf-8")
        env = {**os.environ, "NO_COLOR": "1", "HOME": str(tmp_path / "home")}
        done = subprocess.run([str(OO), "inventory", "--path", str(tmp_path)],
                              capture_output=True, text=True, env=env, timeout=120)
        assert done.returncode == 0, done.stderr
        work, _, shown = done.stdout.partition("used only by tests, examples and benchmarks")
        assert "click" in work and "pytest" not in work and "pytest" in shown

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


@pytest.mark.skipif(not (Path(__file__).resolve().parents[2] / "src").is_dir(),
                    reason="source tree not present (testing the installed package)")
def test_the_check_over_this_project_finds_a_plausible_number_of_files():
    """A rule checked over nothing passes for the wrong reason, and this walk goes through the
    library it is testing: a PathSet that returned nothing would leave every band empty and the
    whole report cheerfully silent (global memory #20's one exemption)."""
    root = Path(__file__).resolve().parents[2]
    files = _inventory.python_files(root)
    assert len(files) > 40, f"only {len(files)} python files found under {root}"
    assert _inventory.ships_in(root) and "pygim" in _inventory.ships_in(root)


class TestWhereItRuns:
    """Finding the execution environment is part of discovery. On 2026-09-23 one of two sessions found
    the conda environment by itself and ran the inventory; the other did not, and gave up on it. An
    editable install records which checkout it came from, which makes this work for any project."""

    def conda_env(self, home, name, *, points_at=None, editable=True, dist="proj-1.0"):
        prefix = home / "miniconda3" / "envs" / name
        site = prefix / "lib" / "python3.12" / "site-packages"
        (site / f"{dist}.dist-info").mkdir(parents=True)
        (prefix / "lib" / "python3.1").symlink_to(prefix / "lib" / "python3.12")    # as conda lays it out
        (prefix / "bin").mkdir()
        (prefix / "bin" / "python3.12").write_text("", encoding="utf-8")
        (prefix / "bin" / "python").symlink_to(prefix / "bin" / "python3.12")
        (prefix / "bin" / "oo").write_text("", encoding="utf-8")
        if points_at is not None:
            (site / f"{dist}.dist-info" / "direct_url.json").write_text(json.dumps(
                {"url": points_at.as_uri(), "dir_info": {"editable": editable}}), encoding="utf-8")
        return prefix

    def windows_env(self, home, name, *, points_at, dist="proj-1.0"):
        """A conda environment as Windows lays it out: Lib\\site-packages, python.exe at the root,
        commands in Scripts\\ and the Python version only in conda-meta."""
        prefix = home / "miniconda3" / "envs" / name
        site = prefix / "Lib" / "site-packages"
        (site / f"{dist}.dist-info").mkdir(parents=True)
        (prefix / "python.exe").write_text("", encoding="utf-8")
        (prefix / "Scripts").mkdir()
        (prefix / "Scripts" / "oo.exe").write_text("", encoding="utf-8")
        (prefix / "conda-meta").mkdir()
        (prefix / "conda-meta" / "python-3.12.4-h2628c8c_0_cpython.json").write_text("{}", encoding="utf-8")
        (site / f"{dist}.dist-info" / "direct_url.json").write_text(json.dumps(
            {"url": points_at.as_uri(), "dir_info": {"editable": True}}), encoding="utf-8")
        return prefix

    def test_an_environment_laid_out_the_windows_way_is_found(self, tmp_path):
        """The first CI run on Windows found no environment at all: the search knew only
        lib/python*/site-packages, bin/python and bin/<command> (2026-09-28)."""
        home, project = tmp_path / "home", tmp_path / "proj"
        project.mkdir()
        prefix = self.windows_env(home, "work", points_at=project)
        hosts = _inventory.environments(project, home)
        assert [(h.name, h.python, h.installs) for h in hosts] == [("work", "3.12", ("proj 1.0",))]
        assert hosts[0].tool("oo") == prefix / "Scripts" / "oo.exe"
        assert hosts[0].tool("python") == prefix / "python.exe"
        assert "use ~/miniconda3/envs/work/python.exe" in _inventory.project_map(project, home=home)

    def test_the_environment_holding_an_editable_install_of_the_project_is_found_once(self, tmp_path):
        home, project = tmp_path / "home", tmp_path / "proj"
        project.mkdir()
        self.conda_env(home, "work", points_at=project)
        self.conda_env(home, "other", points_at=tmp_path / "elsewhere")
        self.conda_env(home, "copied", points_at=project, editable=False)       # an install, not a checkout
        hosts = _inventory.environments(project, home)
        assert [(h.name, h.python, h.installs) for h in hosts] == [("work", "3.12", ("proj 1.0",))]
        assert hosts[0].tool("oo") == home / "miniconda3" / "envs" / "work" / "bin" / "oo"

    def test_a_virtual_environment_inside_the_project_is_where_it_runs(self, tmp_path):
        project = tmp_path / "proj"
        (project / ".venv").mkdir(parents=True)
        (project / ".venv" / "pyvenv.cfg").write_text("home = /usr/bin\n", encoding="utf-8")
        hosts = _inventory.environments(project, tmp_path / "home")
        assert [(h.kind, h.name, h.how) for h in hosts] == [("venv", ".venv", "a virtual environment inside the project")]

    def test_the_map_says_where_it_runs_and_names_the_command_that_works(self, tmp_path):
        home, project = tmp_path / "home", tmp_path / "proj"
        (project / "src" / "proj").mkdir(parents=True)
        (project / "src" / "proj" / "__init__.py").write_text("", encoding="utf-8")
        (project / "src" / "proj" / "idle.py").write_text("", encoding="utf-8")
        self.conda_env(home, "work", points_at=project)
        shown = _inventory.project_map(project, home=home)
        assert "runs in: conda env `work`, Python 3.12, installed editable as proj; not active in a session's shell" \
               " — use ~/miniconda3/envs/work/bin/python" in shown
        assert "`~/miniconda3/envs/work/bin/oo inventory` for the join" in shown

    def test_two_installs_of_one_checkout_are_named_as_such(self, tmp_path):
        home, project = tmp_path / "home", tmp_path / "proj"
        project.mkdir()
        prefix = self.conda_env(home, "work", points_at=project, dist="proj-2.0")
        old = prefix / "lib" / "python3.12" / "site-packages" / "old_name-1.0.dist-info"
        old.mkdir()
        (old / "direct_url.json").write_text(json.dumps({"url": project.as_uri(), "dir_info": {"editable": True}}),
                                             encoding="utf-8")
        assert "2 installs of one checkout" in _inventory.project_map(project, home=home)

    def test_two_installs_that_own_the_same_script_say_what_uninstalling_one_would_take(self, tmp_path):
        """On 2026-09-23 the older of two installs was uninstalled as stale and took `bin/oo` with it —
        both had it in their RECORD — and every hook and server start failed until it was restored."""
        home, project = tmp_path / "home", tmp_path / "proj"
        project.mkdir()
        prefix = self.conda_env(home, "work", points_at=project, dist="proj-2.0")
        site = prefix / "lib" / "python3.12" / "site-packages"
        (site / "proj-2.0.dist-info" / "RECORD").write_text("../../../bin/oo,sha256=a,1\nproj/__init__.py,,\n",
                                                           encoding="utf-8")
        old = site / "old_name-1.0.dist-info"
        old.mkdir()
        (old / "direct_url.json").write_text(json.dumps({"url": project.as_uri(), "dir_info": {"editable": True}}),
                                             encoding="utf-8")
        (old / "RECORD").write_text("../../../bin/oo,sha256=b,1\nold_name/__init__.py,,\n", encoding="utf-8")
        shown = _inventory.project_map(project, home=home)
        assert "both owning oo — uninstalling either removes them; reinstall the other after" in shown

    def test_a_project_nothing_hosts_says_so_rather_than_saying_nothing(self, tmp_path):
        project = tmp_path / "proj"
        project.mkdir()
        assert "runs in: not found" in _inventory.project_map(project, home=tmp_path / "home")


def test_a_project_file_that_is_not_utf8_is_read_and_the_rest_with_it(tmp_path):
    """One file of the project's own in Latin-1 is read (its bad bytes replaced), not the whole
    inventory refused for it: read_all_files raised ValueError for the project before."""
    project = tmp_path / "p"
    (project / "src" / "pkg").mkdir(parents=True)
    (project / "src" / "pkg" / "__init__.py").write_text("import os\n")
    (project / "src" / "pkg" / "old.py").write_bytes("# coding: latin-1\nx = 'é'\nimport json\n".encode("latin-1"))
    found = dict(_inventory.python_files(project))
    assert sorted(found) == ["src/pkg/__init__.py", "src/pkg/old.py"]
    assert "import json" in found["src/pkg/old.py"]
