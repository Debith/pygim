"""ENACT end to end, through the commands and nothing else.

Every step here is a process: `oo enact ...` in, JSON out, assertions on what comes back and on
what the store looks like afterwards. Nothing imports pygim, so these tests see the system the way
a shell script or an agent host sees it — the CLI, the MCP dispatch, the pybind adapter, the C++
service and the files store, in one round trip each (about 90 ms).

They are deliberately about the *negative* scenarios. A refusal is this system\'s most distinctive
behaviour: it is a result, not an exception, it names the facts that would make the call succeed,
and it must leave the store exactly as it was. The last of those is the one nothing else checks, so
every refusal here is followed by a look at the places it must not have touched.

The world is the test\'s own (global memory #2): a store under tmp_path, a user data directory and
a git configuration that point nowhere near this machine\'s.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

OO = Path(sys.executable).parent / "oo"

PACK = """\
pack: dnd
entry:
  brief: Dungeons and Dragons, 2024 rules.
  when: The knowledge is about designing for D&D.
  when_not: Not other tabletop games.
  example: Spell design notes.
# `pack: dnd` is itself the value domain=dnd — a pack names its own domain.
extends:
  artifact:
    spell:
      entry:
        brief: A spell.
        when: The knowledge is about a spell.
        when_not: Not about a monster.
        example: Shield.
"""

TAGS = ["domain=dnd", "artifact=spell", "task=design"]


# ── the world, and how to speak to it ─────────────────────────────────────────


@pytest.fixture
def env(tmp_path):
    """Nothing of this machine: no global store, no user git configuration, no user data directory
    and no colour. A test that found the developer\'s own stores would pass or fail by their
    contents, and one that wrote to them would be worse than a failing test."""
    home = tmp_path / "home"
    home.mkdir()
    return {**os.environ,
            "HOME": str(home),
            "PYGIM_ENACT_GLOBAL": str(tmp_path / "no-global-store"),
            "GIT_CONFIG_GLOBAL": str(tmp_path / "gitconfig"),
            "GIT_CONFIG_NOSYSTEM": "1",
            "XDG_DATA_HOME": str(tmp_path / "user-data"),
            "NO_COLOR": "1"}


def oo(*args, cwd, env, stdin=None):
    """`oo enact <args>` as a process. Returns it whole — the exit code carries meaning here."""
    return subprocess.run([str(OO), "enact", *args], cwd=str(cwd), env=env, input=stdin,
                          capture_output=True, text=True, timeout=120)


def call(name, cwd, env, **arguments):
    """One tool of the agent surface. A refusal is a result, so a non-zero exit is a test failure."""
    done = oo("call", name, "--json", json.dumps(arguments), cwd=cwd, env=env)
    assert done.returncode == 0, f"`oo enact call {name}` failed:\n{done.stderr}"
    return json.loads(done.stdout)


def fingerprint(store):
    """What a refusal must not change: the hash chain, the head views, the content objects and the
    vocabulary. Usage and receipts are evidence of being read and are deliberately left out."""
    out = {}
    for part in ("audit", "memories", "objects", "taxonomy"):
        for file in sorted((store / part).rglob("*")):
            if file.is_file():
                out[str(file.relative_to(store))] = hashlib.sha256(file.read_bytes()).hexdigest()
    return out


@pytest.fixture
def project(tmp_path, env):
    """A project with a store and a vocabulary, made the way a person makes one."""
    project = tmp_path / "project"
    project.mkdir()
    made = oo("setup", "--local", "--no-register", cwd=project, env=env)
    assert made.returncode == 0, made.stderr
    pack = project / "pack-dnd.yaml"
    pack.write_text(PACK, encoding="utf-8")
    accepted = oo("accept", "--pack", str(pack), "--reason", "the game", cwd=project, env=env)
    assert accepted.returncode == 0, accepted.stderr
    return project


@pytest.fixture
def store(project):
    return project / ".enact"


@pytest.fixture
def seeded(project, env):
    """One memory to refuse things about."""
    made = call("remember", project, env, title="Shield is the yardstick",
                text="A defensive reaction earns its slot only if it beats Shield.",
                tags=TAGS + ["kind=principle"], reason="the measure every later note uses")
    assert made["ok"], made
    return made["memory"]


# ── the scenarios ─────────────────────────────────────────────────────────────


class TestWithNoStore:
    def test_every_command_says_how_to_make_one_and_makes_nothing(self, tmp_path, env):
        bare = tmp_path / "bare"
        bare.mkdir()
        for args in (("status",), ("call", "read", "--json", json.dumps({"hard": TAGS}))):
            done = oo(*args, cwd=bare, env=env)
            assert done.returncode == 1, done.stdout
            assert "oo enact setup" in done.stderr, done.stderr
        assert sorted(p.name for p in bare.iterdir()) == []      # and nothing was created on the way


class TestTheVocabularyIsClosed:
    def test_a_read_with_a_tag_that_is_not_in_it_is_refused_and_suggests_what_is(self, project, env, store, seeded):
        was = fingerprint(store)
        out = call("read", project, env, hard=["domain=dnd", "artifact=potion", "task=design"])
        assert out["refused"] == "unknown tag" and "artifact=potion" in out["message"]
        assert "artifact=spell" in out["facts"]
        assert fingerprint(store) == was

    def test_a_write_with_a_tag_that_is_not_in_it_writes_nothing(self, project, env, store, seeded):
        was = fingerprint(store)
        out = call("remember", project, env, title="A potion", text="It heals.",
                   tags=["domain=dnd", "artifact=potion", "task=design"], reason="a note")
        assert out["refused"] == "unknown tag"
        assert fingerprint(store) == was
        assert call("read", project, env, hard=TAGS)["corpus"] == 1     # still only the seeded one

    def test_any_is_refused_in_a_query_once_the_dimension_has_a_value_to_name(self, project, env, seeded):
        out = call("read", project, env, hard=["domain=any", "artifact=spell", "task=design"])
        assert out["refused"] == "any in a query" and "domain=dnd" in out["facts"]


class TestAWriteMustHaveReadFirst:
    def test_writing_into_a_space_with_something_unread_is_refused_by_name(self, project, env, store, seeded):
        was = fingerprint(store)
        out = call("remember", project, env, title="Frost Ward", text="Typed resistance for a round.",
                   tags=TAGS + ["kind=example"], reason="a case")
        assert out["refused"] == "unread"
        assert any(seeded[:8] in fact or "Shield" in fact for fact in out["facts"]), out["facts"]
        assert fingerprint(store) == was

    def test_the_same_text_again_is_refused_rather_than_stored_twice(self, project, env, store, seeded):
        call("read", project, env, hard=TAGS)
        was = fingerprint(store)
        again = call("remember", project, env, title="Shield is the yardstick",
                     text="A defensive reaction earns its slot only if it beats Shield.",
                     tags=TAGS + ["kind=principle"], reason="written twice", seen=[seeded])
        assert again["refused"] == "identical"
        assert fingerprint(store) == was              # no second object holding the same text

    def test_superseding_something_already_superseded_is_refused(self, project, env, store, seeded):
        call("read", project, env, hard=TAGS)
        second = call("remember", project, env, title="Shield is the yardstick",
                      text="A defensive reaction beats Shield per slot, or it is a niche.",
                      tags=TAGS + ["kind=principle"], reason="sharper", supersedes=[seeded], seen=[seeded])
        assert second["ok"], second
        was = fingerprint(store)
        out = call("remember", project, env, title="Shield is the yardstick",
                   text="A third try at the same note.", tags=TAGS + ["kind=principle"],
                   reason="stale reference", supersedes=[seeded], seen=[seeded, second["memory"]])
        assert out["refused"] == "not a head"
        assert fingerprint(store) == was


class TestAttachmentsThatCannotBeMade:
    def test_a_tag_already_carried_and_one_not_carried_are_both_refused(self, project, env, store, seeded):
        was = fingerprint(store)
        assert call("link", project, env, memory=seeded, tag="task=design",
                    reason="it is already there")["refused"] == "already carried"
        assert call("unlink", project, env, memory=seeded, tag="kind=example",
                    reason="it never had it")["refused"] == "not carried"
        assert fingerprint(store) == was
        assert set(call("show", project, env, memory=seeded)["tags"]) == set(TAGS + ["kind=principle"])

    def test_a_locator_that_does_not_resolve_is_refused_before_it_is_recorded(self, project, env, store, seeded):
        was = fingerprint(store)
        out = call("link", project, env, memory=seeded, cite="phb:L10", reason="a guess")
        assert out["refused"] == "unknown locator"
        assert fingerprint(store) == was
        assert call("show", project, env, memory=seeded)["cites"] == []

    def test_naming_both_a_tag_and_a_cite_is_refused(self, project, env, seeded):
        out = call("link", project, env, memory=seeded, tag="kind=example", cite="phb:L10", reason="both")
        assert out["refused"] == "one of tag or cite"


class TestReferringToSomethingThatIsNotThere:
    def test_an_unknown_memory_is_refused_by_every_operation_that_names_one(self, project, env, store, seeded):
        was = fingerprint(store)
        for name, arguments in (("show", {"memory": "#404"}),
                                ("learn", {"memory": "#404", "reason": "no"}),
                                ("retire", {"memory": "deadbeefdeadbeef", "reason": "no"})):
            out = call(name, project, env, **arguments)
            assert out.get("refused") == "unknown memory", (name, out)
        assert fingerprint(store) == was


class TestOpeningTheStoreAgain:
    """Every command here is a process, and every process opens the store from scratch — which is
    why this is where the next defect showed. It is invisible in-process and invisible to any test
    that does not look at what an operation left behind."""

    def rows(self, store):
        return sum(1 for file in sorted((store / "audit").glob("*.jsonl"))
                   for line in file.read_text(encoding="utf-8").splitlines() if line.strip())

    def test_reading_three_times_over_does_not_grow_the_hash_chain(self, project, env, store, seeded):
        """A taxonomy row is appended when the vocabulary has changed since the last row said so,
        and the row that said so was found by scanning for the latest *by timestamp*, ties broken by
        row id — which is a coin toss among rows written in the same second. A store set up, given a
        pack and written to inside one second could lose that toss for good, and then appended a
        taxonomy row on every open, for ever, each one claiming a change that had not happened.

        The same defect class as the mailbox ordering the field report found: a digest standing in
        for an order. Replay is causal, so the snapshot now records the version as it applies the
        row, and nothing has to guess."""
        before = self.rows(store)
        for _ in range(3):
            assert call("read", project, env, hard=TAGS)["corpus"] == 1
        assert self.rows(store) == before

    def test_a_refused_call_leaves_the_chain_exactly_as_it_was(self, project, env, store, seeded):
        was = fingerprint(store)
        before = self.rows(store)
        for name, arguments in (("read", {"hard": ["domain=dnd", "artifact=potion", "task=design"]}),
                                ("remember", {"title": "X", "text": "y", "tags": ["artifact=potion"], "reason": "r"}),
                                ("link", {"memory": seeded, "tag": "task=design", "reason": "r"}),
                                ("unlink", {"memory": seeded, "tag": "kind=example", "reason": "r"}),
                                ("link", {"memory": seeded, "cite": "phb:L1", "reason": "r"}),
                                ("retire", {"memory": "#404", "reason": "r"})):
            assert "refused" in call(name, project, env, **arguments), name
        assert self.rows(store) == before
        assert fingerprint(store) == was


class TestTheCommandItself:
    def test_a_tool_that_does_not_exist_names_the_ones_that_do(self, project, env):
        done = oo("call", "invent", "--json", "{}", cwd=project, env=env)
        assert done.returncode == 1 and "no tool called `invent`" in done.stderr
        assert "remember" in done.stderr and "read" in done.stderr

    def test_arguments_that_are_not_json_fail_before_anything_is_opened(self, project, env, store, seeded):
        was = fingerprint(store)
        done = oo("call", "remember", "--json", "{not json", cwd=project, env=env)
        assert done.returncode == 1 and "not JSON" in done.stderr
        assert fingerprint(store) == was

    def test_arguments_come_from_stdin_when_json_is_not_given(self, project, env, seeded):
        done = oo("call", "read", cwd=project, env=env, stdin=json.dumps({"hard": TAGS}))
        assert done.returncode == 0, done.stderr
        assert json.loads(done.stdout)["corpus"] == 1
