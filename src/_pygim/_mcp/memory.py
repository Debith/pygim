"""pygim's problem-space memory as an MCP server over stdio.

The Model Context Protocol is JSON-RPC 2.0, one message per line on stdin and
stdout. This server answers ``initialize``, ``ping``, ``tools/list`` and
``tools/call``; each tool is one method of :class:`pygim.memory.Memory`, and
its result is that method's dict as JSON. The server holds the session number
and a turn counter itself, so an agent never has to pass them. Nothing but
protocol messages is ever written to stdout; diagnostics go to stderr.

The design is docs/design/memory/ — the write loop the tool descriptions teach
is the overview's §4.5.
"""
from __future__ import annotations

import json
import sys
from typing import Any, Callable, Dict, IO, List, Optional

PROTOCOL_VERSION = "2025-06-18"
SERVER_NAME = "pygim-memory"

INSTRUCTIONS = """\
A problem-space memory: knowledge is found by the kind of problem being solved,
not by similarity to the prompt.

1. Call `session` once, then `vocabulary`. Every tag is dimension=value from that
   list; each value's entry says when to use it and when not to.
2. Before working, `read` the problem space: hard tags filter (every hard
   dimension of the request), soft tags only order. Follow the procedure it
   returns first, if any.
3. When something outlives the task, decide against what you read: nothing
   covers it -> `remember` with supersedes empty; a memory covers it but says
   less or says it wrong -> `remember` with supersedes=[that memory]; a memory
   already says exactly this -> `learn` instead. Always pass the memories you
   read as `seen`. A concept with no tag goes in `proposals` — never force a tag.
4. A refusal is information: it names the facts (unread memories, the current
   head, the missing hard question). Act on them and try again.
5. Consolidate only when the user asks (the `consolidate` prompt): `review` what
   the session wrote, find a point several memories make, `remember` it once with
   `generalises` naming them, and record `lessons`. Nothing is retired, and a
   generalisation folds its instances only after the user accepts it themselves
   with `oo memory accept` — there is no tool for that, on purpose.
"""

CONSOLIDATE = """\
Consolidate what this session has written into memory so far.

1. Call `review` for the list of memories this session wrote, with their tags and
   whatever already generalises each one.
2. Read them together, and `read` the spaces they landed in: a generalisation looks
   before it writes like any memory, and an older memory may share the pattern too.
3. Look for a point that two or more of them make and none of them states alone.
   A memory already generalised needs nothing more, unless a new case widens that
   pattern — then write the wider version superseding the old generalisation.
4. For each pattern, `remember` it once: as general as its cases reach and no
   further; tags that cover every instance on every hard dimension (a dimension's
   `any` only if the pattern truly holds for every value); `generalises` naming the
   instances; `seen` naming what you read. Usually kind=principle; kind=procedure
   when the session kept repeating the same steps.
5. Record the lessons learnt with `lessons`, whatever you found — even "no pattern"
   is a lesson. Use three sections: *Patterns written* (each, and why its cases
   belong together); *Left as cases* (each, and why it did not fit); *Gaps* (details
   a pattern drops, cases that only half fit, contradictions, anything the
   vocabulary could not say). The service publishes them in
   reviews/session-<n>.md beside what it lists itself: the generalisations waiting
   for acceptance, any that claim `any`, the cases left, the proposals raised.
6. Tell the user the report's path, and that each generalisation folds its
   instances only after they accept it with `oo memory accept <key>` — you cannot
   accept it for them.
"""

PROMPTS: List[Dict[str, Any]] = [
    {
        "name": "consolidate",
        "description": "Read back what this session has written and state once, as a generalisation, "
                       "any pattern several memories share. Nothing is retired.",
        "arguments": [],
    },
]
_PROMPT_TEXT = {"consolidate": CONSOLIDATE}

_REF = {"type": "string", "description": "A memory: #n from a read or show, or at least 8 characters of its key."}
_TAGS = {"type": "array", "items": {"type": "string"}, "description": "Qualified tags, dimension=value, from the vocabulary."}
_REFS = {"type": "array", "items": _REF}


def _schema(properties: Dict[str, Any], required: Optional[List[str]] = None) -> Dict[str, Any]:
    return {"type": "object", "properties": properties, "required": required or [], "additionalProperties": False}


TOOLS: List[Dict[str, Any]] = [
    {
        "name": "session",
        "description": "Start here, once per session. Where the store stands, reviews waiting for a human, "
                       "and proposals waiting for the vocabulary. Then call `vocabulary`.",
        "inputSchema": _schema({}),
    },
    {
        "name": "vocabulary",
        "description": "The controlled vocabulary. Each dimension is one question with a closed list of answers; "
                       "role hard means it filters by default, soft means it only orders; each value's entry says "
                       "when to use it and when not to. Tag requests and memories with these exact names.",
        "inputSchema": _schema({}),
    },
    {
        "name": "read",
        "description": "Retrieve the working context for a problem space. `hard`: tags a memory must match — at least "
                       "one; several values of one dimension mean any of them. `soft`: tags that only order. Returns "
                       "the procedure for the artifact and task first (follow its steps), then ranked memories, each "
                       "with the tags that admitted and ranked it. Read before you write.",
        "inputSchema": _schema({
            "hard": _TAGS,
            "soft": _TAGS,
            "max": {"type": "integer", "minimum": 1, "description": "Most memories to return (default 8)."},
            "budget": {"type": "integer", "minimum": 0, "description": "Token budget; 0 is unbounded."},
        }, ["hard"]),
    },
    {
        "name": "remember",
        "description": "Record knowledge that outlives the task. Tag it from the vocabulary; every hard dimension must "
                       "be answered (dimension=any only if it holds for every value). Pass in `seen` every memory you "
                       "read in its space — a new memory is refused while unread ones exist there. To replace what a "
                       "memory says, pass it in `supersedes`. If a memory already says exactly this, call `learn` "
                       "instead. Concepts with no tag go in `proposals`, each with brief, when, when_not and example.",
        "inputSchema": _schema({
            "title": {"type": "string"},
            "text": {"type": "string"},
            "tags": _TAGS,
            "reason": {"type": "string", "description": "Why this outlives the task, or why it replaces what it supersedes."},
            "supersedes": _REFS,
            "generalises": {"type": "array", "items": _REF,
                            "description": "For a generalisation: the two or more heads whose shared pattern this states. "
                                           "They stay heads, count as read, and must be covered on every hard dimension."},
            "seen": _REFS,
            "cites": {"type": "array", "items": {"type": "string"}, "description": "Source locators, e.g. phb-2024-spells:L4459."},
            "proposals": {"type": "array", "items": _schema({
                "concept": {"type": "string"},
                "dimension": {"type": "string", "description": "The dimension it would be a value of; empty for a new dimension."},
                "brief": {"type": "string"}, "full": {"type": "string"}, "when": {"type": "string"},
                "when_not": {"type": "string"}, "example": {"type": "string"},
            }, ["concept", "brief", "when", "when_not", "example"])},
        }, ["title", "text", "tags"]),
    },
    {
        "name": "learn",
        "description": "Report that a memory helped. With a tag the memory does not carry, the report counts toward "
                       "linking that tag (three reports promote it). On a procedure with no tag, it records that its "
                       "steps held.",
        "inputSchema": _schema({"memory": _REF, "tag": {"type": "string"}, "reason": {"type": "string"}}, ["memory"]),
    },
    {
        "name": "merge",
        "description": "Two or more memories say one thing: write one text that joins them; it supersedes them all "
                       "and carries the union of their tags unless `tags` is given. The result names the kind of "
                       "recollection failure that made the duplicate.",
        "inputSchema": _schema({
            "memories": _REFS, "title": {"type": "string"}, "text": {"type": "string"},
            "reason": {"type": "string"}, "tags": _TAGS,
        }, ["memories", "title", "text", "reason"]),
    },
    {
        "name": "link",
        "description": "Add a tag to a memory, with a reason.",
        "inputSchema": _schema({"memory": _REF, "tag": {"type": "string"}, "reason": {"type": "string"}}, ["memory", "tag", "reason"]),
    },
    {
        "name": "unlink",
        "description": "Remove a tag from a memory, with a reason.",
        "inputSchema": _schema({"memory": _REF, "tag": {"type": "string"}, "reason": {"type": "string"}}, ["memory", "tag", "reason"]),
    },
    {
        "name": "retire",
        "description": "Take a memory out of retrieval, with a reason. It stays readable with `show`.",
        "inputSchema": _schema({"memory": _REF, "reason": {"type": "string"}}, ["memory", "reason"]),
    },
    {
        "name": "review",
        "description": "What this session has written so far, in order: each memory's tags, whether it is still a head, "
                       "and what already generalises it. Where a consolidation starts. `session` names an earlier one.",
        "inputSchema": _schema({"session": {"type": "integer", "minimum": 1, "description": "Default: this session."}}),
    },
    {
        "name": "lessons",
        "description": "Record the lessons learnt of a consolidation — patterns written, cases left and why, and every "
                       "gap you saw — and publish the session's report, reviews/session-<n>.md, for the user. "
                       "Returns the report's path. The user accepts each generalisation themselves.",
        "inputSchema": _schema({"text": {"type": "string",
                                         "description": "Markdown with three sections: Patterns written, Left as cases, Gaps."}},
                               ["text"]),
    },
    {
        "name": "show",
        "description": "One memory in full: its text, tags, lineage, what it saw, citations and counters.",
        "inputSchema": _schema({"memory": _REF}, ["memory"]),
    },
    {
        "name": "proposals",
        "description": "Concepts the vocabulary lacks, folded, with the memories that asked. A human accepts one by "
                       "adding it to a pack file under taxonomy/; the asking memories are then linked on the next start.",
        "inputSchema": _schema({}),
    },
]


class MemoryServer:
    """Answers MCP messages for one repository. `handle` takes one decoded
    message and returns the response to write, or None for a notification."""

    def __init__(self, memory: Any) -> None:
        self.memory = memory
        self.session: Optional[int] = None
        self.turn = 0
        self._calls: Dict[str, Callable[[Dict[str, Any]], Any]] = {
            "session": self._session,
            "vocabulary": lambda a: self.memory.vocabulary(),
            "read": lambda a: self.memory.read(a["hard"], a.get("soft", []), max=a.get("max", 8),
                                               budget=a.get("budget", 0), session=self._session_no()),
            "remember": self._remember,
            "learn": lambda a: self.memory.learn(a["memory"], tag=a.get("tag", ""), reason=a.get("reason", ""),
                                                 session=self._session_no()),
            "merge": lambda a: self.memory.merge(a["memories"], title=a["title"], text=a["text"], reason=a["reason"],
                                                 tags=a.get("tags", []), session=self._session_no()),
            "link": lambda a: self.memory.link(a["memory"], a["tag"], reason=a["reason"], author="agent"),
            "unlink": lambda a: self.memory.unlink(a["memory"], a["tag"], reason=a["reason"], author="agent"),
            "retire": lambda a: self.memory.retire(a["memory"], reason=a["reason"], author="agent"),
            "review": lambda a: self.memory.review(a.get("session") or self._session_no()),
            "lessons": lambda a: self.memory.lessons(self._session_no(), a["text"], author="agent"),
            "show": lambda a: self.memory.show(a["memory"]),
            "proposals": lambda a: self.memory.proposals(),
        }

    # ── tools ───────────────────────────────────────────────────────────────

    def _session_no(self) -> int:
        if self.session is None:
            self.session = int(self.memory.session()["session"])
        return self.session

    def _session(self, _: Dict[str, Any]) -> Any:
        info = self.memory.session()
        self.session = int(info["session"])
        return info

    def _remember(self, a: Dict[str, Any]) -> Any:
        return self.memory.remember(
            title=a["title"], text=a["text"], tags=a["tags"], reason=a.get("reason", ""),
            supersedes=a.get("supersedes", []), generalises=a.get("generalises", []), seen=a.get("seen", []),
            cites=a.get("cites", []),
            proposals=a.get("proposals", []), session=self._session_no(), turn=self.turn, author="agent")

    # ── protocol ────────────────────────────────────────────────────────────

    def handle(self, msg: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        method = msg.get("method")
        mid = msg.get("id")
        if mid is None:  # a notification: never answered
            return None
        if method == "initialize":
            requested = (msg.get("params") or {}).get("protocolVersion") or PROTOCOL_VERSION
            return _result(mid, {
                "protocolVersion": requested,
                "capabilities": {"tools": {"listChanged": False}, "prompts": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": _version()},
                "instructions": INSTRUCTIONS,
            })
        if method == "ping":
            return _result(mid, {})
        if method == "tools/list":
            return _result(mid, {"tools": TOOLS})
        if method == "tools/call":
            params = msg.get("params") or {}
            return _result(mid, self.call(params.get("name", ""), params.get("arguments") or {}))
        if method == "prompts/list":
            return _result(mid, {"prompts": PROMPTS})
        if method == "prompts/get":
            name = (msg.get("params") or {}).get("name", "")
            text = _PROMPT_TEXT.get(name)
            if text is None:
                return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32602, "message": f"unknown prompt: {name}"}}
            meta = next(p for p in PROMPTS if p["name"] == name)
            return _result(mid, {"description": meta["description"],
                                 "messages": [{"role": "user", "content": {"type": "text", "text": text}}]})
        return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"method not found: {method}"}}

    def call(self, name: str, arguments: Dict[str, Any]) -> Dict[str, Any]:
        """One tool call, as an MCP CallToolResult. A refusal is a normal result
        (the agent reads its facts); only a failure to run is an error."""
        self.turn += 1
        fn = self._calls.get(name)
        if fn is None:
            return _text(f"unknown tool: {name}", error=True)
        try:
            result = fn(arguments)
        except KeyError as exc:
            return _text(f"{name}: missing argument {exc.args[0]!r}", error=True)
        except Exception as exc:  # the extension's messages already name file and line
            return _text(f"{name}: {type(exc).__name__}: {exc}", error=True)
        return _text(json.dumps(result, ensure_ascii=False))

    def serve(self, stdin: IO[str], stdout: IO[str]) -> None:
        for line in stdin:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except ValueError:
                _write(stdout, {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}})
                continue
            response = self.handle(msg) if isinstance(msg, dict) else None
            if response is not None:
                _write(stdout, response)


def _result(mid: Any, result: Dict[str, Any]) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "id": mid, "result": result}


def _text(text: str, *, error: bool = False) -> Dict[str, Any]:
    return {"content": [{"type": "text", "text": text}], "isError": error}


def _write(stdout: IO[str], msg: Dict[str, Any]) -> None:
    stdout.write(json.dumps(msg, ensure_ascii=False) + "\n")
    stdout.flush()


def _version() -> str:
    try:
        from importlib.metadata import version
        return version("python-gimmicks")
    except Exception:
        return "0"


def run(root: str, stdin: Optional[IO[str]] = None, stdout: Optional[IO[str]] = None) -> None:
    """Serves the repository at *root* over stdio until stdin closes."""
    from pygim.memory import Memory

    memory = Memory(root)
    print(f"{SERVER_NAME}: serving {memory.root} at v{memory.version}", file=sys.stderr)
    MemoryServer(memory).serve(stdin or sys.stdin, stdout or sys.stdout)
