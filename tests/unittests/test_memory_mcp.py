"""The memory MCP server: JSON-RPC 2.0 over stdio, one message per line.

In-process tests drive MemoryServer.handle; one test runs the real `oo memory
mcp` command over pipes, the way an agent host does.
"""
from __future__ import annotations

import io
import json
import subprocess
import sys
from pathlib import Path

import pytest

from _pygim._mcp import _stores
from _pygim._mcp.memory import TOOLS, MemoryServer
from pygim.memory import Memory

TAGS = ["domain=any", "artifact=any", "task=design", "kind=principle"]
SHOP = """\
pack: shop
entry: {brief: The shop front., when: About the shop front., when_not: Not another project., example: The basket.}
dimensions:
  area:
    role: soft
    weight: 1.0
    entry: {brief: Which part of the shop., full: The part of the shop the knowledge is about., when: It holds for one part., when_not: Not the kind of thing made., example: The checkout.}
    values:
      basket: {entry: {brief: Holding items before paying., when: About the basket., when_not: Not paying., example: Adding an item.}}
"""


@pytest.fixture(autouse=True)
def no_machine_state(tmp_path, monkeypatch):
    """Nothing here reads this machine's own stores: a server merges the global store into standing
    knowledge, and a test that found the developer's would pass or fail by what is in it."""
    monkeypatch.setenv("PYGIM_MEMORY_GLOBAL", str(tmp_path / "no-global-store"))
    monkeypatch.setenv("GIT_CONFIG_GLOBAL", str(tmp_path / "gitconfig"))
    monkeypatch.setenv("GIT_CONFIG_NOSYSTEM", "1")
    monkeypatch.setattr(_stores, "user_data_dir", lambda: tmp_path / "user-data")


@pytest.fixture
def server(tmp_path):
    root = tmp_path / "repo"
    Memory.init(str(root))
    return MemoryServer(Memory(str(root)), cwd=tmp_path)   # cwd matters: stores are discovered around it


def call(server, name, **arguments):
    resp = server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": name, "arguments": arguments}})
    result = resp["result"]
    return result["isError"], (result["content"][0]["text"] if result["isError"] else json.loads(result["content"][0]["text"]))


class TestProtocol:
    def test_initialize_echoes_the_version_and_offers_tools(self, server):
        resp = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                              "params": {"protocolVersion": "2025-03-26", "capabilities": {}}})
        assert resp["result"]["protocolVersion"] == "2025-03-26"
        assert resp["result"]["capabilities"] == {"tools": {"listChanged": False}, "prompts": {"listChanged": False}}
        assert "read" in resp["result"]["instructions"]

    def test_standing_knowledge_reaches_every_session_without_a_read(self, tmp_path):
        root = tmp_path / "standing"
        Memory.init(str(root))
        m = Memory(str(root))
        pref = m.remember(title="Prefer templates", text="Template it,\neven with one use.",
                          tags=["domain=any", "artifact=any", "task=design", "kind=preference"])
        m.remember(title="Releasing", text="1 tag\n2 push", tags=["domain=any", "artifact=any", "task=design", "kind=procedure"],
                   seen=[pref["memory"]])
        receipts = len(m.receipts())
        server = MemoryServer(Memory(str(root)), cwd=tmp_path)
        text = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]["instructions"]
        assert f"- {pref['memory']} Prefer templates" in text and "even with one use" not in text   # an index: titles only
        err, info = call(server, "session")
        standing = info["standing"]
        assert [(p["title"], p["text"]) for p in standing["preferences"]] == [("Prefer templates", "Template it,\neven with one use.")]
        assert [(p["title"], p["where"]) for p in standing["procedures"]] == [("Releasing", "artifact=any task=design")]
        assert len(Memory(str(root)).receipts()) == receipts                       # nothing recorded as read

    def test_the_instructions_fit_what_a_host_keeps_however_much_there_is_to_say(self, tmp_path):
        """Claude Code keeps 2,048 characters of a server's instructions and drops the rest silently.
        Standing knowledge once went there in full — 11,900 characters sent, 2,048 received, so a
        preference that would have changed a recommendation never arrived. The instructions carry an
        index now, and the count of what it leaves out; `session` carries every text."""
        from _pygim._mcp.memory import INSTRUCTIONS, INSTRUCTIONS_CAP

        assert len(INSTRUCTIONS) < 1500 and INSTRUCTIONS_CAP <= 2048
        root = tmp_path / "many"
        Memory.init(str(root))
        m = Memory(str(root))
        seen = []
        for n in range(30):
            w = m.remember(title=f"Preference number {n} with a long and descriptive title that goes on", text=f"{n} " + "x" * 900,
                           tags=["domain=any", "artifact=any", "task=design", "kind=preference"], seen=seen)
            seen.append(w["memory"])
        server = MemoryServer(Memory(str(root)), cwd=tmp_path)
        text = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]["instructions"]
        assert len(text) <= INSTRUCTIONS_CAP
        assert "Preference number 29" in text and "Preference number 0 " not in text   # newest first
        assert "more, in `session`" in text                                             # and it says what it left out
        assert len(call(server, "session")[1]["standing"]["preferences"]) == 30         # nothing is lost, only moved

    def test_a_store_with_nothing_standing_adds_nothing(self, server):
        text = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]["instructions"]
        assert "Standing knowledge" not in text

    def test_notifications_are_never_answered(self, server):
        assert server.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}) is None

    def test_tools_list_and_schemas(self, server):
        tools = server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})["result"]["tools"]
        names = [t["name"] for t in tools]
        assert names == [t["name"] for t in TOOLS]
        assert {"session", "vocabulary", "read", "remember", "learn", "merge", "show", "proposals"} <= set(names)
        remember = next(t for t in tools if t["name"] == "remember")
        assert remember["inputSchema"]["required"] == ["title", "text", "tags"]

    def test_consolidate_is_offered_as_a_prompt(self, server):
        prompts = server.handle({"jsonrpc": "2.0", "id": 4, "method": "prompts/list"})["result"]["prompts"]
        assert [p["name"] for p in prompts] == ["consolidate", "prepare-vocabulary", "seed-memories"]
        got = server.handle({"jsonrpc": "2.0", "id": 5, "method": "prompts/get", "params": {"name": "consolidate"}})["result"]
        text = got["messages"][0]["content"]["text"]
        assert got["messages"][0]["role"] == "user" and "`review`" in text and "`generalises`" in text

    def test_an_unknown_prompt_is_a_json_rpc_error(self, server):
        resp = server.handle({"jsonrpc": "2.0", "id": 6, "method": "prompts/get", "params": {"name": "close"}})
        assert resp["error"]["code"] == -32602

    def test_unknown_method_is_a_json_rpc_error(self, server):
        resp = server.handle({"jsonrpc": "2.0", "id": 3, "method": "resources/list"})
        assert resp["error"]["code"] == -32601


class TestTools:
    def test_session_then_vocabulary(self, server):
        err, info = call(server, "session")
        assert not err and info["session"] >= 1 and info["version"] == 1
        err, vocab = call(server, "vocabulary")
        assert [d["name"] for d in vocab["dimensions"]] == ["domain", "artifact", "task", "kind", "tier"]

    def test_the_write_loop_through_tools(self, server):
        err, first = call(server, "remember", title="Prefer templates", text="General, templated.", tags=TAGS)
        assert not err and first["ok"]
        err, refused = call(server, "remember", title="Read first", text="Read before writing.", tags=TAGS)
        assert refused["refused"] == "unread" and refused["facts"][0].startswith(first["memory"])
        err, read = call(server, "read", hard=["task=design"])
        seen = [m["memory"] for m in read["memories"]]
        err, second = call(server, "remember", title="Read first", text="Read before writing.", tags=TAGS, seen=seen)
        assert second["ok"]
        err, shown = call(server, "show", memory=second["memory"])
        assert shown["seen"] == seen
        assert shown["author"] == "agent"

    def test_consolidating_through_tools(self, server):
        call(server, "session")
        err, a = call(server, "remember", title="Templates in each", text="each is templated.", tags=TAGS)
        err, b = call(server, "remember", title="Templates in pathlike", text="pathlike is templated.", tags=TAGS,
                      seen=[a["memory"]])
        err, review = call(server, "review")
        assert not err and [w["title"] for w in review["written"]] == ["Templates in each", "Templates in pathlike"]
        err, g = call(server, "remember", title="Prefer templates", text="Template it even with one use.", tags=TAGS,
                      generalises=[a["memory"], b["memory"]])
        assert not err and g["ok"]
        err, review = call(server, "review")
        assert review["written"][0]["generalised_by"] == [g["memory"]] and review["written"][2]["accepted"] is False
        err, lessons = call(server, "lessons", text="## Patterns written\n\nTemplates everywhere.")
        assert not err and lessons["report"].endswith(f"session-{review['session']}.md")

    def test_a_read_narrows_by_term_and_asks_for_learn(self, server):
        err, a = call(server, "remember", title="Mounted combat", text="A mount acts on your initiative.", tags=TAGS)
        call(server, "remember", title="Hiding", text="Hide takes an action.", tags=TAGS, seen=[a["memory"]])
        err, read = call(server, "read", hard=["task=design"], term="mount")
        assert not err and [m["title"] for m in read["memories"]] == ["Mounted combat"] and read["term_matched"] == 1
        assert "learn" in read["next"] and read["skipped"] == 0 and "coverage" in read
        err, empty = call(server, "read", hard=["task=design"], term="invisible")
        assert empty["memories"] == [] and "next" not in empty

    def test_a_read_names_the_preferences_it_did_not_place(self, server):
        """With no soft tags a read ranks by age, so the newest preference is last and `max` cuts it
        — the one most likely to be unknown to the reader. It is named, not paid for."""
        err, a = call(server, "remember", title="An old principle", text="Old.", tags=TAGS)
        err, b = call(server, "remember", title="Design for the end state", text="Not the current one.",
                      tags=["domain=any", "artifact=any", "task=design", "kind=preference"], seen=[a["memory"]])
        err, read = call(server, "read", hard=["task=design"], max=1)
        assert [m["title"] for m in read["memories"]] == ["An old principle"] and read["skipped"] == 1
        assert [(s["memory"], s["title"]) for s in read["standing"]] == [(b["memory"], "Design for the end state")]
        assert "standing" not in call(server, "read", hard=["task=design"], max=5)[1]    # placed, so nothing to name

    def test_changes_return_the_tags_and_a_seed_skips_the_unread_check(self, server):
        err, a = call(server, "remember", title="Rule one", text="One.", tags=TAGS)
        err, b = call(server, "remember", title="Rule two", text="Two.", tags=TAGS, seed=True)
        assert b["ok"] and call(server, "show", memory=b["memory"])[1]["origin"] == "seed"
        err, unlinked = call(server, "unlink", memory=a["memory"], tag="kind=principle", reason="not a principle")
        assert unlinked["ok"] and "kind=principle" not in unlinked["tags"] and unlinked["head"]

    def test_the_next_call_says_when_the_vocabulary_changed(self, server):
        call(server, "session")
        before = server.memory.taxonomy
        (Path(server.memory.root) / "taxonomy" / "pack-shop.yaml").write_text(SHOP, encoding="utf-8")
        err, read = call(server, "read", hard=["task=design"])
        assert read["vocabulary_changed"]["from"] == before and read["vocabulary_changed"]["to"] == server.memory.taxonomy
        assert "vocabulary_changed" not in call(server, "read", hard=["task=design"])[1]

    def test_the_agent_has_no_way_to_accept(self, server):
        names = {t["name"] for t in TOOLS}
        assert "accept" not in names and "lessons" in names
        err, text = call(server, "accept", memory="#0")
        assert err and "unknown tool" in text

    def test_a_missing_argument_is_a_tool_error_not_a_crash(self, server):
        err, text = call(server, "read")
        assert err and "hard" in text

    def test_an_unknown_tool_is_a_tool_error(self, server):
        err, text = call(server, "forget")
        assert err and "unknown tool" in text


class TestReloading:
    """A server holds the code it started with; `oo memory reload` asks it to restart into what is
    on disk, between messages, keeping the host's pipes (03 §9.1.3)."""

    def test_a_result_says_once_that_the_code_moved_on(self, server):
        assert not server.stale()
        server._code = ("an older pygim", ())
        assert server.stale()
        err, info = call(server, "session")
        assert "`oo memory reload`" in info["server_stale"]["next"]
        assert "server_stale" not in call(server, "session")[1]        # said once, not on every call

    def test_a_signal_or_a_marker_asks_for_the_reload(self, server):
        assert not server._reload_asked()
        (Path(server.memory.root) / "local" / "reload").write_text("asked", encoding="utf-8")
        assert server._reload_asked()
        (Path(server.memory.root) / "local" / "reload").unlink()
        assert not server._reload_asked()
        server.signalled = True                                        # what the SIGHUP handler sets
        assert server._reload_asked()

    def test_a_reloaded_server_resumes_its_session_and_says_the_tools_may_have_moved(self, tmp_path, monkeypatch):
        root = tmp_path / "repo"
        Memory.init(str(root))
        monkeypatch.setenv("PYGIM_MEMORY_SESSION", "7")
        monkeypatch.setenv("PYGIM_MEMORY_RELOADED", "1")
        resumed = MemoryServer(Memory(str(root)))
        assert resumed.session == 7                                    # the audit log keeps one session
        out = io.StringIO()
        resumed.serve(io.StringIO(""), out)
        assert json.loads(out.getvalue())["method"] == "notifications/tools/list_changed"


def test_the_real_command_over_pipes(tmp_path):
    root = tmp_path / "repo"
    Memory.init(str(root))
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
         "params": {"name": "remember", "arguments": {"title": "One", "text": "first", "tags": TAGS}}},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "read", "arguments": {"hard": ["task=design"]}}},
    ]
    proc = subprocess.run(
        [sys.executable, "-c", "from pygim.__main__ import cli_oo; cli_oo()", "memory", "mcp", "--root", str(root)],
        input="\n".join(json.dumps(m) for m in msgs) + "\n", capture_output=True, text=True, timeout=120)
    lines = [json.loads(line) for line in proc.stdout.splitlines()]
    assert [r["id"] for r in lines] == [1, 2, 3]                      # nothing else on stdout
    read = json.loads(lines[2]["result"]["content"][0]["text"])
    assert [m["title"] for m in read["memories"]] == ["One"]
    assert "serving" in proc.stderr
