"""ENACT end to end, through its two shipped surfaces and nothing else.

Every step here is a process. Nothing imports pygim, so these tests see the system the way a shell
script or an agent host sees it — the CLI, the MCP dispatch, the pybind adapter, the C++ service
and the files store, in one round trip each.

**Two surfaces, and they are not the same path.** `oo enact call` reaches the server\'s dispatch and
stops there; the MCP server also has a loop around it. Both are production — a person\'s scripts and
session-start hook use the first, an agent host uses the second — so both are tested as themselves,
and neither stands in for the other:

| | `oo enact call` | `oo enact mcp` |
|---|---|---|
| tool dispatch, refusals, the store | yes | yes |
| JSON-RPC framing, `initialize`, `tools/list` | no | yes |
| recovering from a bad line and serving on | no | yes |
| several calls in one process — one session, turn counts, said-once notices | no | yes |
| a reload asked for between messages | no | yes |

`TestOverTheRealProtocol` drives the second: the installed `oo enact mcp` as a subprocess, one JSON
line in and one out, which is exactly what an agent host does to it. The rest drive the first.

They are deliberately about the *negative* scenarios. A refusal is this system\'s most distinctive
behaviour: it is a result, not an exception, it names the facts that would make the call succeed,
and it must leave the store exactly as it was. The last of those is the one nothing else checks, so
every refusal here is followed by a look at the places it must not have touched.

The world is the test\'s own (global memory #26): a store under tmp_path, a user data directory and
a git configuration that point nowhere near this machine\'s. Nothing is monkeypatched — a process
cannot be — so the isolation is the one a person gets from the same environment variables.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import sysconfig
from pathlib import Path

import pytest

# The installed command, where pip put it for this interpreter: bin/oo, or Scripts\oo.exe on Windows.
OO = Path(sysconfig.get_path("scripts")) / ("oo.exe" if os.name == "nt" else "oo")

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


# A memory is a card and a body (`_cards`), and a write without `when` and `why` is refused. Most tests
# here are about something else — scopes, supersession, the unread check — so the helper gives them
# a valid card unless the test names its own; the template has tests of its own that pass none.
CARD = {"when": "In the case this test sets up.", "why": "The test needs a memory the store will accept."}


def carded(name, arguments):
    if name in ("remember", "merge") and not any(k in arguments for k in ("when", "why", "not", "do", "steps")) \
            and not str(arguments.get("text", "")).lstrip().lower().startswith(("when:", "not:", "do:", "why:")):
        return {**CARD, **arguments}
    return arguments


def call(name, cwd, env, **arguments):
    """One tool of the agent surface. A refusal is a result, so a non-zero exit is a test failure."""
    arguments = carded(name, arguments)
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


class Serving:
    """The installed `oo enact mcp` as a subprocess, spoken to the way an agent host speaks to it:
    one JSON line in, one JSON line out, over its stdin and stdout. Nothing here is a stand-in —
    this is the command registered with the host."""

    def __init__(self, cwd, env, root=None):
        self.proc = subprocess.Popen([str(OO), "enact", "mcp", *(["--root", str(root)] if root else [])],
                                     cwd=str(cwd), env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, bufsize=1)
        self.id = 0
        self.notifications = []

    def send(self, method, **params):
        """A request, and the reply that carries its id — notifications on the way are kept."""
        self.id += 1
        self.proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.id, "method": method,
                                          "params": params}) + "\n")
        self.proc.stdin.flush()
        while True:
            line = self.proc.stdout.readline()
            assert line, f"the server stopped before answering {method}"
            message = json.loads(line)
            if message.get("id") == self.id:
                return message
            self.notifications.append(message)

    def raw(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        return json.loads(self.proc.stdout.readline())

    def tool(self, name, **arguments):
        reply = self.send("tools/call", name=name, arguments=arguments)
        result = reply["result"]
        assert not result.get("isError"), result["content"][0]["text"]
        return json.loads(result["content"][0]["text"])

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=30)
        return self.proc.stderr.read()


@pytest.fixture
def serving(project, env):
    server = Serving(project, env)
    server.send("initialize", protocolVersion="2025-06-18", capabilities={})
    yield server
    server.close()


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
        for args in (("status",), ("vocabulary",), ("call", "read", "--json", json.dumps({"hard": TAGS}))):
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


class TestOverTheRealProtocol:
    """The surface an agent host actually uses: `oo enact mcp` as a subprocess, JSON-RPC over its
    pipes. Everything here needs the loop, so none of it is reachable through `oo enact call`."""

    def test_it_introduces_itself_and_offers_the_tools_it_dispatches(self, serving):
        started = serving.send("initialize", protocolVersion="2025-03-26", capabilities={})
        assert started["result"]["protocolVersion"] == "2025-03-26"
        listed = {tool["name"] for tool in serving.send("tools/list")["result"]["tools"]}
        assert {"read", "remember", "link", "unlink", "cite", "session"} <= listed
        for name in sorted(listed):                       # every offered tool is dispatchable
            reply = serving.send("tools/call", name=name, arguments={})
            text = reply["result"]["content"][0]["text"]
            assert "unknown tool" not in text, name

    def test_one_process_is_one_session_and_review_gathers_it(self, serving, seeded):
        opened = serving.tool("session")["session"]
        serving.tool("read", hard=TAGS)
        seen = [seeded]
        for title in ("Frost Ward", "Absorb"):
            made = serving.tool("remember", **CARD, title=title, text=f"{title} is a case.",
                                tags=TAGS + ["kind=example"], reason="a case", seen=seen)
            assert made["ok"], made
            seen.append(made["memory"])
        reviewed = serving.tool("review")
        assert reviewed["session"] == opened                      # the process never opened a second
        assert [m["title"] for m in reviewed["written"]] == ["Frost Ward", "Absorb"]

    def test_a_refusal_reads_the_same_here_as_through_the_command(self, serving, project, env, seeded, store):
        was = fingerprint(store)
        over_pipes = serving.tool("link", memory=seeded, tag="task=design", reason="already there")
        through_cli = call("link", project, env, memory=seeded, tag="task=design", reason="already there")
        assert over_pipes["refused"] == through_cli["refused"] == "already carried"
        assert over_pipes["message"] == through_cli["message"]
        assert fingerprint(store) == was

    def test_a_line_that_is_not_json_is_answered_and_the_server_serves_on(self, serving, seeded):
        """Only the loop can be wrong about this, and getting it wrong ends the session: a host that
        sends one bad line would find the server gone rather than complaining."""
        broken = serving.raw("{ this is not json")
        assert broken["error"]["code"] == -32700 and broken["id"] is None
        assert serving.tool("read", hard=TAGS)["corpus"] == 1      # still answering

    def test_the_banner_goes_to_stderr_so_stdout_carries_only_the_protocol(self, serving, seeded):
        serving.tool("read", hard=TAGS)
        noise = [m for m in serving.notifications if "jsonrpc" not in m]
        assert noise == []
        assert "serving" in serving.close()


class TestSeveralCallsAsOneSession:
    """A process is a session. That is right for the MCP server, which is one process for one
    conversation, and wrong for a script, where it means every memory lands in a session of its own
    and `review` — what a consolidation starts from — can gather none of them."""

    def test_without_a_session_each_call_is_its_own_and_review_sees_one_write(self, project, env, seeded):
        call("read", project, env, hard=TAGS)
        made = call("remember", project, env, title="Frost Ward", text="Typed resistance for a round.",
                    tags=TAGS + ["kind=example"], reason="a case", seen=[seeded])
        assert made["ok"], made
        assert [m["title"] for m in call("review", project, env)["written"]] == []   # a fresh session wrote nothing

    def test_naming_one_gathers_the_writes_of_several_processes(self, project, env, seeded):
        opened = call("session", project, env)["session"]
        where = ["call", "read", "--json", json.dumps({"hard": TAGS}), "--session", str(opened)]
        assert oo(*where, cwd=project, env=env).returncode == 0
        seen = [seeded]
        for title, text in (("Frost Ward", "Typed resistance for a round."), ("Absorb", "It converts damage.")):
            done = oo("call", "remember", "--session", str(opened), "--json",
                      json.dumps({"when": "In the case this test sets up.", "why": "The test needs a memory the store will accept.", "title": title, "text": text, "tags": TAGS + ["kind=example"],
                                  "reason": "a case", "seen": seen}), cwd=project, env=env)
            made = json.loads(done.stdout)
            assert made["ok"], done.stdout
            seen.append(made["memory"])
        reviewed = json.loads(oo("call", "review", "--session", str(opened), "--json", "{}",
                                 cwd=project, env=env).stdout)
        assert reviewed["session"] == opened
        assert [m["title"] for m in reviewed["written"]] == ["Frost Ward", "Absorb"]


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


class TestAcceptingAConceptTheVocabularyLacks:
    """A write that meets a concept with no tag proposes it; a person accepts it (overview §4.6, "During
    work"): the value joins the vocabulary with its reviewed entry, and every memory that asked for it
    gains the tag. `oo enact accept` listed such a concept and told the person to add it to a pack file
    by hand, then accept the pack again — an agent passed the owner `oo enact accept --root <store>` as
    the way to accept one, and it accepted nothing (2026-09-30)."""

    POTION = {"concept": "potion", "dimension": "artifact", "brief": "A potion.",
              "when": "The knowledge is about a potion.", "when_not": "Not a spell.", "example": "A healing potion."}

    @staticmethod
    def _asking(project, env, seeded, proposal):
        made = call("remember", project, env, title="Potions are one action", text="Drinking one costs an action.",
                    tags=TAGS + ["kind=principle"], reason="a rule the potions follow", seen=[seeded],
                    proposals=[proposal])
        assert made["ok"], made
        return made["memory"]

    def test_one_command_accepts_it_and_the_memory_that_asked_carries_it(self, project, env, store, seeded):
        asker = self._asking(project, env, seeded, self.POTION)
        listed = oo("accept", cwd=project, env=env)
        assert "potion" in listed.stdout and "oo enact accept potion" in listed.stdout, listed.stdout
        done = oo("accept", "potion", "--yes", cwd=project, env=env)
        assert done.returncode == 0, done.stderr
        assert "artifact=potion" in done.stdout
        assert "now carried by" in done.stdout and "Potions are one action" in done.stdout   # read back, not counted
        values = [v["tag"] for d in call("vocabulary", project, env, dimension="artifact")["dimensions"] for v in d["values"]]
        assert "artifact=potion" in values
        assert "artifact=potion" in call("show", project, env, memory=asker)["tags"]
        assert "nothing is waiting" in oo("accept", cwd=project, env=env).stdout
        pack = (store / "taxonomy" / "pack-dnd.yaml").read_text(encoding="utf-8")
        assert "potion:" in pack and "# `pack: dnd` is itself the value domain=dnd" in pack   # the person's comments stay

    def test_a_value_for_a_dimension_the_pack_does_not_extend_yet(self, project, env, store, seeded):
        balance = {"concept": "balance", "dimension": "task", "brief": "Weighing a thing against its peers.",
                   "when": "The knowledge compares power across options.", "when_not": "Not a rules question.",
                   "example": "Is this spell stronger than Fireball?"}
        asker = self._asking(project, env, seeded, balance)
        done = oo("accept", "balance", "--yes", cwd=project, env=env)
        assert done.returncode == 0, done.stderr
        assert "task=balance" in call("show", project, env, memory=asker)["tags"]
        kept = call("vocabulary", project, env, dimension="artifact")["dimensions"]
        assert "artifact=spell" in [v["tag"] for d in kept for v in d["values"]]      # the rest of the pack is untouched

    def test_read_one_at_a_time_enter_accepts(self, project, env, seeded):
        asker = self._asking(project, env, seeded, self.POTION)
        walked = oo("accept", "--all", cwd=project, env=env, stdin="\n")
        assert walked.returncode == 0, walked.stderr
        assert "A potion." in walked.stdout and "Potions are one action" in walked.stdout   # the entry, and who asked, by title
        assert "artifact=potion" in call("show", project, env, memory=asker)["tags"]

    def test_the_store_named_twice_is_listed_once(self, project, env, store, seeded):
        self._asking(project, env, seeded, self.POTION)
        same = {**env, "PYGIM_ENACT_GLOBAL": str(store)}      # the global store is the store --root names
        listed = oo("accept", "--root", str(store), cwd=project, env=same)
        assert listed.stdout.count("oo enact accept potion") == 1, listed.stdout


class TestShowingTheVocabulary:
    """A person reads the store's vocabulary with `oo enact vocabulary`, and sees what an agent's
    `vocabulary` call returns: the same tool answers both, so the two can never disagree. Debith,
    2026-10-02: "oo needs a command to show its current vocabulary" — until then the only way was
    `oo enact call vocabulary`, a page of JSON."""

    def test_every_dimension_and_value_with_its_brief_under_the_version(self, project, env):
        index = call("vocabulary", project, env)
        shown = oo("vocabulary", cwd=project, env=env)
        assert shown.returncode == 0, shown.stderr
        assert index["version"][:12] in shown.stdout
        flat = " ".join(shown.stdout.split())                          # columns and wrapping aside
        for dimension in index["dimensions"]:
            assert f"{dimension['name']} {dimension['role']}" in flat, dimension["name"]
            for tag, brief in dimension["values"].items():
                assert f"{tag} {' '.join(brief.split())}" in flat, tag
        assert "artifact=spell" in shown.stdout                       # the pack's values, not only the base

    def test_a_dimension_by_name_shows_each_entry_in_full_with_its_request_rule(self, project, env):
        task = call("vocabulary", project, env, dimension="task")["dimensions"][0]
        shown = oo("vocabulary", "task", cwd=project, env=env)
        assert shown.returncode == 0, shown.stderr
        flat = " ".join(shown.stdout.split())
        implement = next(v for v in task["values"] if v["tag"] == "task=implement")
        assert " ".join(implement["entry"]["when"].split()) in flat
        assert " ".join(implement["request"]["give_if"].split()) in flat
        assert ", ".join(implement["request"]["words"]) in flat
        assert "artifact=spell" not in shown.stdout                   # one dimension, not the rest

    def test_an_unknown_dimension_fails_and_names_the_ones_there_are(self, project, env):
        shown = oo("vocabulary", "colour", cwd=project, env=env)
        assert shown.returncode != 0
        assert "colour" in shown.stderr and "task" in shown.stderr, shown.stderr

    def test_request_prints_the_guide_exactly_as_an_agent_receives_it(self, project, env):
        shown = oo("vocabulary", "--request", cwd=project, env=env)
        assert shown.returncode == 0, shown.stderr
        assert shown.stdout.strip() == call("vocabulary", project, env)["request"].strip()

    def test_json_is_the_tool_result_for_programs(self, project, env):
        shown = oo("vocabulary", "task", "--json", cwd=project, env=env)
        assert shown.returncode == 0, shown.stderr
        assert json.loads(shown.stdout) == call("vocabulary", project, env, dimension="task")

    def test_scope_reads_another_store_this_machine_holds(self, project, env, tmp_path):
        place = tmp_path / "global-store"
        wide = {**env, "PYGIM_ENACT_GLOBAL": str(place)}
        made = oo("setup", "--global", "--path", str(place), "--no-register", cwd=project, env=wide)
        assert made.returncode == 0, made.stderr
        shown = oo("vocabulary", "--scope", "global", cwd=project, env=wide)
        assert shown.returncode == 0, shown.stderr
        assert "task=implement" in shown.stdout
        assert "artifact=spell" not in shown.stdout                   # the project's pack is not the global store's
        assert "artifact=spell" in oo("vocabulary", cwd=project, env=wide).stdout

    def test_showing_it_writes_nothing(self, project, env, store, seeded):
        before = fingerprint(store)
        for args in ((), ("task",), ("--request",), ("--json",)):
            assert oo("vocabulary", *args, cwd=project, env=env).returncode == 0, args
        assert fingerprint(store) == before
