"""The memory MCP server: JSON-RPC 2.0 over stdio, one message per line.

In-process tests drive EnactServer.handle; one test runs the real `oo enact
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
from _pygim._mcp.enact import TOOLS, EnactServer
from pygim.enact import Enact

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
    monkeypatch.setenv("PYGIM_ENACT_GLOBAL", str(tmp_path / "no-global-store"))
    monkeypatch.setenv("GIT_CONFIG_GLOBAL", str(tmp_path / "gitconfig"))
    monkeypatch.setenv("GIT_CONFIG_NOSYSTEM", "1")
    monkeypatch.setattr(_stores, "user_data_dir", lambda: tmp_path / "user-data")


@pytest.fixture
def server(tmp_path):
    root = tmp_path / "repo"
    Enact.init(str(root))
    return EnactServer(Enact(str(root)), cwd=tmp_path)   # cwd matters: stores are discovered around it


def call(server, name, **arguments):
    resp = server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": name, "arguments": arguments}})
    result = resp["result"]
    return result["isError"], (result["content"][0]["text"] if result["isError"] else json.loads(result["content"][0]["text"]))


class TestAttachingAndQuoting:
    """`link` and `unlink` move either of a memory's two attachments — a tag or a citation — because
    that is one job on two targets. A locator is checked against the store's own inventory before it
    is recorded, and the passage comes back so the writer can read what they just cited."""

    @pytest.fixture
    def sourced(self, server, tmp_path):
        """A store that carries its own source, as the ddd one does: no separate project checkout."""
        root = Path(server.memory.root)
        (root / "sources").mkdir(exist_ok=True)
        (root / "sources" / "book.txt").write_text("one\ntwo\nthree\nfour\n", encoding="utf-8")
        (root / "sources" / "inventory.yaml").write_text(
            'book:\n  kind: text\n  path: "sources/book.txt"\n', encoding="utf-8")
        err, made = call(server, "remember", title="A rule", text="It holds.", tags=TAGS, reason="a rule")
        assert not err and made["ok"]
        return made["memory"]

    def test_a_locator_is_added_and_the_passage_comes_back_to_be_read(self, server, sourced):
        err, out = call(server, "link", memory=sourced, cite="book:L2-3", reason="where it is said")
        assert not err and out["ok"] and out["passage"] == "two\nthree"
        err, shown = call(server, "show", memory=sourced)
        assert shown["cites"] == ["book:L2-3"] and shown["tags"] == TAGS       # the tags are untouched
        err, out = call(server, "unlink", memory=sourced, cite="book:L2-3", reason="wrong lines")
        assert not err and out["ok"]
        assert call(server, "show", memory=sourced)[1]["cites"] == []

    def test_a_locator_that_does_not_resolve_is_refused_and_nothing_is_recorded(self, server, sourced):
        err, out = call(server, "link", memory=sourced, cite="ch9:L1", reason="a guess")
        assert out["refused"] == "unknown locator" and "book" in out["message"]     # and it names what it has
        err, past = call(server, "link", memory=sourced, cite="book:L9-12", reason="past the end")
        assert past["refused"] == "unknown locator" and "runs past its end" in past["message"]
        err, shape = call(server, "link", memory=sourced, cite="book 2", reason="not a locator")
        assert shape["refused"] == "unknown locator"
        assert call(server, "show", memory=sourced)[1]["cites"] == []               # none of the three landed

    def test_naming_both_a_tag_and_a_cite_or_neither_is_refused(self, server, sourced):
        err, both = call(server, "link", memory=sourced, tag="kind=example", cite="book:L1", reason="r")
        assert both["refused"] == "one of tag or cite"
        err, neither = call(server, "link", memory=sourced, reason="r")
        assert neither["refused"] == "one of tag or cite"
        assert call(server, "show", memory=sourced)[1]["tags"] == TAGS

    def test_cite_reaches_another_store_that_carries_its_own_sources(self, server, tmp_path):
        """Without a scope the tool resolved paths only under *this* project, so a store built
        entirely from its own documents — the ddd one — could not be quoted at all, and its
        citations went unverified. Two of that store's eight locators were wrong because of it."""
        other = tmp_path / "user-data" / "ddd"
        Enact.init(str(other))
        (other / "sources").mkdir(exist_ok=True)
        (other / "sources" / "ref.txt").write_text("alpha\nbeta\ngamma\n", encoding="utf-8")
        (other / "sources" / "inventory.yaml").write_text(
            'ref:\n  kind: text\n  path: "sources/ref.txt"\n', encoding="utf-8")
        err, out = call(server, "cite", path="sources/ref.txt", line=2, lines=2, scope="ddd")
        assert not err, out
        assert out["locator"] == "ref:L2-3" and out["text"] == "beta\ngamma"
        err, blind = call(server, "cite", path="sources/ref.txt", line=2)
        assert err                                        # the project scope cannot see it at all


class TestProtocol:
    def test_initialize_echoes_the_version_and_offers_tools(self, server):
        resp = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                              "params": {"protocolVersion": "2025-03-26", "capabilities": {}}})
        assert resp["result"]["protocolVersion"] == "2025-03-26"
        assert resp["result"]["capabilities"] == {"tools": {"listChanged": False}, "prompts": {"listChanged": False}}
        assert "read" in resp["result"]["instructions"]

    def test_standing_knowledge_reaches_every_session_without_a_read(self, tmp_path):
        root = tmp_path / "standing"
        Enact.init(str(root))
        m = Enact(str(root))
        pref = m.remember(title="Prefer templates", text="Template it,\neven with one use.",
                          tags=["domain=any", "artifact=any", "task=design", "kind=preference"])
        m.remember(title="Releasing", text="1 tag\n2 push", tags=["domain=any", "artifact=any", "task=design", "kind=procedure"],
                   seen=[pref["memory"]])
        receipts = len(m.receipts())
        server = EnactServer(Enact(str(root)), cwd=tmp_path)
        text = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]["instructions"]
        assert f"- {pref['memory']} Prefer templates" in text and "even with one use" not in text   # an index: titles only
        err, info = call(server, "session")
        standing = info["standing"]
        assert [(p["title"], p["text"]) for p in standing["preferences"]] == [("Prefer templates", "Template it,\neven with one use.")]
        assert [(p["title"], p["where"]) for p in standing["procedures"]] == [("Releasing", "artifact=any task=design")]
        assert len(Enact(str(root)).receipts()) == receipts                       # nothing recorded as read

    def test_the_instructions_fit_what_a_host_keeps_however_much_there_is_to_say(self, tmp_path):
        """Claude Code keeps 2,048 characters of a server's instructions and drops the rest silently.
        Standing knowledge once went there in full — 11,900 characters sent, 2,048 received, so a
        preference that would have changed a recommendation never arrived. The instructions carry an
        index now, and the count of what it leaves out; `session` carries every text."""
        from _pygim._mcp.enact import INSTRUCTIONS, INSTRUCTIONS_CAP

        assert len(INSTRUCTIONS) < 1500 and INSTRUCTIONS_CAP <= 2048
        root = tmp_path / "many"
        Enact.init(str(root))
        m = Enact(str(root))
        seen = []
        for n in range(30):
            w = m.remember(title=f"Preference number {n} with a long and descriptive title that goes on", text=f"{n} " + "x" * 900,
                           tags=["domain=any", "artifact=any", "task=design", "kind=preference"], seen=seen)
            seen.append(w["memory"])
        server = EnactServer(Enact(str(root)), cwd=tmp_path)
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

    def test_learn_survives_an_extension_older_than_this_server(self, server):
        """A study's every `learn` failed with TypeError because the server passed a verdict to an
        installed pygim that had none (feedback, 2026-09-21). A tool must degrade, not break."""
        err, a = call(server, "remember", title="A rule", text="Rule.", tags=TAGS)
        real = server.memory

        class Older:                                               # the extension as it was before verdicts
            def __init__(self, inner):
                self._inner = inner

            def learn(self, memory, *, tag="", reason="", session=0):
                return self._inner.learn(memory, tag=tag, reason=reason, session=session)

            def __getattr__(self, name):
                return getattr(self._inner, name)

        server._memory = Older(real)
        err, told = call(server, "learn", memory=a["memory"], verdict="misleading", reason="wrong here")
        assert not err and told["ok"] and "oo enact reload" in told["degraded"]
        assert call(server, "learn", memory=a["memory"])[1]["ok"]   # and it stops trying the verdict
        server._memory = real

    def test_a_verdict_goes_through_the_tool_and_a_read_asks_for_one(self, server):
        err, a = call(server, "remember", title="A rule", text="Rule.", tags=TAGS)
        err, read = call(server, "read", hard=["task=design"])
        assert "not_needed" in read["next"] and "misleading" in read["next"]
        err, told = call(server, "learn", memory=a["memory"], verdict="misleading", reason="it answered another question")
        assert not err and told["message"] == "recorded misleading"
        assert call(server, "show", memory=a["memory"])[1]["counters"]["misleading"] == 1
        learn = next(t for t in TOOLS if t["name"] == "learn")
        assert learn["inputSchema"]["properties"]["verdict"]["enum"] == ["useful", "not_needed", "misleading"]

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

    def test_the_mailbox_through_the_tools(self, server):
        err, posted = call(server, "post", text="Please finish the pathlike rebase.", kind="request", to="next")
        assert not err and posted["ok"] and posted["waiting"] == 1
        err, box = call(server, "mailbox")
        assert [(m["kind"], m["to"], m["author"]) for m in box] == [("request", "next", "agent")]
        err, closed = call(server, "post", text="Done, rebased.", resolves=posted["message"])
        assert closed["ok"] and call(server, "mailbox")[1] == []
        assert len(call(server, "mailbox", all=True)[1]) == 2
        err, info = call(server, "session")
        assert info["mailbox"] == []                                   # nothing open, so nothing is pressed on it
        assert "mailbox" in {t["name"] for t in TOOLS} and "post" in {t["name"] for t in TOOLS}

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
    """A server holds the code it started with; `oo enact reload` asks it to restart into what is
    on disk, between messages, keeping the host's pipes (03 §9.1.3)."""

    def test_a_result_says_once_that_the_code_moved_on(self, server):
        assert not server.stale()
        server._code = ("an older pygim", ())
        assert server.stale()
        err, info = call(server, "session")
        assert "`oo enact reload`" in info["server_stale"]["next"]
        assert "server_stale" not in call(server, "session")[1]        # said once, not on every call

    def test_a_signal_or_a_marker_asks_for_the_reload(self, server):
        assert not server._reload_asked()
        (Path(server.memory.root) / "local" / "reload").write_text("asked", encoding="utf-8")
        assert server._reload_asked()
        (Path(server.memory.root) / "local" / "reload").unlink()
        assert not server._reload_asked()
        server.signalled = True                                        # what the SIGHUP handler sets
        assert server._reload_asked()

    def test_it_refuses_to_exec_into_a_command_this_installation_no_longer_has(self, server, monkeypatch, capsys):
        """A reload re-execs the argv the server was started with. When `oo memory mcp` became
        `oo enact mcp`, every running server's argv named a command that had ceased to exist, so a
        reload would have exec'd into `No such command` and taken the server down. It refuses
        instead, and clears the marker so it does not refuse again on every message."""
        marker = Path(server.memory.root) / "local" / "reload"
        marker.write_text("asked", encoding="utf-8")
        monkeypatch.setattr(sys, "argv", ["oo", "memory", "mcp"])      # the name it was started under
        server.reload(io.StringIO())                                   # returns rather than exec'ing
        assert "not reloading" in capsys.readouterr().err
        assert not marker.exists() and not server._reload_asked()

    def test_a_reloaded_server_resumes_its_session_and_says_the_tools_may_have_moved(self, tmp_path, monkeypatch):
        root = tmp_path / "repo"
        Enact.init(str(root))
        monkeypatch.setenv("PYGIM_ENACT_SESSION", "7")
        monkeypatch.setenv("PYGIM_ENACT_RELOADED", "1")
        resumed = EnactServer(Enact(str(root)))
        assert resumed.session == 7                                    # the audit log keeps one session
        out = io.StringIO()
        resumed.serve(io.StringIO(""), out)
        assert json.loads(out.getvalue())["method"] == "notifications/tools/list_changed"


def test_the_real_command_over_pipes(tmp_path):
    root = tmp_path / "repo"
    Enact.init(str(root))
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
         "params": {"name": "remember", "arguments": {"title": "One", "text": "first", "tags": TAGS}}},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "read", "arguments": {"hard": ["task=design"]}}},
    ]
    proc = subprocess.run(
        [sys.executable, "-c", "from pygim.__main__ import cli_oo; cli_oo()", "enact", "mcp", "--root", str(root)],
        input="\n".join(json.dumps(m) for m in msgs) + "\n", capture_output=True, text=True, timeout=120)
    lines = [json.loads(line) for line in proc.stdout.splitlines()]
    assert [r["id"] for r in lines] == [1, 2, 3]                      # nothing else on stdout
    read = json.loads(lines[2]["result"]["content"][0]["text"])
    assert [m["title"] for m in read["memories"]] == ["One"]
    assert "serving" in proc.stderr
