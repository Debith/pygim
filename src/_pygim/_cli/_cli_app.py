# -*- coding: utf-8 -*-
"""
Command-Line Interface Application for Python Gimmicks.
"""

from __future__ import annotations  # `str | None` in signatures on Python 3.9

from subprocess import Popen, DEVNULL
import os
import sys
import shutil
import functools
from typing import Optional
from pathlib import Path
from dataclasses import dataclass
from importlib import import_module
import click

from _pygim._config import Environment
from _pygim._cli import _style

__all__ = ["GimmicksCliApp", "flag_opt"]


def _echo(msg, quiet):
    if not quiet:
        click.echo(msg)


flag_opt = functools.partial(click.option, is_flag=True, default=False)


@dataclass
class GimmicksCliApp:
    def clean_up(self, yes, build_dirs, pycache_dirs, compiled_files, quiet, all):
        root = Path.cwd()
        _echo(f"Starting clean up in `{root}`", quiet)
        targets = []
        pycache_dirs = pycache_dirs or not build_dirs and not compiled_files

        if all or build_dirs:
            targets.extend(p for p in root.rglob("build") if p.is_dir())

        if all or pycache_dirs:
            targets.extend(p for p in root.rglob("__pycache__") if p.is_dir())

        if all or compiled_files:
            targets.extend(p for p in root.rglob("*.c") if p.is_file())
            targets.extend(p for p in root.rglob("*.so") if p.is_file())

        if targets and not yes:
            print("\n".join([str(t) for t in targets]))
            response = input(f"Remove all {len(targets)} files/folders (Y/N)? ")
            if response.lower() == "n":
                sys.exit("No? Maybe next time...")
            elif response.lower() != "y":
                return

        if yes or targets:
            for t in targets:
                if t.is_dir():
                    shutil.rmtree(t)
                elif t.is_file():
                    t.unlink()
            _echo(f"Removed {len(targets)} items.", quiet)

    def show_test_coverage(self):
        # TODO: Make this nicer
        Popen(
            "python -m coverage run -m pytest".split(" "),
            stdout=DEVNULL,
            stderr=DEVNULL,
        ).wait()
        Popen("python -m coverage report -m".split(" ")).wait()

    def docs_serve(self, *, port: int = 8000, directory: str | None = None,
                   host: str | None = None, index: str | None = None,
                   rebuild: str | None = None) -> None:
        """Serve a docs directory locally with the ✎ commenter and image-drop endpoint.

        *rebuild* is a shell command run (in *directory*) before serving; a
        non-zero exit aborts."""
        from _pygim._cli import _docs_serve  # local import: only needed for this verb
        from pygim.pathlike import PathStore

        store = PathStore()                    # the server's table: every path it makes lives here
        doc_root = Path(directory) if directory else Path.cwd()
        try:
            if rebuild:
                click.echo(f"rebuild: {rebuild}")
                added, removed = _docs_serve.rebuild(doc_root, rebuild, store=store)
                click.echo(f"rebuilt: {len(added)} page(s) added, {len(removed)} removed"
                           + "".join(f"\n  + {p}" for p in added) + "".join(f"\n  - {p}" for p in removed))
            _docs_serve.serve(doc_root, port=port, host=host, index=index, store=store)
        except (FileNotFoundError, _docs_serve.ServeError) as exc:
            raise click.ClickException(str(exc)) from exc

    def inventory(self, *, where: Environment, path: str | None, brief: bool = False) -> None:
        """What is already at hand in a project, against what its code actually reaches for.

        The point is the join, not either list: a component you already have is invisible at the
        call site, so nobody notices reaching past it. Printed, never failed — a first run finds
        things that are fine on purpose, and a check that cries wolf is read once and then never
        again. What should fail is decided after the exemptions are written down."""
        from _pygim import _inventory

        root = Path(path) if path else Path(where.cwd)
        if not root.is_dir():
            raise click.ClickException(f"{root} is not a directory")
        if brief:
            click.echo(_inventory.project_map(root))
            return
        found = _inventory.from_machine(root)
        click.echo(_style.title(f"{root.name} — {found.files} python file(s), "
                                f"{len(found.ships)} module(s) shipped") + "\n")
        if not found.ships:
            click.echo(_style.muted("  ships nothing importable — an application, not a library\n"))

        def band(heading, rows, note=""):
            if not rows:
                return
            click.echo(_style.strong(heading) + (_style.muted(f"  — {note}") if note else ""))
            for row in rows:
                click.echo(f"  {row}")
            click.echo("")

        band("ships, and nothing imports it", found.unused,
             "not even a test; either unfinished or unreachable")
        band("ships, and only its own tests and examples import it", found.shown_only,
             "demonstrated, never used — the feedback a component gives only comes from a real caller")
        band("ships, and the project builds with it",
             [f"{name:<28}{use.work:>4} in the work{('  +' + str(use.shown) + ' shown') if use.shown else ''}"
              for name, use in found.used])
        band("declared as a dependency, never imported", list(found.declared_unused),
             "from pyproject.toml")
        band("imported, and nothing here provides it",
             [f"{name:<28}{use.total:>4}" for name, use in found.unresolved.items()],
             "a sibling checkout or a missing install")
        band("available here, and used",
             [f"{name:<28}{use.total:>4}" for name, use in list(found.third_party.items())[:8]],
             "what this project reaches for from elsewhere")

        idle = found.unused + found.shown_only
        if idle:
            click.echo(_style.bad(f"{len(idle)} of {len(found.ships)} shipped module(s) are not "
                                  f"used by this project's own code."))
            click.echo(_style.muted("Each is a decision — worth recording either way. Nothing here "
                                    "fails; the join is the finding."))
        elif found.ships:
            click.echo(_style.good("every shipped module is used by this project's own code."))

    def enact_mcp(self, *, where: Environment) -> None:
        """Serve the project's store over MCP on stdio; the server starts even without one."""
        from _pygim._mcp import enact as server
        from pygim.enact import VocabularyError

        try:
            server.run(where)
        except (RuntimeError, VocabularyError) as exc:
            raise click.ClickException(str(exc)) from exc

    def enact_call(self, *, where: Environment, name: str, arguments: Optional[str]) -> None:
        """One tool of the agent surface, from a shell. The same dispatch the MCP server uses, so a
        script — or a test — drives the whole stack through the commands, with no Python import of
        its own: `oo enact call read --json '{"hard": ["domain=dnd", "artifact=spell",
        "task=design"]}'`. Arguments come from --json or, without it, from stdin.

        A refusal is a result here, as everywhere else in this system: it prints as JSON with
        `refused` and exits 0. Exit 1 means the call could not be made at all — the tool does not
        exist, or the arguments were not JSON.

        Each call is a process, and a process is a session, so a script that does not say otherwise
        writes every memory into a session of its own and `review` can gather none of them. Name one
        with *session* — or export ``PYGIM_ENACT_SESSION``, which is the same mechanism a reloaded
        server resumes by."""
        import json as _json

        from _pygim._mcp import enact as server

        text = arguments if arguments is not None else sys.stdin.read()
        try:
            parsed = _json.loads(text or "{}")
        except ValueError as bad:
            raise click.ClickException(f"--json is not JSON: {bad}") from bad
        if not isinstance(parsed, dict):
            raise click.ClickException("--json must be an object of the tool's arguments")
        if name not in {tool["name"] for tool in server.TOOLS}:
            known = ", ".join(sorted(tool["name"] for tool in server.TOOLS))
            raise click.ClickException(f"no tool called `{name}` — this server offers: {known}")

        made = server.build(where)   # the same wiring the MCP server itself uses
        reply = made.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                             "params": {"name": name, "arguments": parsed}})
        result = reply.get("result") or {}
        body = (result.get("content") or [{}])[0].get("text", "")
        if result.get("isError"):
            raise click.ClickException(body)
        try:
            click.echo(_json.dumps(_json.loads(body), indent=2, ensure_ascii=False))
        except ValueError:
            click.echo(body)

    def enact_hook(self, *, where: Environment) -> None:
        """Speak an agent host's hook protocol on stdin and stdout, so the host's configuration holds
        one command rather than a shell pipeline nobody can test.

        Three events, and they are the moments a rule can arrive: the session's start, where the
        project map and the standing knowledge are delivered because nothing is known about what is
        coming; the moment a request is made, where a procedure whose task the request asks for
        arrives in full; and immediately before a file is written, where almost nothing is known —
        only what the path says (global #27).

        Prints nothing when there is nothing to say. A hook that fires on every write and speaks
        every time is read as noise and then not read at all."""
        import json as _json

        try:
            event = _json.loads(sys.stdin.read() or "{}")
        except ValueError:
            return
        name = event.get("hook_event_name") or ""
        here = where.at(Path(event.get("cwd") or where.cwd))
        if name == "SessionStart":
            said = self._project_map(here) + self._capture(lambda: self.enact_status(where=here, standing=True))
        elif name == "UserPromptSubmit":
            said = self._capture(lambda: self._process_for(here, event.get("prompt") or ""))
        elif name == "PreToolUse":
            path = (event.get("tool_input") or {}).get("file_path") or ""
            if not path:
                return
            said = self._capture(lambda: self._standing_for(where.at(Path(event.get("cwd") or where.cwd)), path))
        else:
            return
        if said.strip():
            click.echo(_json.dumps({"hookSpecificOutput": {"hookEventName": name,
                                                           "additionalContext": said}}))

    @staticmethod
    def _project_map(where: Environment) -> str:
        """The project map, framed for a session's start — or nothing outside a git checkout, where
        there is no project to describe and a walk could wander the whole home directory."""
        from _pygim import _inventory
        from _pygim._mcp import _stores

        if _stores.git(["rev-parse", "--show-toplevel"], Path(where.cwd)) is None:
            return ""
        root = _stores.project_root(Path(where.cwd))
        try:
            body = _inventory.project_map(root)
        except Exception:                                # never worth failing a session start for
            return ""
        return f'<project-map root="{root}">\n{body}\n</project-map>\n\n'

    def _process_for(self, where: Environment, prompt: str) -> None:
        """The procedure a request asks for, in full — or nothing, which is the common answer.

        Each procedure names the words a request uses when its task is asked (`asked`); the one
        whose words the request uses most is delivered, both on a tie. A process known only by its
        title is not followed, so its steps must arrive at the moment the task is stated."""
        from _pygim._mcp import _cards
        from _pygim._mcp.enact import build

        if not prompt.strip():
            return
        server = build(where)
        scored = []
        for scope, memory in (("global", server._global_if_any()), ("project", server._project_if_any())):
            for p in server._heads(memory, "kind=procedure"):
                hits = _cards.asked_for(_cards.parse(p.get("text", "")), prompt)
                if hits:
                    scored.append((hits, scope, p))
        if not scored:
            return
        best = max(hits for hits, _, _ in scored)
        for hits, scope, p in [s for s in scored if s[0] == best][:2]:
            click.echo(_cards.process(p, scope))

    @staticmethod
    def _capture(run) -> str:
        """What a command would have printed. The hook returns it as data rather than as output."""
        import contextlib
        import io

        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            try:
                run()
            except click.ClickException:
                return ""
        return buffer.getvalue()

    def enact_triggers(self, *, where: Environment) -> None:
        """What every trigger in the map would deliver, by running the delivery itself.

        A trigger map that quietly delivers nothing is worse than none: the hook fires, the command
        says nothing, and the rule that was supposed to arrive never does — with no failure anywhere
        to notice. Hard tags intersect, so this is the common way to get it wrong, and the count is
        the only thing that shows it."""
        from _pygim._mcp import _triggers

        stores = self._stores(where)
        found = stores.find()
        if found is None or not found.exists:
            raise click.ClickException(stores.guidance())
        triggers = _triggers.load(found.root)
        if not triggers:
            click.echo(f"no trigger map: {found.root / _triggers.TRIGGERS} does not exist")
            return
        click.echo(_style.title(f"{len(triggers)} trigger(s) in {found.root / _triggers.TRIGGERS}") + "\n")
        empty = 0
        for pattern, raw in triggers.items():
            example = pattern.replace("**", "x").replace("*", "x")
            said = self._capture(lambda: self._standing_for(where, example))
            rules = [line for line in said.splitlines() if line.startswith("  ")]
            if rules:
                click.echo(f"  {_style.strong(pattern)}  {_style.muted(', '.join(raw))}")
                for line in rules:
                    click.echo(f"  {line}")
            else:
                empty += 1
                click.echo(f"  {_style.bad(pattern)}  {_style.muted(', '.join(raw))}\n"
                           f"      {_style.bad('delivers nothing')} — hard tags intersect, so try fewer")
        if empty:
            click.echo("\n" + _style.bad(f"{empty} of {len(triggers)} deliver nothing"))

    def enact_stale(self, *, where: Environment, everything: bool = False) -> None:
        """Every store on this machine, asked whether what its memories name still exists.

        A card is followed literally, so a dead command or a renamed name in one is stale; the same
        thing in a body may be history on purpose and is only worth a look. Code that cites a global
        memory by a number since superseded is listed too — the citation outlives the memory. Printed,
        never failed: this finds the work, and the work is a rewrite."""
        from pygim.enact import Enact
        from pygim.__main__ import cli_oo
        from _pygim import _inventory
        from _pygim._mcp import _cards, _stale, _stores

        stores = self._stores(where)
        scopes = [(s.name, Path(s.root)) for s in stores.scopes()]
        if not scopes:
            raise click.ClickException(stores.guidance())

        def everything_in(root: Path):
            """All heads, and every superseded memory's slug with the head that replaced it."""
            memory = Enact(str(root))
            domains = [tag for d in memory.vocabulary(dimension="domain", brief=True)["dimensions"]
                       for tag in d["values"]]
            heads = memory.heads(domains)
            highest = max((int(h["memory"].lstrip("#")) for h in heads), default=-1)
            slugs, replaced = {}, {}
            for n in range(highest + 1):
                shown = memory.show(f"#{n}")
                if not shown.get("ok"):
                    continue
                if shown.get("head"):
                    slugs[shown["slug"]] = f"#{n}"
                elif shown.get("superseded_by"):
                    replaced[shown["slug"]] = shown["superseded_by"][0]
            return memory, heads, slugs, replaced

        loaded = {name: everything_in(root) for name, root in scopes}
        head_slugs = {slug: ref for _, _, slugs, _ in loaded.values() for slug, ref in slugs.items()}
        by_ref = {(name, ref): slug for name, (_, _, slugs, _) in loaded.items() for slug, ref in slugs.items()}
        successor = {}
        for name, (_, _, _, replaced) in loaded.items():
            for old, new_ref in replaced.items():
                successor[old] = by_ref.get((name, new_ref), new_ref)
        link = _stale.link_resolver(head_slugs, successor)

        global_name = next((name for name, root in scopes if stores.global_root() and
                            Path(root).resolve() == Path(stores.global_root()).resolve()), None)
        reference = None
        if global_name:
            memory, heads, _, _ = loaded[global_name]
            live = {int(h["memory"].lstrip("#")): h["title"] for h in heads}
            replaced_by = {}
            highest = max(live, default=-1)
            for n in range(highest + 1):
                if n not in live:
                    shown = memory.show(f"#{n}")
                    if shown.get("ok") and shown.get("superseded_by"):
                        replaced_by[n] = int(shown["superseded_by"][0].lstrip("#"))
            reference = _stale.reference_resolver(live, replaced_by)
        command = _stale.command_resolver(cli_oo)
        from importlib import metadata as _metadata
        elsewhere = set(sys.stdlib_module_names) | set(_inventory.installed(_metadata.distributions()))

        stale_total = look_total = legacy_total = 0
        for name, root in scopes:
            memory, heads, _, _ = loaded[name]
            project = _stores.project_of(root)
            files = _inventory.text_files(project) if project else []
            ours = set(_inventory.ships_in(project)) | _inventory.local_in(files) if project else set()
            ours = {n.split(".")[0] for n in ours}
            resolve = _stale.Resolvers(
                command=command, link=link, reference=reference,
                path=_stale.path_resolver(project, where.home, [r for r, _ in files]) if project else None,
                name=_stale.name_resolver(_stale.words_in(text for _, text in files), ours, elsewhere)
                if files else None)
            findings = [f for h in heads for f in _stale.check(h, resolve)]
            legacy = [h["memory"] for h in heads if _cards.parse(h.get("text", "")).legacy]
            stale = [f for f in findings if f.stale]
            look = [f for f in findings if not f.stale]
            stale_total, look_total, legacy_total = stale_total + len(stale), look_total + len(look), legacy_total + len(legacy)
            click.echo(_style.title(f"{name}") + _style.muted(f"  {root}") + "\n  "
                       + f"{len(heads)} memories: "
                       + (_style.bad(f"{len(stale)} stale") if stale else _style.good("0 stale"))
                       + f", {len(look)} to look at, {len(legacy)} not yet a card"
                       + ("" if project else _style.muted("  — no project, so paths and code names are not checked")))
            shown = stale + (look if everything else [])
            for ref in dict.fromkeys(f.memory for f in shown):
                mine = [f for f in shown if f.memory == ref]
                click.echo(f"\n  {_style.strong(ref)} {mine[0].title[:80]}")
                for f in mine:
                    label = (_style.bad("card") if f.stale else
                             _style.muted("card?" if f.part == "card" else "body"))
                    click.echo(f"    {label}  {f.named}\n          {f.problem}")
            if project and global_name and reference is not None:
                cited = _stale.cited_in_code(files, reference)
                if cited:
                    click.echo("\n  " + _style.strong("code citing a superseded global memory"))
                    for at, problem in cited:
                        click.echo(f"    {at}\n          {problem}")
            click.echo("")
        click.echo((_style.bad(f"{stale_total} stale in cards") if stale_total else _style.good("nothing stale in any card"))
                   + f" · {look_total} to look at" + ("" if everything else " (`--all` lists them)")
                   + f" · {legacy_total} memories not yet cards")

    def _standing_for(self, where: Environment, path: str, most: int = 3) -> None:
        """What applies to the space *path* is in — for a hook, at the moment of a write.

        Silent when the store has no trigger map, when the path is in no space it names, or when
        that space holds nothing: a delivery that fires on everything is read as noise and then not
        read at all, which is the same failure as colouring every line (global memory #27)."""
        from _pygim._mcp import _stores, _triggers
        from _pygim._mcp.enact import build

        stores = self._stores(where)
        found = stores.find()
        if found is None or not found.exists:
            return
        tags, term = _triggers.split_term(
            _triggers.tags_for(path, _triggers.load(found.root), _stores.project_root(Path(where.cwd))))
        if not tags:
            return
        server = build(where)
        placed = []
        for scope in ("project", "global"):       # the rule most often missed is the global one
            placed += self._rules_in(server, scope, tags, most, term)
        if not placed:
            return
        subject = f" about {term}" if term else ""
        click.echo(_style.title(f"enact — {path} is {', '.join(tags)}{subject}"))
        for m in placed[:most]:
            point = _triggers.one_line(m.get("text", "")) or (m.get("text", "").split("\n")[0][:110])
            where_from = " (global)" if m.get("scope") == "global" else ""
            click.echo(f"  {_style.strong(m['memory'] + where_from)}  {m['title']}")
            if point:
                click.echo(f"      {_style.muted(point)}")

    def _proposals_waiting(self, where: Environment):
        """Concepts waiting for a human, in every store this machine holds — a proposal raised in
        the global store was invisible here until `proposals` was taught to read its scope."""
        import json as _json

        from _pygim._mcp.enact import build

        server = build(where)
        out = []
        for scope in ("project", "global"):
            answer = server.call("proposals", {"scope": scope})
            if answer.get("isError"):
                continue
            for concept in _json.loads(answer["content"][0]["text"]):
                out.append((scope, concept))
        return out

    @staticmethod
    def _rules_in(server, scope: str, tags, most: int, term: str = ""):
        """What one store has for this space. A store is asked only with tags its own vocabulary
        knows — the global store answers every project and so names almost nothing specific, and a
        tag it has never heard of is a refusal, not a narrower question."""
        import json as _json

        known = server.call("vocabulary", {"scope": scope, "brief": True})
        if known.get("isError"):
            return []
        live = {tag for d in _json.loads(known["content"][0]["text"])["dimensions"] for tag in d["values"]}
        mine = [tag for tag in tags if tag in live]
        if not mine:
            return []
        ask = {"scope": scope, "hard": mine, "max": most, "budget": 700,
               "soft": ["kind=preference", "kind=principle"]}
        if term:
            ask["term"] = term
        answer = server.call("read", ask)
        if answer.get("isError"):
            return []
        body = _json.loads(answer["content"][0]["text"])
        if body.get("refused"):
            return []
        out = list(body.get("memories") or [])
        if body.get("procedure"):
            out.insert(0, body["procedure"])
        for m in out:
            m["scope"] = scope
        return out

    @staticmethod
    def _stores(where: Environment):
        """The machine's stores, built here: one object for the command's whole run, so a command
        that asks twice walks the filesystem once."""
        from _pygim._mcp import _stores

        return _stores.Stores(where)

    @classmethod
    def _store(cls, where: Environment) -> str:
        """The store for this project, or a ClickException that says how to make one."""
        stores = cls._stores(where)
        found = stores.find()
        if found is None or not found.exists:
            raise click.ClickException(stores.guidance())
        return str(found.root)

    def enact_setup(self, *, where: Environment, kind: Optional[str], name: Optional[str], path: Optional[str], source: Optional[str],
                     register: bool) -> None:
        """Find or create the project's store, point the clone at it, and register the server."""
        from _pygim._mcp import _stores

        stores = self._stores(where)
        cwd = Path(where.cwd)
        try:
            if kind == "global":
                root = stores.setup_global(Path(source) if source else None, Path(path) if path else None)
                policy = _stores.policy(root)
                click.echo(f"global store: {root} (sharing: {policy.sharing}, push: {policy.push})")
                click.echo("every project on this machine reads it; write `domain=any` knowledge there with scope global")
                click.echo(f"other machines: give it a git remote, or point them at a clone with "
                           f"`git config --global {_stores.GLOBAL_KEY} <path>`")
                return
            if kind == "user":
                root = stores.setup_user(name, Path(source) if source else None)
                how = "a user-level store"
            elif kind == "local":
                root = stores.setup_local(Path(source) if source else None)
                how = "the project's own .enact"
            elif kind == "branch":
                root = stores.setup_branch(Path(path) if path else None, Path(source) if source else None)
                how = f"the `{_stores.BRANCH}` branch"
            else:
                found = stores.find()
                if found is None or not found.exists:
                    raise click.ClickException("no store yet — choose where it lives: `oo enact setup --user` "
                                               "(your user data directory) or `oo enact setup --branch` "
                                               "(an orphan branch shared through git)")
                root, how = found.root, found.how
        except RuntimeError as exc:
            raise click.ClickException(str(exc)) from exc
        click.echo(f"store: {root} ({how})")
        if (wide := stores.global_root()) is not None:
            click.echo(f"global store: {wide} — read by every project on this machine")
        if _stores.git(["config", "--get", _stores.GIT_KEY], cwd):
            click.echo(f"every worktree of this clone finds it through `git config {_stores.GIT_KEY}`")
        local = Path(cwd) / ".mcp.json"
        if local.is_file() and _stores.SERVER in local.read_text(encoding="utf-8"):
            click.echo(f"note: {local} also defines `{_stores.SERVER}`; a project-scoped entry overrides the user one")
        if register:
            r = _stores.register()
            click.echo(r.message)
            if not r.ran and not r.message.startswith(f"`{_stores.SERVER}` is already"):
                click.echo("  " + " ".join(r.command))

    def enact_stores(self, *, where: Environment, remote: bool) -> None:
        """List the stores a session can name here."""
        from _pygim._mcp import _stores

        stores = self._stores(where)
        scopes = stores.scopes()
        if not scopes:
            click.echo("no store found — run `oo enact setup` in a project, or `oo enact setup --global`")
            return
        for s in scopes:
            policy = _stores.policy(s.root)
            also = f" (also: {', '.join(s.aliases)})" if s.aliases else ""
            click.echo(f"{_style.title(f'{s.name:12}')} {s.root}{also}")
            click.echo(_style.muted(f"{'':12} {s.how} · sharing: {policy.sharing} · push: {policy.push}"))
        if remote:
            here = scopes[0].root
            branches = _stores.remote_stores(here)
            have = {s.name for s in scopes} | {a for s in scopes for a in s.aliases}
            missing = [b for b in branches if b.lower() not in have]
            click.echo(f"in the remote of {here}: {', '.join(branches) or 'nothing'}")
            if missing:
                click.echo(_style.bad("not checked out here: ") + _style.strong(", ".join(missing)))

    def enact_mailbox(self, *, where: Environment, text: Optional[str], kind: str, to: Optional[str], about: Optional[str],
                       resolves: Optional[str], show_all: bool) -> None:
        """List the store's mailbox, or leave a message in it."""
        from pygim.enact import Enact

        memory = Enact(self._store(where))
        if text is not None:
            done = memory.post(text, kind=kind, to=to or "", about=about or "", resolves=resolves or "", author="human")
            if not done["ok"]:
                raise click.ClickException(f"{done['refused']}: {done['message']}")
            click.echo(_style.good("posted ") + _style.muted(done["message"]) +
                       f" — {_style.strong(str(done['waiting']) + ' message(s)')} waiting")
            return
        messages = memory.mailbox(all=show_all)
        if not messages:
            click.echo("nothing waiting" if not show_all else "the mailbox is empty")
            return
        for m in messages:
            who = f" to {m['to']}" if m.get("to") else ""
            about_it = f" · about {m['about']}" if m.get("about") else ""
            closed = _style.good(" · resolved") if show_all and m.get("resolves") else ""
            stamp = _style.muted(f"({m['author']}, {m['time']})")
            click.echo(f"\n{_style.muted(m['id'])}  {_style.title(m['kind'] + who)}{about_it}  {stamp}{closed}")
            click.echo("  " + "\n  ".join(m["text"].splitlines()))

    def enact_reload(self, *, where: Environment, signal_servers: bool = False) -> None:
        """Ask the running MCP servers to restart into the code on disk."""
        from _pygim._mcp import _stores

        asked = self._stores(where).ask_reload(send_signal=signal_servers)
        if asked["signalled"]:
            click.echo(_style.good("signalled ") + _style.strong(f"{len(asked['signalled'])} server(s)") + ": "
                       + ", ".join(str(p) for p in asked["signalled"]))
        for root in asked["marked"]:
            click.echo(_style.good("marked ") + f"{root} {_style.muted('— a server on it reloads at its next call')}")
        if not asked["signalled"] and not asked["marked"]:
            click.echo("no running server found and no store to mark — reconnect the server in your editor instead")
        else:
            click.echo("each server reloads between messages; the call in flight finishes on the old code")
            click.echo("a server older than this feature ignores the marker — if `server_stale` keeps coming back, "
                       "reconnect the client instead")

    def enact_ingest(self, *, where: Environment, corpus: str) -> None:
        """Ingest a hand-written corpus file into the project's store."""
        from pygim.enact import Enact

        result = Enact(self._store(where)).ingest(corpus)
        click.echo(f"{result['added']} added, {result['superseded']} superseded, {result['unchanged']} unchanged")
        for line in result["refused"]:
            click.echo(f"  refused {line}")
        if result["refused"]:
            raise click.exceptions.Exit(1)

    @staticmethod
    def _show_waiting(waiting: dict, index: str = "") -> None:
        """One generalisation as a person needs to see it before answering for it: what it says, and
        which memories stop being placed on their own once it is accepted. The title is the heading,
        the fold count is the thing to weigh, and the key is dim — needed only to act."""
        click.echo(f"\n{_style.muted(index)}{_style.title(waiting['memory'] + '  ' + waiting['title'])}")
        click.echo("  " + "\n  ".join(waiting["text"].strip().splitlines()))
        click.echo(f"\n  tags: {_style.muted(' '.join(waiting['tags']))}")
        click.echo(f"  folds {_style.strong(str(len(waiting['folds'])) + ' memories')}, "
                   f"listed under it instead of placed on their own:")
        for f in waiting["folds"]:
            click.echo(f"    {f['memory']} {f['title']}")
        click.echo(f"  key: {_style.muted(waiting['key'])}")

    def enact_accept(self, *, where: Environment, memory: Optional[str], pack: Optional[str], reason: str, replace: bool,
                      walk: bool = False, assume_yes: bool = False) -> None:
        """Accept a generalisation (*memory*) or a drafted vocabulary pack (*pack*) in the project's store."""
        from pygim.enact import Enact

        if memory is not None and pack is not None:
            raise click.UsageError("accept one thing: a generalisation as MEMORY, or a vocabulary draft with --pack")
        if memory is None and pack is None:
            store = Enact(self._store(where))
            waiting = store.waiting_acceptance()
            proposed = self._proposals_waiting(where)
            if not waiting and not proposed:
                click.echo("nothing is waiting for you")
                return
            if proposed and not walk:
                # A proposal is not accepted by this command — it is accepted by adding the value to
                # a pack file — but saying nothing about it is how it stays pending for a week.
                click.echo(_style.title(f"{len(proposed)} concept(s) the vocabulary lacks") +
                           " — add the value to a pack under taxonomy/, then `oo enact accept --pack`:\n")
                for scope, concept in proposed:
                    where_from = f" ({scope})" if scope != "project" else ""
                    click.echo(f"  {_style.strong(concept['concept'])}{where_from}"
                               f"  {_style.muted((concept.get('dimension') or 'a new dimension') + ' — ' + concept['entry']['brief'])}")
                    asked = concept.get("asked_by") or []
                    if asked:
                        click.echo(f"      {_style.muted('asked by ' + ', '.join(asked))}")
                if not waiting:
                    return
                click.echo("")
            if not walk:
                click.echo(_style.title(f"{len(waiting)} waiting for you") +
                           " — read them with `oo enact accept --all`, which shows each and asks:\n")
                for w in waiting:
                    click.echo(f"  {w['memory']} {w['title']}  {_style.muted('folds ' + str(len(w['folds'])))}")
                return
            accepted = 0
            for n, w in enumerate(waiting, 1):
                self._show_waiting(w, index=f"[{n}/{len(waiting)}] ")
                # Enter accepts: by the time this prompt appears the reader has the whole thing in front
                # of them, and a wrong yes costs one retire, which unfolds the instances again.
                answer = click.prompt("  accept this one? Enter accepts", type=click.Choice(["y", "n", "q"]),
                                      default="y", show_choices=True)
                if answer == "q":
                    break
                if answer == "n":
                    continue
                done = store.accept(w["key"], reason=reason or "read and accepted")
                if not done["ok"]:
                    click.echo(_style.bad(f"  refused: {done['message']}"))
                    continue
                accepted += 1
                click.echo(_style.good(f"  accepted") + f" — {len(w['folds'])} memories now fold under it")
            click.echo(f"\n{_style.strong(f'accepted {accepted} of {len(waiting)}')}; the rest are still waiting")
            return
        if pack is not None:
            from _pygim._mcp import _packs

            from _pygim._mcp import _stores

            done = _packs.accept(Path(self._store(where)), Path(pack).resolve(), replace=replace,
                                 project=_stores.project_root(Path(where.cwd)))
            if not done["ok"]:
                raise click.ClickException(done["errors"])
            click.echo(f"accepted pack `{done['pack']}`: {len(done['dimensions'])} dimension(s), {done['values']} value(s)"
                       + (f"; {len(done['inventory'])} document(s) added to the inventory" if done["inventory"] else ""))
            if done.get("adds"):
                click.echo("added: " + ", ".join(done["adds"]))
            if done["removed"]:
                click.echo("removed values, carried by no memory: " + ", ".join(r["tag"] for r in done["removed"]))
            for warning in done["warnings"]:
                click.echo(f"  locator: {warning}")
            click.echo("a running MCP server picks it up at its next call")
            return
        store = Enact(self._store(where))
        if not assume_yes:                       # a key says nothing; show what is being approved
            match = [w for w in store.waiting_acceptance() if memory in (w["key"], w["memory"]) or w["key"].startswith(memory)]
            if match:
                self._show_waiting(match[0])
                if not click.confirm("  accept this one? Enter accepts", default=True):
                    click.echo("left as it is")
                    return
        result = store.accept(memory, reason=reason or "read and accepted")
        if not result["ok"]:
            raise click.ClickException(f"{result['refused']}: {result['message']}"
                                       + "".join(f"\n  {fact}" for fact in result["facts"]))
        click.echo(_style.good("accepted ") + f"{memory} — its instances fold from the next read "
                   f"{_style.muted('(report: ' + result['report'] + ')')}")

    def enact_status(self, *, where: Environment, standing: bool = False,
                     for_path: Optional[str] = None) -> None:
        """Print where the store stands, the standing knowledge of a session, or — with *for_path* —
        only what applies to the space that path is in."""
        from pygim.enact import Enact

        if for_path:
            self._standing_for(where, for_path)
            return
        if standing:
            from _pygim._mcp.enact import build

            server = build(where)
            data = server.standing()
            waiting = data.get("waiting") or []
            if not data["preferences"] and not data["procedures"] and not waiting:
                return
            click.echo("Standing knowledge from ENACT. " + data["note"])
            if waiting:
                click.echo("\n" + _style.bad(f"Waiting in the mailbox ({len(waiting)})") +
                           " — read them with the mailbox tool:")
                for m in waiting:
                    who = f" to {m['to']}" if m.get("to") else ""
                    click.echo(f"- {m['id']} {m['kind']}{who} ({m['author']}): {m['text'].splitlines()[0][:100]}")
            if data.get("left_out"):
                click.echo(_style.muted(f"To fit, the cards below leave out: {', '.join(data['left_out'])} — "
                                        "`show` any of them for the whole card and the evidence."))
            if data["preferences"]:
                click.echo("\n" + _style.title("Preferences") + _style.muted(" — each a card; `show` one for the rest"))
                for p in data["preferences"]:
                    click.echo(p["card"])
            if data["procedures"]:
                click.echo("\n" + _style.title("Procedures") +
                           _style.muted(" — the steps arrive when a request asks for the task, or with `show`"))
                for p in data["procedures"]:
                    click.echo(p["card"])
            return

        store = self._store(where)
        info = Enact(store).session()
        click.echo(_style.title(str(store)) + f": v{info['version']}, "
                   + _style.strong(f"{info['memories']} memories")
                   + f", vocabulary {_style.muted(info['taxonomy'][:12])}")
        if info.get("waiting_acceptance"):
            click.echo("  " + _style.bad(f"{info['waiting_acceptance']} waiting for you") +
                       " — `oo enact accept --all`")
        if info.get("mailbox"):
            click.echo("  " + _style.bad(f"{len(info['mailbox'])} message(s)") + " — `oo enact mailbox`")
        from _pygim._mcp import _stores

        if (wide := self._stores(where).global_root()) is not None and Path(wide) != Path(store):
            policy = _stores.policy(wide)
            wide_info = Enact(str(wide)).session()
            click.echo(f"{wide}: v{wide_info['version']}, {wide_info['memories']} memories "
                       f"(global, sharing: {policy.sharing}, push: {policy.push})")
        for r in info["reviews"]:
            click.echo(f"  {_style.bad('review')} ({r['kind']}): {r['text']}")
        for p in info["proposals"]:
            click.echo(f"  proposal: {p['dimension'] or '(new dimension)'}={p['concept']} — asked by {', '.join(p['asked_by'])}")

    def stubs(self, *, check: bool = False) -> None:
        """Regenerate (or verify) the generated block of pygim/pathlike.pyi."""
        from pygim import _stubs

        stale = _stubs.update(check=check)
        path = _stubs.stub_path()
        if check:
            click.echo(f"{path}: {'STALE — run `pygim stubs`' if stale else 'up to date'}")
            if stale:
                sys.exit(1)
        else:
            click.echo(f"{path}: {'rewritten' if stale else 'already up to date'}")

    def show_support(self):
        rows = []
        try:
            _ = import_module("pygim._persistence")
            rows.append(("persistence extension", True))
            rows.append(("odbc", True))
            rows.append(("arrow (c++)", True))
        except ImportError:
            rows.append(("persistence extension", False))
        click.echo("Feature support:")
        for name, supported in rows:
            status = "supported" if supported else "missing"
            click.echo(f"- {name}: {status}")
