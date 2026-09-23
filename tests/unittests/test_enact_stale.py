# -*- coding: utf-8 -*-
"""Whether what a memory names still exists.

`oo memory reload` stayed in a standing card for a day after the command was renamed, and running it
printed a sentence and exited 0. A memory is prose, and prose does not fail — so this reads what a
memory puts in code font and asks. Certainty is part of the answer: a dead command in a card is
stale; a bare word or an illustrative path is only worth a look, because a check that cries wolf is
read once and then never again.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import click
import pytest

from _pygim._mcp import _cards, _stale
from _pygim._mcp._stale import Doubt, Resolvers

OO = Path(sys.executable).parent / "oo"


@click.group()
def tool():
    """A command tree to resolve against, standing in for `oo`."""


@tool.group()
def enact():
    pass


@enact.command()
@click.option("--signal", is_flag=True)
def reload(signal):
    pass


class TestWhatASpanIs:
    @pytest.mark.parametrize("span, sort", [
        ("oo enact reload", "command"), ("src/_pygim/_config.py:106", "path"), ("_mcp/enact.py", "path"),
        ("policy.yaml", "path"), ("_stores.project_of", "name"), ("from_process()", "name")])
    def test_a_span_is_sorted_by_its_shape(self, span, sort):
        assert _stale.classify(span)[0] == sort

    @pytest.mark.parametrize("span", ["<cmd>", "tests/**", "https://clig.dev/", "--root", "a b c", "any", "42"])
    def test_what_cannot_be_checked_is_left_alone(self, span):
        assert _stale.classify(span) is None


class TestCommands:
    resolve = staticmethod(_stale.command_resolver(tool))

    def test_a_command_that_exists_with_an_option_it_takes_holds(self):
        assert self.resolve(["oo", "enact", "reload", "--signal"]) is None
        assert self.resolve(["oo", "enact"]) is None                       # a group on its own is still a group

    def test_a_renamed_group_is_named_with_what_there_is_instead(self):
        assert self.resolve(["oo", "memory", "reload"]) == "no such command: `oo memory`; `oo` has enact"

    def test_a_near_miss_is_offered_its_likely_meaning(self):
        assert "did you mean `oo enact reload`?" in self.resolve(["oo", "enact", "relaod"])

    def test_an_option_the_command_does_not_take_is_named(self):
        assert self.resolve(["oo", "enact", "reload", "--force"]).startswith("`oo enact reload` takes no `--force`")


class TestPathsAndNames:
    @pytest.fixture
    def project(self, tmp_path):
        (tmp_path / "src" / "pkg").mkdir(parents=True)
        (tmp_path / "src" / "pkg" / "mod.py").write_text("def real_function():\n    pass\n", encoding="utf-8")
        return tmp_path

    def test_a_path_from_the_projects_top_is_certain_either_way(self, project):
        resolve = _stale.path_resolver(project, project / "home", ["src/pkg/mod.py"])
        assert resolve("src/pkg/mod.py") is None
        gone = resolve("src/pkg/gone.py")
        assert gone == "does not exist" and not isinstance(gone, Doubt)

    def test_a_partial_path_that_ends_a_real_one_holds(self, project):
        resolve = _stale.path_resolver(project, project / "home", ["src/pkg/mod.py"])
        assert resolve("pkg/mod.py") is None

    def test_an_illustrative_path_is_only_a_doubt(self, project):
        found = _stale.path_resolver(project, project / "home", ["src/pkg/mod.py"])("pkg/c.py")
        assert isinstance(found, Doubt)

    def test_a_line_past_the_end_of_the_file_is_named(self, project):
        resolve = _stale.path_resolver(project, project / "home", ["src/pkg/mod.py"])
        assert resolve("src/pkg/mod.py:99") == "points at line 99; the file has 3"

    def test_a_name_under_the_projects_own_package_is_certain(self):
        resolve = _stale.name_resolver({"pkg", "real_function"}, ours={"pkg"}, elsewhere={"os"})
        assert resolve("pkg.real_function") is None
        gone = resolve("pkg.renamed_function")
        assert "renamed_function" in gone and not isinstance(gone, Doubt)

    def test_another_librarys_name_is_not_this_projects_business(self):
        assert _stale.name_resolver(set(), ours={"pkg"}, elsewhere={"unittest"})("unittest.TestResult") is None

    def test_a_bare_word_nowhere_in_the_project_is_only_a_doubt(self):
        assert isinstance(_stale.name_resolver({"pkg"}, ours={"pkg"})("pipx"), Doubt)


class TestAMemoryAsAWhole:
    resolve = Resolvers(command=_stale.command_resolver(tool),
                        link=_stale.link_resolver({"live-slug": "#1"}, {"old-slug": "live-slug"}))

    def memory(self, text):
        return {"memory": "#5", "title": "A rule", "text": text}

    def test_a_dead_command_in_the_card_is_stale(self):
        found = _stale.check(self.memory(_cards.compose({"when": "the server is old", "why": "stale code",
                                                          "do": "run `oo memory reload`"})), self.resolve)
        assert [(f.part, f.named, f.stale) for f in found] == [("card", "oo memory reload", True)]

    def test_the_same_command_in_the_body_is_history_and_only_worth_a_look(self):
        text = _cards.compose({"when": "the server is old", "why": "stale code", "do": "run `oo enact reload`"},
                              "It was `oo memory reload` until 2026-09-22.")
        found = _stale.check(self.memory(text), self.resolve)
        assert [(f.part, f.stale) for f in found] == [("body", False)]

    def test_a_memory_written_before_the_card_is_judged_as_a_card(self):
        found = _stale.check(self.memory("Run `oo memory reload` without asking."), self.resolve)
        assert found and found[0].stale

    def test_a_link_to_a_superseded_memory_names_its_head(self):
        found = _stale.check(self.memory("When: x\nWhy: y\n\nSee [[old-slug]] and [[live-slug]] and [[nothing]]."),
                             self.resolve)
        assert [(f.named, f.problem) for f in found] == [
            ("[[old-slug]]", "points at a superseded memory; link [[live-slug]] instead"),
            ("[[nothing]]", "no memory has this slug")]

    def test_code_citing_a_superseded_global_memory_is_listed_with_its_line(self):
        resolve = _stale.reference_resolver({20: "Eat your own dogfood"}, {12: 20})
        old, new = 12, 20                        # built at runtime, so this file cites nothing itself
        files = [("tests/test_x.py", f"x = 1\n# see global memory #{old}\n# and global #{new}\n")]
        assert _stale.cited_in_code(files, resolve) == [
            ("tests/test_x.py:2", f"global #{old} was superseded by global #{new}")]


def test_the_command_finds_a_dead_command_in_a_real_store(tmp_path):
    """Through the real command, over a real store: the one a person runs to find the work."""
    project = tmp_path / "proj"
    project.mkdir()
    home = tmp_path / "home"
    home.mkdir()
    env = {**os.environ, "HOME": str(home), "NO_COLOR": "1", "XDG_DATA_HOME": str(tmp_path / "data"),
           "PYGIM_ENACT_GLOBAL": str(tmp_path / "no-global"), "GIT_CONFIG_GLOBAL": str(tmp_path / "gitconfig"),
           "GIT_CONFIG_NOSYSTEM": "1"}
    run = lambda *args: subprocess.run([str(OO), "enact", *args], cwd=project, env=env,
                                       capture_output=True, text=True, timeout=120)
    assert run("setup", "--local", "--no-register").returncode == 0
    made = json.loads(run("call", "remember", "--json", json.dumps({
        "title": "Reload the server after changing its code", "tags": ["domain=any", "artifact=any", "task=design"],
        "when": "the server's code changed", "why": "a stale server answers with old behaviour",
        "do": "run `oo memory reload`", "text": "It used to be `oo memory reload`."})).stdout)
    assert made["ok"], made
    said = run("status", "--stale").stdout
    assert "1 stale" in said and "no such command: `oo memory`" in said
    assert "1 to look at" in said                                          # the same words in the body
