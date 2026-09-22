"""pygim's problem-space memory as an MCP server over stdio.

The Model Context Protocol is JSON-RPC 2.0, one message per line on stdin and
stdout. This server answers ``initialize``, ``ping``, ``tools/list`` and
``tools/call``; each tool is one method of :class:`pygim.enact.Enact`, and
its result is that method's dict as JSON. The server holds the session number
and a turn counter itself, so an agent never has to pass them. Nothing but
protocol messages is ever written to stdout; diagnostics go to stderr.

The design is docs/design/enact/ — the write loop the tool descriptions teach
is the overview's §4.5.
"""
from __future__ import annotations

import datetime
import json
import os
import signal
import sys
import time
from pathlib import Path
from typing import Any, Callable, Dict, IO, List, Optional

from . import _packs, _stores

PROTOCOL_VERSION = "2025-06-18"
SERVER_NAME = "pygim-enact"
STANDING_TOKENS = 2000  # how much preference text the startup instructions may carry
SESSION_ENV = "PYGIM_ENACT_SESSION"   # a reloaded server resumes the session number it had
RELOADED_ENV = "PYGIM_ENACT_RELOADED"  # set on the process a reload exec'd into

INSTRUCTIONS_CAP = 2000  # characters. Claude Code keeps the first 2,048 of a server's instructions and
#                          drops the rest without a word: the first session to rely on them was sent
#                          11,900 and received 2,048, to the character. So the instructions carry the
#                          loop and an index, and `session` carries the knowledge itself.

INSTRUCTIONS = """\
A problem-space memory: knowledge is found by the kind of problem being solved,
not by similarity to the prompt.

1. Call `session` first. Its `standing` holds the owner's preferences in full.
   They apply to everything you do here, advice included: read them before
   anything else. Then call `vocabulary`.
2. `read` before you work, and before you recommend something, rule something
   out or propose a design. Hard tags filter, soft tags order, `term` narrows to
   a subject. Follow the procedure a read returns first; its `standing` names
   preferences in that space it did not place.
3. When something outlives the task: nothing covers it -> `remember`; a memory
   says less or says it wrong -> `remember` with `supersedes`; one already says
   exactly this -> `learn`. Pass what you read as `seen`. No tag fits ->
   `proposals`, never a forced tag. After a read, `learn` what it gave you:
   `useful`, `not_needed` or `misleading` — all three are evidence.
4. A refusal names facts. Act on them and try again.
5. Consolidate only when the user asks (the `consolidate` prompt). Only the user
   accepts a generalisation or a pack, with `oo enact accept`.
6. `scope` names another store that `session` lists; `global` holds what is
   about no single project (tag it domain=any). A #n belongs to one store.
7. No store: tell the user to run `oo enact setup`. A new project starts with
   the `prepare-vocabulary` prompt, then `seed-memories`.
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
   instances only after they accept it with `oo enact accept <key>` — you cannot
   accept it for them.
"""

PACK_SHAPE = """\
pack: {domain}
entry: {{brief: ..., when: ..., when_not: ..., example: ...}}       # the domain itself: domain={domain}
dimensions:
  <dimension>:
    role: hard            # hard: filters by default. soft: only orders
    weight: 1.0
    entry: {{brief: ..., full: ..., when: ..., when_not: ..., example: ...}}
    values:
      <value>:
        entry: {{brief: ..., when: ..., when_not: ..., example: ...}}
        source: {{doc: <id from cite>, line: 12, lines: 1, passage: <digest from cite>}}
extends:                  # this domain's values for the base dimensions
  artifact:
    <value>: {{entry: {{...}}, source: {{...}}}}
  task:
    <value>: {{entry: {{...}}}}
  tier:
    <value>: {{entry: {{...}}}}"""

PREPARE_VOCABULARY = """\
Prepare the vocabulary for the `{domain}` domain: a pack drafted from this project's own
documents, which becomes the vocabulary only when the user accepts it.

The base vocabulary (domain, artifact, task, kind, tier) ships with every store. A pack adds
this domain's values to artifact, task and tier, and dimensions of its own. Every memory will be
found through these tags, so the vocabulary is the first thing a project needs.

1. Call `session` (it gives the store's `root` and the `project` root) and `vocabulary`.
2. Inventory the sources. The project's documents are the only guaranteed input: the README,
   design documents, guides, and the names the source tree itself uses — directories, modules,
   manifests. List them. Anything you compute from them, such as a term count, is derived, not a source.
3. Find the questions. A dimension is one question with a closed list of answers, independent of
   the others. Make one hard only when knowledge under one answer should never surface while
   working under another, as a subsystem's should not; everything else is soft. Look first for
   closed lists the sources already label — directory names, manifest kinds, document types:
   they cost nothing and classify reliably.
4. Name every value in the project's ubiquitous language. Count how the sources name each
   concept and use their word; never coin one. The dimension and the value together should read
   as a term the project uses. Give each value a locator with `cite`, and list the values nothing
   cites rather than inventing a source.
5. Write a codebook entry for every dimension and value: brief, full (dimensions), when,
   when_not, example. An artifact value names what a thing is, never how it is used — given
   only the thing, two readers should agree on it.
6. Reconcile every list you derived against the sources' complete list: what is in both, what
   the sources have that you left out, and what you have that the sources do not. Never present
   a derived list alone.
7. Probe coverage: take one real item for every artifact value and try to tag it. Note what
   cannot be tagged — that is where the vocabulary is thin.
8. Draft into the store, under `taxonomy/studies/{today}-{domain}/`:
   - `proposal/pack-{domain}.yaml`, in this shape:
{pack_shape}
   - `proposal/inventory.yaml`: one entry per cited document, from `cite`'s `inventory` —
     `<id>:` then `kind`, `path` (relative to the project root) and `version`.
   - `report.md`: the sources, each dimension and why it exists, the counts that chose each name,
     the uncited values, the reconciliation, the coverage probe, and your open questions.
9. Call `check_pack` on the proposal and fix every error it names, by file and line.
10. Tell the user where the report is, and that the pack becomes the vocabulary only when they
    run `oo enact accept --pack <path to the proposal>`. Do not copy it into taxonomy/ yourself.
"""

SEED_MEMORIES = """\
Seed this project's memory with what is already known, so the first working session does not
start from nothing.

1. Call `session` and `vocabulary`. If no pack names this project's domain yet, stop and suggest
   the `prepare-vocabulary` prompt first: a memory tagged from the base vocabulary alone is hard
   to find again.
2. Find durable knowledge in the project's documents and history: decisions and their reasons,
   conventions the code follows, how recurring tasks are done (adding a module, releasing,
   testing), known pitfalls. Skip what a document already states plainly — sources are cited,
   not copied. A memory earns its place by saying what a reader would otherwise rediscover.
3. For each one, `read` its space first, then decide: nothing covers it, so `remember`;
   something says less or says it wrong, so `remember` superseding it; something already says
   exactly this, so `learn`. Pass `seed: true`: a store being seeded has nothing to have read,
   so its writes skip the unread check. Answer every hard dimension, and put the `locator` that
   `cite` returns for the passage it rests on in `cites`. A document's own cross-references ("see
   also") are not yet followed by reads, so a memory that depends on another rule says so in its
   text.
4. Write how a recurring task is done as a procedure (kind=procedure, one per artifact and
   task), a choice and its reason as a decision, and a rule of thumb as a principle only when
   several cases show it.
5. Write concretely — the case and where it came from. Leave patterns for a later `consolidate`.
6. Record `lessons`: what you seeded, what you left out and why, and the gaps — knowledge the
   vocabulary could not tag. Tell the user the report's path.
"""

PROMPTS: List[Dict[str, Any]] = [
    {
        "name": "consolidate",
        "description": "Read back what this session has written and state once, as a generalisation, "
                       "any pattern several memories share. Nothing is retired.",
        "arguments": [],
    },
    {
        "name": "prepare-vocabulary",
        "description": "Draft this project's vocabulary pack from its own documents, with a study report, "
                       "for the user to accept with `oo enact accept --pack`.",
        "arguments": [{"name": "domain", "description": "The domain's name, which is the pack's name "
                                                        "(default: the project directory's name).", "required": False}],
    },
    {
        "name": "seed-memories",
        "description": "Record what the project's documents and history already know — decisions, conventions, "
                       "procedures — as the store's first memories.",
        "arguments": [],
    },
]


def _domain_of(arguments: Dict[str, Any], server: "EnactServer") -> str:
    given = str(arguments.get("domain") or "").strip()
    name = given or _stores.project_name(server._cwd)
    return "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_") or "project"


_PROMPT_TEXT: Dict[str, Callable[[Dict[str, Any], "EnactServer"], str]] = {
    "consolidate": lambda args, server: CONSOLIDATE,
    "prepare-vocabulary": lambda args, server: PREPARE_VOCABULARY.format(
        domain=_domain_of(args, server), today=datetime.date.today().isoformat(),
        pack_shape="\n".join("       " + line for line in PACK_SHAPE.format(domain=_domain_of(args, server)).split("\n"))),
    "seed-memories": lambda args, server: SEED_MEMORIES,
}

_SCOPE = {"type": "string",
          "description": "Which store: `project` (default), `global` — the machine's, for knowledge about no single "
                         "project — or the name of any other store this machine holds, as `session` lists them. A #n "
                         "numbers memories within one store, so read a scope before writing to it."}
_REF = {"type": "string", "description": "A memory: #n from a read or show, or at least 8 characters of its key."}
_LOCATOR = {"type": "string", "description": "One source locator, as `cite` returns it: document:L<line> or "
                                             "document:L<from>-<to>."}
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
        "inputSchema": _schema({"scope": _SCOPE}),
    },
    {
        "name": "read",
        "description": "Retrieve the working context for a problem space. `hard`: tags a memory must match — at least "
                       "one; several values of one dimension mean any of them; never `any` (name the value your work "
                       "is: memories tagged `any` answer it too). `soft`: tags that only order. Returns the procedure "
                       "for the artifact and task first (follow its steps), then ranked memories, each with the tags "
                       "that admitted and ranked it; `skipped` counts the rest and `budget_dropped` how many of those the "
                       "budget had no room for, `facets` counts the tags the candidates "
                       "carry (a soft tag at 0 cannot match), and `coverage` names the documents they cite and the "
                       "inventoried ones none cites. Read before you write.",
        "inputSchema": _schema({
            "hard": _TAGS,
            "soft": _TAGS,
            "scope": _SCOPE,
            "term": {"type": "string", "description": "Keep only candidates whose title or text has this word — the "
                                                      "subject tags cannot name, such as \"invisible\". It matches at the start of a "
                                                      "word, so query the word as your sources write it (\"mount\" finds mounted and "
                                                      "mounts, not amount; \"visible\" does not find invisible); several words match "
                                                      "that phrase exactly. A filter, never a score: "
                                                      "`term_matched` beside `candidates` shows what the word, not the tags, left."},
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
            "scope": _SCOPE,
            "title": {"type": "string"},
            "text": {"type": "string"},
            "tags": _TAGS,
            "reason": {"type": "string", "description": "Why this outlives the task, or why it replaces what it supersedes."},
            "supersedes": _REFS,
            "generalises": {"type": "array", "items": _REF,
                            "description": "For a generalisation: the two or more heads whose shared pattern this states. "
                                           "They stay heads, count as read, and must be covered on every hard dimension."},
            "seen": _REFS,
            "cites": {"type": "array", "items": {"type": "string"},
                      "description": "Source locators as `cite` returns them, e.g. phb-2024-spells:L4459 or phb-2024-ch1:L843-849."},
            "seed": {"type": "boolean", "description": "Only when seeding a new store from existing documents (the "
                                                       "seed-memories prompt): there is nothing to have read, so the unread "
                                                       "check is skipped. Never for knowledge learned while working."},
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
        "description": "Report what a memory you were given turned out to be worth. `useful` (the default) with a tag "
                       "the memory does not carry counts toward linking that tag — three reports promote it; on a "
                       "procedure with no tag it records that its steps held. `not_needed` and `misleading` are worth "
                       "as much: without them a memory that wastes every context it enters looks exactly like one "
                       "nobody has read.",
        "inputSchema": _schema({"memory": _REF, "tag": {"type": "string"}, "reason": {"type": "string"},
                                "verdict": {"type": "string", "enum": ["useful", "not_needed", "misleading"],
                                            "description": "What it was worth here. Default: useful."},
                                "scope": _SCOPE}, ["memory"]),
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
        "description": "Attach something to a memory, with a reason: a `tag` from the vocabulary, or a `cite` — one "
                       "locator as the `cite` tool returns it. Exactly one of the two. A citation moves this way so "
                       "that correcting a line number costs a row, not a new memory and a new number.",
        "inputSchema": _schema({"memory": _REF, "tag": {"type": "string"}, "cite": _LOCATOR,
                                "reason": {"type": "string"}, "scope": _SCOPE},
                               ["memory", "reason"]),
    },
    {
        "name": "unlink",
        "description": "Take a `tag` or a `cite` off a memory, with a reason. Exactly one of the two.",
        "inputSchema": _schema({"memory": _REF, "tag": {"type": "string"}, "cite": _LOCATOR,
                                "reason": {"type": "string"}, "scope": _SCOPE},
                               ["memory", "reason"]),
    },
    {
        "name": "retire",
        "description": "Take a memory out of retrieval, with a reason. It stays readable with `show`.",
        "inputSchema": _schema({"memory": _REF, "reason": {"type": "string"}, "scope": _SCOPE}, ["memory", "reason"]),
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
        "name": "mailbox",
        "description": "Messages other sessions, agents or people left in this store: feedback, requests and comments. "
                       "Open ones by default, oldest first; `all` includes what has been resolved. `session` reports "
                       "how many are waiting, so read this when it does. A message is not a memory — it is addressed, "
                       "answered and done — so nothing here is knowledge until someone writes it as one.",
        "inputSchema": _schema({"all": {"type": "boolean", "description": "Include resolved messages. Default: false."},
                                "mine": {"type": "string", "description": "Keep only messages addressed to this name, "
                                                                          "or to nobody in particular."},
                                "scope": _SCOPE}),
    },
    {
        "name": "post",
        "description": "Leave a message for whoever works in this store next — another session, an agent, or a person. "
                       "Use it to hand over what you could not finish, to ask for something, or to report what you "
                       "found. `resolves` closes an earlier message and needs text of its own, so a thread is never "
                       "closed silently.",
        "inputSchema": _schema({
            "text": {"type": "string", "description": "What is being asked, reported or commented on."},
            "kind": {"type": "string", "enum": ["feedback", "request", "comment"], "description": "Default: comment."},
            "to": {"type": "string", "description": "Who it is for — a session, an agent, a person. Empty: whoever reads next."},
            "about": {"type": "string", "description": "What it concerns: a memory (#n or a key), a path, a report."},
            "reply_to": {"type": "string", "description": "The message id this answers."},
            "resolves": {"type": "string", "description": "The message id this closes."},
            "author": {"type": "string", "description": "Who is leaving it, if not this agent."},
            "scope": _SCOPE,
        }, ["text"]),
    },
    {
        "name": "show",
        "description": "One memory in full: its text, tags, lineage, what it saw, citations and counters.",
        "inputSchema": _schema({"memory": _REF, "scope": _SCOPE}, ["memory"]),
    },
    {
        "name": "proposals",
        "description": "Concepts the vocabulary lacks, folded, with the memories that asked. A human accepts one by "
                       "adding it to a pack file under taxonomy/; the asking memories are then linked on the next start.",
        "inputSchema": _schema({}),
    },
    {
        "name": "cite",
        "description": "A locator into a document: `locator` for a memory's `cites`, `source` for a vocabulary value, "
                       "the document's inventory entry (under the id the inventory already gives it), and the "
                       "passage's text — read it, and check it says what you are about to cite it for. Paths are "
                       "relative to the project the store serves, which for a store of its own sources is the store.",
        "inputSchema": _schema({
            "path": {"type": "string", "description": "The document, relative to the project root."},
            "line": {"type": "integer", "minimum": 1},
            "lines": {"type": "integer", "minimum": 1, "description": "How many lines the passage spans (default 1)."},
            "scope": _SCOPE,
        }, ["path", "line"]),
    },
    {
        "name": "check_pack",
        "description": "Loads a drafted pack beside the store's vocabulary in a scratch copy and reports every error "
                       "by file and line, or what the pack adds, with `warnings` for locators that do not hold in their "
                       "documents and `removed` for live values it drops, with the memories still carrying them — "
                       "retag those before the user accepts. Nothing live changes; the user accepts a pack.",
        "inputSchema": _schema({"path": {"type": "string",
                                         "description": "The proposal, relative to the store root or the project root."}},
                               ["path"]),
    },
]


class NoStore(RuntimeError):
    """The project has no memory store yet; the message says how to make one."""


class EnactServer:
    """Answers MCP messages for one project's store. `handle` takes one decoded
    message and returns the response to write, or None for a notification.

    The store is found lazily (see `_stores.find`), so the server starts in a project
    that has none and answers with setup guidance until one exists. It reopens the
    store when a vocabulary file under taxonomy/ changes, so an accepted pack is live
    at the next call without restarting the server."""

    def __init__(self, memory: Any = None, *, root: Optional[str] = None, cwd: Optional[Path] = None) -> None:
        self._memory = memory
        self._root = root
        self._cwd = Path(cwd or os.getcwd())
        self._global: Any = None
        self._standing_seen: Optional[Dict[str, str]] = None  # global preference key → title, as last told
        self.signalled = False        # set by SIGHUP; acted on between messages
        self._stores_open: Dict[str, Any] = {}   # stores opened by name this session
        self._verdicts: Any = None    # whether the installed pygim takes a learn verdict; None until tried
        self._started = time.time_ns()
        self._code = self._code_stamp()
        self._told_stale = False
        self.session: Optional[int] = int(os.environ[SESSION_ENV]) if os.environ.get(SESSION_ENV) else None
        self._stamp = self._taxonomy_stamp() if memory is not None else None
        self._vocabulary: Optional[str] = None  # the vocabulary version the agent last saw
        self.turn = 0
        self._calls: Dict[str, Callable[[Dict[str, Any]], Any]] = {
            "cite": self._cite,
            "check_pack": lambda a: _packs.check(Path(self.memory.root), self._store_path(a["path"]),
                                                 project=_stores.project_root(self._cwd), memory=self.memory),
            "session": self._session,
            "vocabulary": lambda a: self._mem(a).vocabulary(),
            "read": self._read,
            "remember": self._remember,
            "learn": self._learn,
            "merge": lambda a: self.memory.merge(a["memories"], title=a["title"], text=a["text"], reason=a["reason"],
                                                 tags=a.get("tags", []), session=self._session_no()),
            "link": lambda a: self._attach(a, add=True),
            "unlink": lambda a: self._attach(a, add=False),
            "retire": lambda a: self._mem(a).retire(a["memory"], reason=a["reason"], author="agent"),
            "review": lambda a: self.memory.review(a.get("session") or self._session_no()),
            "lessons": lambda a: self.memory.lessons(self._session_no(), a["text"], author="agent"),
            "show": lambda a: self._mem(a).show(a["memory"]),
            "mailbox": lambda a: self._mem(a).mailbox(all=bool(a.get("all", False)), mine=a.get("mine", "")),
            "post": lambda a: self._mem(a).post(a["text"], kind=a.get("kind", "comment"), to=a.get("to", ""),
                                                about=a.get("about", ""), reply_to=a.get("reply_to", ""),
                                                resolves=a.get("resolves", ""), session=self._session_no(),
                                                author=a.get("author", "agent")),
            "proposals": lambda a: self.memory.proposals(),
        }

    # ── the stores ──────────────────────────────────────────────────────────

    def _mem(self, arguments: Dict[str, Any]) -> Any:
        """The store a call works on, by name. `project` (the default) and `global` always resolve;
        every other store this machine holds is found by `_stores.discover`, so a store checked out
        beside a project is addressable the moment it exists — nothing to configure."""
        name = (arguments.get("scope") or "project").strip().lower()
        if name == "project":
            return self.memory
        if name == "global":
            return self.global_memory
        if name in self._stores_open:
            return self._stores_open[name]
        from pygim.enact import Enact

        found = _stores.scope(name, self._cwd, self._root)
        if found is None:
            known = ", ".join(s.name for s in self.scopes()) or "project"
            raise NoStore(f"no store called `{name}` — this machine has: {known}. A store is found by its "
                          f"policy's name or its directory's, in your user data directory or beside a project.")
        self._stores_open[name] = Enact(str(found.root))
        return self._stores_open[name]

    def scopes(self) -> List[Any]:
        """Every store a session can name here, the project's first."""
        return _stores.discover(self._cwd, self._root)

    @property
    def global_memory(self) -> Any:
        """The global store, opened on first use. Knowledge here reaches every project's sessions."""
        from pygim.enact import Enact

        if self._global is None:
            root = _stores.find_global()
            if root is None or not _stores.is_store(root):
                raise NoStore("this machine has no global store — run `oo enact setup --global` to make one; "
                              "knowledge about one project belongs in its own store")
            self._global = Enact(str(root))
        return self._global

    def _global_if_any(self) -> Any:
        try:
            return self.global_memory
        except Exception:  # no store, or a vocabulary that will not load: the tools say so
            return None

    # ── the store ───────────────────────────────────────────────────────────

    @property
    def memory(self) -> Any:
        from pygim.enact import Enact

        if self._memory is None:
            found = _stores.find(self._root, self._cwd)
            if found is None or not found.exists:
                raise NoStore(_stores.guidance(self._cwd))
            self._memory = Enact(str(found.root))
            self._stamp = self._taxonomy_stamp()
        elif self._taxonomy_stamp() != self._stamp:
            self._memory = Enact(self._memory.root)  # a broken pack raises here, by file and line, and nothing is swapped
            self._stamp = self._taxonomy_stamp()
        return self._memory

    def _heads(self, memory: Any, tag: str) -> List[Dict[str, Any]]:
        try:
            return memory.heads([tag]) if memory is not None else []
        except Exception:  # a store that will not load: the tools will say so
            return []

    def standing(self) -> Dict[str, Any]:
        """The standing knowledge of a session here: every preference in full and every procedure by
        title, from the machine's global store and then this project's — global first and the
        project's last, because where the two disagree the nearer rule wins. Nothing is recorded as
        read. This is what `session` returns; the instructions carry only an index of it."""
        wide = self._global_if_any()
        near = self._memory if self._memory is not None else self._project_if_any()
        sources = [("global", wide), ("project", near)]
        preferences = [dict(scope=origin, memory=p["memory"], title=p["title"], text=p["text"].strip(), tokens=p["tokens"])
                       for origin, m in sources for p in self._heads(m, "kind=preference")]
        procedures = [dict(scope=origin, memory=p["memory"], title=p["title"],
                           where=" ".join(t for t in p["tags"] if t.startswith(("artifact=", "task="))))
                      for origin, m in sources for p in self._heads(m, "kind=procedure")]
        if self._standing_seen is None:   # the baseline `standing_changed` compares against; only it moves it on
            self._standing_seen = {p["key"]: p["title"] for p in self._heads(wide, "kind=preference")}
        waiting = []
        try:
            waiting = near.mailbox()[:10] if near is not None else []
        except Exception:   # a store that will not load: the tools will say so
            waiting = []
        return {"note": "These apply to every task here, advice included. Where a global and a project "
                        "preference disagree, the project's is the rule.",
                "preferences": preferences, "procedures": procedures, "waiting": waiting}

    def _standing(self) -> str:
        """The index of standing knowledge that the instructions carry: titles only, and only as many
        as fit under INSTRUCTIONS_CAP, saying how many were left out. A host truncates a server's
        instructions without saying so, so nothing a session must not miss is delivered here —
        the full texts come back from `session`, which every session calls first."""
        data = self.standing()
        def line(p: Dict[str, Any]) -> str:
            title = p["title"] if len(p["title"]) <= 58 else p["title"][:57].rstrip() + "…"
            return f"- {p['memory']}{' (global)' if p['scope'] == 'global' else ''} {title}"

        # newest first: an old preference has had sessions to sink in, a new one has not
        entries = [line(p) for p in reversed(data["preferences"])]
        if not entries:
            return ""
        head = "\nThe owner's standing preferences — titles only; `session` returns them in full:\n"
        room = INSTRUCTIONS_CAP - len(INSTRUCTIONS) - len(head) - 60
        shown: List[str] = []
        for entry in entries:
            if len("\n".join(shown + [entry])) > room:
                break
            shown.append(entry)
        left = len(entries) - len(shown)
        tail = f"\n- and {left} more, in `session`" if left else ""
        return head + "\n".join(shown) + tail + "\n"

    def _project_if_any(self) -> Any:
        try:
            return self.memory
        except Exception:
            return None

    def _cite(self, a: Dict[str, Any]) -> Any:
        """A locator into a document of the project a store serves. For `project` that is this
        worktree; for any other store it is what its policy declares, else the sibling the
        convention names, else the store itself — which is where a store that carries its own
        sources, such as the ddd one, keeps them. Without this the tool reached only the project's
        own documents, so a store built entirely from its own sources could not be quoted at all
        and its citations went unchecked."""
        project, store = self._cite_roots(a)
        return _packs.cite(project, a["path"], int(a["line"]), int(a.get("lines", 1)), store=store)

    def _cite_roots(self, a: Dict[str, Any]) -> Any:
        name = (a.get("scope") or "project").strip().lower()
        if name == "project":
            return _stores.project_root(self._cwd), self._store_root()
        root = Path(self._mem(a).root)
        return (_stores.project_of(root) or root), root

    def _attach(self, a: Dict[str, Any], *, add: bool) -> Any:
        """`link` and `unlink` on either of a memory's two attachments: a tag, or a citation. One
        job on two targets is one command with an option, not two commands (#17)."""
        tag, locator = a.get("tag"), a.get("cite")
        if bool(tag) == bool(locator):
            return {"ok": False, "refused": "one of tag or cite",
                    "message": "name exactly one: `tag` to move a tag, `cite` to move a source locator",
                    "facts": []}
        mem = self._mem(a)
        if tag:
            op = mem.link if add else mem.unlink
            return op(a["memory"], tag, reason=a["reason"], author="agent")
        if add:
            checked = self._locator_facts(a, locator)
            if checked is not None:
                return checked
        op = mem.cite if add else mem.uncite
        result = op(a["memory"], locator, reason=a["reason"], author="agent")
        if result.get("ok") and add:
            result["passage"] = self._passage(a, locator)
        return result

    def _locator_facts(self, a: Dict[str, Any], locator: str) -> Any:
        """Refuses a locator that does not resolve in this store's inventory, naming what does. It
        cannot tell whether the passage says what it is cited for — only that it exists — so the
        text comes back beside the result for the writer to read."""
        try:
            project, store = self._cite_roots(a)
            _packs.passage(project, store, locator)
            return None
        except (ValueError, KeyError, OSError) as bad:
            return {"ok": False, "refused": "unknown locator", "message": str(bad), "facts": []}

    def _passage(self, a: Dict[str, Any], locator: str) -> Any:
        try:
            project, store = self._cite_roots(a)
            return _packs.passage(project, store, locator)
        except Exception:
            return None

    def _store_root(self) -> Optional[Path]:
        try:
            return Path(self.memory.root)
        except NoStore:  # a project can be cited before it has a store
            return None

    def _taxonomy_stamp(self) -> Any:
        files = sorted(Path(self._memory.root, "taxonomy").glob("*.yaml"))
        return tuple((f.name, f.stat().st_mtime_ns, f.stat().st_size) for f in files)

    def _store_path(self, path: str) -> Path:
        p = Path(path).expanduser()
        if p.is_absolute():
            return p
        in_store = Path(self.memory.root) / p
        return in_store if in_store.exists() else _stores.project_root(self._cwd) / p

    # ── tools ───────────────────────────────────────────────────────────────

    def _session_no(self) -> int:
        if self.session is None:
            self.session = int(self.memory.session()["session"])
        return self.session

    def _session(self, _: Dict[str, Any]) -> Any:
        info = self.memory.session()
        self.session = int(info["session"])
        info["root"] = self.memory.root
        info["project"] = str(_stores.project_root(self._cwd))
        info["standing"] = self.standing()
        info["scopes"] = [{"scope": s.name, "root": str(s.root), "how": s.how,
                           "sharing": _stores.policy(s.root).sharing,
                           **({"also": s.aliases} if s.aliases else {})}
                          for s in self.scopes()]
        return info

    def _learn(self, a: Dict[str, Any]) -> Any:
        """A tool must not fail because this server's Python is newer than the extension it imported.
        The first `verdict` a stale build rejects is the last one passed: the call is retried without
        it, and every answer afterwards says the loop is not recording until the server is replaced."""
        memory = self._mem(a)
        fixed = dict(tag=a.get("tag", ""), reason=a.get("reason", ""), session=self._session_no())
        verdict = a.get("verdict", "useful")
        if self._verdicts is not False:
            try:
                out = memory.learn(a["memory"], verdict=verdict, **fixed)
                self._verdicts = True
                return out
            except TypeError:
                self._verdicts = False   # the installed pygim predates verdicts
        out = memory.learn(a["memory"], **fixed)
        if isinstance(out, dict):
            out["degraded"] = ("this server's pygim has no verdicts, so `" + verdict + "` was not recorded — "
                               "run `oo enact reload`, and reconnect if it does not clear")
        return out

    def _read(self, a: Dict[str, Any]) -> Any:
        result = self._mem(a).read(a["hard"], a.get("soft", []), max=a.get("max", 8), budget=a.get("budget", 0),
                                  term=a.get("term", ""), session=self._session_no())
        if result.get("memories") or result.get("procedure"):
            result["next"] = ("Report each of these with learn when you are done: verdict useful, not_needed or "
                              "misleading. A read records nothing about what its memories were worth.")
        return result

    def _remember(self, a: Dict[str, Any]) -> Any:
        return self._mem(a).remember(
            title=a["title"], text=a["text"], tags=a["tags"], reason=a.get("reason", ""),
            supersedes=a.get("supersedes", []), generalises=a.get("generalises", []), seen=a.get("seen", []),
            cites=a.get("cites", []),
            proposals=a.get("proposals", []), session=self._session_no(), turn=self.turn, author="agent",
            origin="seed" if a.get("seed") else "written")

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
                "instructions": INSTRUCTIONS + self._standing(),
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
            params = msg.get("params") or {}
            name = params.get("name", "")
            render = _PROMPT_TEXT.get(name)
            if render is None:
                return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32602, "message": f"unknown prompt: {name}"}}
            meta = next(p for p in PROMPTS if p["name"] == name)
            text = render(params.get("arguments") or {}, self)
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
            self._note_vocabulary(name, result)
            self._note_global(name, arguments, result)
            self._note_stale(result)
        except NoStore as exc:
            return _text(f"{name}: {exc}", error=True)
        except KeyError as exc:
            return _text(f"{name}: missing argument {exc.args[0]!r}", error=True)
        except Exception as exc:  # the extension's messages already name file and line
            return _text(f"{name}: {type(exc).__name__}: {exc}", error=True)
        return _text(json.dumps(result, ensure_ascii=False))

    def _note_vocabulary(self, name: str, result: Any) -> None:
        """Tells the agent, in the result of whichever call comes first, that the vocabulary changed
        since it last saw it — an accepted pack is live without anyone polling `session`."""
        if self._memory is None or name in ("cite",):
            return
        now = self._memory.taxonomy
        if name in ("session", "vocabulary") or self._vocabulary is None:
            self._vocabulary = now
            return
        if now != self._vocabulary and isinstance(result, dict):
            result["vocabulary_changed"] = {"from": self._vocabulary, "to": now,
                                            "next": "call vocabulary: values may have been added, renamed or removed"}
            self._vocabulary = now

    WRITES = ("remember", "link", "unlink", "retire", "learn", "merge")

    def _note_global(self, name: str, arguments: Dict[str, Any], result: Any) -> None:
        """Two things about the global store. A write to it is published at once if its policy says
        so — that is what carries a preference to this machine's other projects and to other
        machines. And whatever the call was, a preference that appeared or left since this session
        last looked is named, so a session already running learns of one written elsewhere."""
        if not isinstance(result, dict):
            return
        where = (arguments.get("scope") or "project").strip().lower()
        if where != "project" and result.get("ok") and name in self.WRITES:
            written = self._mem(arguments)
            result["store"] = where
            what = arguments.get("title") or f"{name} {result.get('memory', '')}".strip()
            result["synced"] = _stores.publish(Path(written.root), f"memory: {what}")
        wide = self._global if self._global is not None else self._global_if_any()
        if wide is None:
            return
        current = {p["key"]: p["title"] for p in self._heads(wide, "kind=preference")}
        if self._standing_seen is None:
            self._standing_seen = current
            return
        added = [t for k, t in current.items() if k not in self._standing_seen]
        gone = [t for k, t in self._standing_seen.items() if k not in current]
        if added or gone:
            result["standing_changed"] = {"added": added, "no_longer": gone,
                                          "next": "these hold in every project; `show` them with scope global"}
            self._standing_seen = current

    # ── reloading (03 §9.1.3) ───────────────────────────────────────────────

    def _code_stamp(self) -> Any:
        """What the server is running: the version, and every source file it was loaded from. A
        reinstall or an edited file changes it, and the process cannot pick that up by itself —
        Python has already imported what it has, and the extension cannot be re-imported at all."""
        files = []
        for module in list(sys.modules.values()):
            path = getattr(module, "__file__", None)
            if not path or ("_pygim" not in path and "pygim" not in path):
                continue
            try:
                stat = os.stat(path)
            except OSError:
                continue
            files.append((path, stat.st_mtime_ns, stat.st_size))
        return (_version(), tuple(sorted(files)))

    def stale(self) -> bool:
        """Whether the code on disk has moved on since this process started."""
        return self._code_stamp() != self._code

    def _reload_asked(self) -> bool:
        """`oo enact reload` asks either by signal — SIGHUP, which sets the flag — or by touching
        `local/reload` in the store, which is what Windows has instead of a signal."""
        if self.signalled:
            return True
        try:
            marker = Path(self._memory.root) / "local" / "reload" if self._memory is not None else None
            return marker is not None and marker.is_file() and marker.stat().st_mtime_ns > self._started
        except OSError:
            return False

    @staticmethod
    def _argv_still_runs() -> bool:
        """Whether this process could start itself again. A reload re-execs the argv it was started
        with, so a command that has since been renamed would exec into `No such command` and take
        the server down instead of refreshing it — which is exactly what `oo memory mcp` became
        when the system was renamed to enact. Re-exec only when some argument still names a command
        this installation has."""
        try:
            from pygim.__main__ import cli_oo
            known = set(cli_oo.commands)
        except Exception:
            return True                      # cannot tell: behave as before rather than refuse
        return any(arg in known for arg in sys.argv)

    def reload(self, stdout: IO[str]) -> None:
        """Replaces this process with a fresh one, between messages. The pipes are file descriptors,
        and exec keeps them, so the host's connection survives; the session number travels in the
        environment so the audit log does not split a session in two. Returns only when it refused."""
        try:                             # first, so a refusal below is not asked again on every message
            marker = Path(self._memory.root) / "local" / "reload" if self._memory is not None else None
            if marker is not None and marker.is_file():
                marker.unlink()
        except OSError:
            pass
        if not self._argv_still_runs():
            print(f"{SERVER_NAME}: not reloading — this server was started as "
                  f"`{' '.join(sys.argv[1:])}`, which this installation no longer has. "
                  f"Reconnect the client to replace it.", file=sys.stderr)
            return
        print(f"{SERVER_NAME}: reloading into the code on disk", file=sys.stderr)
        stdout.flush()
        os.environ[RELOADED_ENV] = "1"
        if self.session is not None:
            os.environ[SESSION_ENV] = str(self.session)
        os.execv(sys.executable, [sys.executable, *sys.argv])

    def _note_stale(self, result: Any) -> None:
        """Says once, in a result, that this server is running code older than the files on disk —
        the reader can then run `oo enact reload`, which every server here acts on at its next
        quiet moment. Said once per staleness, not on every call."""
        if not isinstance(result, dict) or not self.stale() or self._told_stale:
            return
        self._told_stale = True
        result["server_stale"] = {"running": _version(),
                                  "next": "the installed pygim has changed since this server started — run "
                                          "`oo enact reload`. If it keeps coming back, this server is older than "
                                          "the reload feature and only reconnecting the client replaces it."}

    def serve(self, stdin: IO[str], stdout: IO[str]) -> None:
        if os.environ.pop(RELOADED_ENV, None):  # a host that watches for it re-fetches the schemas
            _write(stdout, {"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})
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
            if self._reload_asked():   # between messages: nothing is half-answered
                self.reload(stdout)


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


def run(root: Optional[str] = None, stdin: Optional[IO[str]] = None, stdout: Optional[IO[str]] = None) -> None:
    """Serves this project's store over stdio until stdin closes. With no *root*, the store is found
    from the working directory the host started the server in; with none at all the server still
    starts, and answers with how to create one. SIGHUP asks it to reload into the code on disk,
    which it does between messages — `oo enact reload` sends it."""
    found = _stores.find(root)
    if found and found.exists:
        print(f"{SERVER_NAME}: serving {found.root} (from {found.how})", file=sys.stderr)
    else:
        print(f"{SERVER_NAME}: {_stores.guidance()}", file=sys.stderr)
    server = EnactServer(root=root)
    if hasattr(signal, "SIGHUP"):
        signal.signal(signal.SIGHUP, lambda *_: setattr(server, "signalled", True))
    server.serve(stdin or sys.stdin, stdout or sys.stdout)
