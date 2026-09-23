# -*- coding: utf-8 -*-
"""Delivering a rule at the moment it governs, rather than at the session's door.

A rule reaches an agent at session start and is inert two hundred calls later, when the decision it
governs is actually made (global memory #16). The moment a file is about to be written is a moment
that can be classified — by its path — and `triggers.yaml` in the store is that classification,
written by hand, versioned with everything else, and the first stand-in for the context controller
the design is aimed at.

Everything here runs the real command as a process, the way a host runs it.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

from _pygim._mcp import _triggers

OO = Path(sys.executable).parent / "oo"

TRIGGERS = """\
# a comment, and a blank line, both ignored

"tests/**": [artifact=test, task=design, term=test]
'src/**/*.h': [language=cpp, task=implement]
"docs/**": [artifact=design_doc]
"""


class TestWhatAPathIsAbout:
    """The map itself: no store, no server, no process — just the classification."""

    def test_every_pattern_that_matches_contributes_its_tags(self, tmp_path):
        (tmp_path / "triggers.yaml").write_text(TRIGGERS, encoding="utf-8")
        loaded = _triggers.load(tmp_path)
        assert list(loaded) == ["tests/**", "src/**/*.h", "docs/**"]        # the file's own order
        assert _triggers.tags_for("tests/unittests/test_x.py", loaded) == [
            "artifact=test", "task=design", "term=test"]
        assert _triggers.tags_for("src/_pygim_fast/enact/core/service.h", loaded) == [
            "language=cpp", "task=implement"]

    def test_a_path_in_no_space_gets_nothing(self, tmp_path):
        """Silence is the right answer, and the common one. A delivery that fires on everything is
        read as noise and then not read at all."""
        (tmp_path / "triggers.yaml").write_text(TRIGGERS, encoding="utf-8")
        assert _triggers.tags_for("README.md", _triggers.load(tmp_path)) == []
        assert _triggers.load(tmp_path / "nowhere") == {}                   # and no map is not an error

    def test_an_absolute_path_is_matched_by_its_project_relative_form(self, tmp_path):
        (tmp_path / "triggers.yaml").write_text(TRIGGERS, encoding="utf-8")
        project = tmp_path / "proj"
        (project / "tests").mkdir(parents=True)
        here = project / "tests" / "test_y.py"
        here.write_text("", encoding="utf-8")
        assert _triggers.tags_for(str(here), _triggers.load(tmp_path), project) == [
            "artifact=test", "task=design", "term=test"]

    def test_a_term_is_separated_from_the_tags_it_travels_with(self):
        """Tags say where in the taxonomy a moment sits; a term says what it is about. The global
        store needs the second, because it answers every project and so names nothing specific."""
        tags, term = _triggers.split_term(["artifact=test", "term=test", "task=design"])
        assert tags == ["artifact=test", "task=design"] and term == "test"
        assert _triggers.split_term(["artifact=test"]) == (["artifact=test"], "")

    def test_a_memory_that_wrote_its_own_one_line_is_delivered_by_it(self):
        """The house style's second element is a line beginning `One line:`. The compact form a
        moment can afford is therefore already written, by whoever knew what mattered."""
        assert _triggers.one_line("Title\n\nOne line: keep it near.\n\nmore") == "keep it near."
        assert _triggers.one_line("**One line:** near.") == "near."
        assert _triggers.one_line("no such line here") == ""


@pytest.fixture
def env(tmp_path):
    home = tmp_path / "home"
    home.mkdir(exist_ok=True)
    return {**os.environ, "HOME": str(home), "NO_COLOR": "1",
            "PYGIM_ENACT_GLOBAL": str(tmp_path / "no-global-store"),
            "GIT_CONFIG_GLOBAL": str(tmp_path / "gitconfig"), "GIT_CONFIG_NOSYSTEM": "1",
            "XDG_DATA_HOME": str(tmp_path / "data")}


@pytest.fixture
def project(tmp_path, env):
    """A project with a store, a vocabulary and one memory about tests."""
    project = tmp_path / "proj"
    project.mkdir()
    assert subprocess.run([str(OO), "enact", "setup", "--local", "--no-register"], cwd=project,
                          env=env, capture_output=True, text=True).returncode == 0
    pack = project / "pack.yaml"
    pack.write_text("pack: proj\nentry:\n  brief: The project.\n  when: About it.\n"
                    "  when_not: Not another.\n  example: A note.\nextends:\n  artifact:\n"
                    "    test: {entry: {brief: A test., when: About a test., when_not: Not code.,"
                    " example: A fixture.}}\n", encoding="utf-8")
    assert subprocess.run([str(OO), "enact", "accept", "--pack", str(pack), "--reason", "the pack"],
                          cwd=project, env=env, capture_output=True, text=True).returncode == 0
    (project / ".enact" / "triggers.yaml").write_text(TRIGGERS, encoding="utf-8")
    made = subprocess.run(
        [str(OO), "enact", "call", "remember", "--json", json.dumps({
            "title": "A test names its own world", "reason": "it outlives the task",
            "when": "Writing or changing a test.", "why": "A test that reads the machine is not reproducible.",
            "text": "One line: build the world in the test, never the machine's.",
            "tags": ["domain=proj", "artifact=test", "task=design", "kind=principle"]})],
        cwd=project, env=env, capture_output=True, text=True)
    assert json.loads(made.stdout)["ok"], made.stdout
    return project


def hook(event, project, env):
    """The command as a host runs it: JSON on stdin, JSON or nothing on stdout."""
    done = subprocess.run([str(OO), "enact", "hook"], cwd=str(project), env=env,
                          input=json.dumps(event), capture_output=True, text=True, timeout=60)
    assert done.returncode == 0, done.stderr
    return json.loads(done.stdout) if done.stdout.strip() else None


class TestTheHookAHostRuns:
    def test_a_write_to_a_mapped_path_carries_the_rule_for_that_space(self, project, env):
        said = hook({"hook_event_name": "PreToolUse", "tool_name": "Write", "cwd": str(project),
                     "tool_input": {"file_path": "tests/unittests/test_thing.py"}}, project, env)
        assert said["hookSpecificOutput"]["hookEventName"] == "PreToolUse"
        context = said["hookSpecificOutput"]["additionalContext"]
        assert "A test names its own world" in context
        assert "build the world in the test" in context           # its own one-line, not its first line
        assert "artifact=test" in context                         # and why it was chosen

    def test_a_write_to_an_unmapped_path_says_nothing_at_all(self, project, env):
        assert hook({"hook_event_name": "PreToolUse", "tool_name": "Write",
                     "cwd": str(project), "tool_input": {"file_path": "README.md"}}, project, env) is None

    def test_a_tool_call_with_no_file_and_an_unknown_event_say_nothing(self, project, env):
        assert hook({"hook_event_name": "PreToolUse", "tool_input": {}}, project, env) is None
        assert hook({"hook_event_name": "Whatever"}, project, env) is None
        assert hook({}, project, env) is None

    def test_session_start_carries_the_standing_knowledge_when_there_is_any(self, project, env):
        """Silence is right for a store with no preferences in it, so the fixture writes one: the
        two events share a command, and this is the half that must still deliver everything."""
        assert hook({"hook_event_name": "SessionStart", "source": "startup"}, project, env) is None
        made = subprocess.run(
            [str(OO), "enact", "call", "remember", "--json", json.dumps({
                "title": "Lay options out as a table", "reason": "how Debith wants answers",
                "when": "Answering with choices.", "why": "A reader compares options faster in a table.",
                "text": "One line: name the choices, recommend one.",
                "tags": ["domain=proj", "artifact=any", "task=design", "kind=preference"],
                "seen": ["#0"]})],
            cwd=project, env=env, capture_output=True, text=True)
        assert json.loads(made.stdout)["ok"], made.stdout
        said = hook({"hook_event_name": "SessionStart", "source": "startup"}, project, env)
        assert said["hookSpecificOutput"]["hookEventName"] == "SessionStart"
        assert "Lay options out as a table" in said["hookSpecificOutput"]["additionalContext"]

    def test_a_project_with_no_store_is_silent_rather_than_an_error(self, tmp_path, env):
        bare = tmp_path / "bare"
        bare.mkdir()
        assert hook({"hook_event_name": "PreToolUse", "cwd": str(bare),
                     "tool_input": {"file_path": "tests/x.py"}}, bare, env) is None


class TestCheckingTheMapItself:
    """A trigger that delivers nothing fails silently: the hook fires, the command says nothing, and
    the rule that should have arrived never does, with no failure anywhere to notice. Hard tags
    intersect, so emptying a trigger by making it more precise is the common way to get it wrong —
    three of this project's own ten did it on the first draft."""

    def run(self, project, env):
        done = subprocess.run([str(OO), "enact", "status", "--triggers"], cwd=str(project), env=env,
                              capture_output=True, text=True, timeout=120)
        assert done.returncode == 0, done.stderr
        return done.stdout

    def test_it_names_the_triggers_that_deliver_nothing(self, project, env):
        (project / ".enact" / "triggers.yaml").write_text(
            '"tests/**": [artifact=test, task=design]\n'
            '"src/**": [artifact=test, task=troubleshoot]\n', encoding="utf-8")
        said = self.run(project, env)
        assert "A test names its own world" in said              # the one that works, with what it gives
        assert "delivers nothing" in said
        assert "1 of 2 deliver nothing" in said

    def test_a_map_that_all_works_says_so_by_not_complaining(self, project, env):
        (project / ".enact" / "triggers.yaml").write_text(
            '"tests/**": [artifact=test, task=design]\n', encoding="utf-8")
        said = self.run(project, env)
        assert "delivers nothing" not in said and "A test names its own world" in said

    def test_no_map_at_all_is_said_plainly(self, project, env):
        (project / ".enact" / "triggers.yaml").unlink()
        assert "no trigger map" in self.run(project, env)


class TestAProcessArrivesWhenItsTaskIsAsked:
    """A procedure known only by its title is not followed — standing knowledge carries titles, and
    three sessions asked to "Analyze this file" on 2026-09-23 followed no process at all. So the
    steps arrive at the moment a request asks for their task, matched on the words the procedure
    itself names in `asked`, and nothing arrives when no task is asked."""

    @pytest.fixture
    def with_a_process(self, project, env):
        made = subprocess.run(
            [str(OO), "enact", "call", "remember", "--json", json.dumps({
                "title": "Review what exists against what the project already has",
                "tags": ["domain=proj", "artifact=any", "task=design", "kind=procedure"],
                "when": "asked to review existing work", "why": "a review of the file alone misses the shelf",
                "asked": "review, analyze, analyse, critique",
                "steps": ["Discover the project — check: can you name what it ships?",
                          "Judge it against what the project owns — check: a finding or an explicit nothing?"],
                "seen": ["#0"], "reason": "a process"})],
            cwd=project, env=env, capture_output=True, text=True)
        assert json.loads(made.stdout)["ok"], made.stdout
        return project

    def ask(self, prompt, project, env):
        return hook({"hook_event_name": "UserPromptSubmit", "prompt": prompt, "cwd": str(project)}, project, env)

    def test_a_request_that_asks_for_the_task_gets_the_steps_in_full(self, with_a_process, env):
        said = self.ask("Analyze this file, please", with_a_process, env)
        context = said["hookSpecificOutput"]["additionalContext"]
        assert said["hookSpecificOutput"]["hookEventName"] == "UserPromptSubmit"
        assert "1. Discover the project — check: can you name what it ships?" in context
        assert "checklist" in context                                 # and it asks to be followed visibly

    def test_a_request_that_asks_for_no_task_hears_nothing(self, with_a_process, env):
        assert self.ask("go", with_a_process, env) is None
        assert self.ask("yes, commit it", with_a_process, env) is None

    def test_a_word_inside_another_word_is_not_a_request(self, with_a_process, env):
        assert self.ask("the reviewer-bot left a note", with_a_process, env) is None


class TestTheSessionStartsWithTheProject:
    def test_the_project_map_comes_first_when_the_directory_is_a_checkout(self, project, env):
        subprocess.run(["git", "init", "-q"], cwd=project, env=env, check=True)
        (project / "README.md").write_text("# Proj\n\nA project that does one thing.\n", encoding="utf-8")
        (project / "src" / "proj").mkdir(parents=True)
        (project / "src" / "proj" / "__init__.py").write_text("", encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=project, env=env, check=True)
        made = subprocess.run(
            [str(OO), "enact", "call", "remember", "--json", json.dumps({
                "title": "Lay options out as a table", "reason": "how Debith wants answers",
                "when": "Answering with choices.", "why": "A reader compares options faster in a table.",
                "tags": ["domain=proj", "artifact=any", "task=design", "kind=preference"], "seen": ["#0"]})],
            cwd=project, env=env, capture_output=True, text=True)
        assert json.loads(made.stdout)["ok"], made.stdout
        said = hook({"hook_event_name": "SessionStart", "source": "startup", "cwd": str(project)}, project, env)
        context = said["hookSpecificOutput"]["additionalContext"]
        assert context.startswith(f'<project-map root="{project}">')
        assert "Proj — A project that does one thing." in context and "ships: proj" in context
        assert context.index("</project-map>") < context.index("Lay options out as a table")
