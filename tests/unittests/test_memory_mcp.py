"""The memory MCP server: JSON-RPC 2.0 over stdio, one message per line.

In-process tests drive MemoryServer.handle; one test runs the real `oo memory
mcp` command over pipes, the way an agent host does.
"""
from __future__ import annotations

import json
import subprocess
import sys

import pytest

from _pygim._mcp.memory import TOOLS, MemoryServer
from pygim.memory import Memory

TAGS = ["domain=any", "artifact=any", "task=design", "kind=principle"]


@pytest.fixture
def server(tmp_path):
    root = tmp_path / "repo"
    Memory.init(str(root))
    return MemoryServer(Memory(str(root)))


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
        assert [p["name"] for p in prompts] == ["consolidate"]
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
        assert review["written"][0]["generalised_by"] == [g["memory"]]

    def test_a_missing_argument_is_a_tool_error_not_a_crash(self, server):
        err, text = call(server, "read")
        assert err and "hard" in text

    def test_an_unknown_tool_is_a_tool_error(self, server):
        err, text = call(server, "forget")
        assert err and "unknown tool" in text


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
