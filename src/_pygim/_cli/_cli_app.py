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

    def ai(self, text):
        print("AI is not implemented yet!")

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

    def enact_mcp(self, *, root: Optional[str]) -> None:
        """Serve the project's store over MCP on stdio; the server starts even without one."""
        from _pygim._mcp import enact as server
        from pygim.enact import VocabularyError

        try:
            server.run(root)
        except (RuntimeError, VocabularyError) as exc:
            raise click.ClickException(str(exc)) from exc

    def enact_call(self, *, name: str, arguments: Optional[str], session: Optional[int],
                   root: Optional[str]) -> None:
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

        if session is not None:      # every process is its own session unless one is named
            os.environ[server.SESSION_ENV] = str(session)
        made = server.EnactServer(root=root)
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

    @staticmethod
    def _store(root: Optional[str]) -> str:
        """The store for this project, or a ClickException that says how to make one."""
        from _pygim._mcp import _stores

        found = _stores.find(root)
        if found is None or not found.exists:
            raise click.ClickException(_stores.guidance())
        return str(found.root)

    def enact_setup(self, *, kind: Optional[str], name: Optional[str], path: Optional[str], source: Optional[str],
                     register: bool) -> None:
        """Find or create the project's store, point the clone at it, and register the server."""
        from _pygim._mcp import _stores

        cwd = Path.cwd()
        try:
            if kind == "global":
                root = _stores.setup_global(Path(source) if source else None, Path(path) if path else None)
                policy = _stores.policy(root)
                click.echo(f"global store: {root} (sharing: {policy.sharing}, push: {policy.push})")
                click.echo("every project on this machine reads it; write `domain=any` knowledge there with scope global")
                click.echo(f"other machines: give it a git remote, or point them at a clone with "
                           f"`git config --global {_stores.GLOBAL_KEY} <path>`")
                return
            if kind == "user":
                root = _stores.setup_user(cwd, name, Path(source) if source else None)
                how = "a user-level store"
            elif kind == "local":
                root = _stores.setup_local(cwd, Path(source) if source else None)
                how = "the project's own .enact"
            elif kind == "branch":
                root = _stores.setup_branch(cwd, Path(path) if path else None, Path(source) if source else None)
                how = f"the `{_stores.BRANCH}` branch"
            else:
                found = _stores.find(cwd=cwd)
                if found is None or not found.exists:
                    raise click.ClickException("no store yet — choose where it lives: `oo enact setup --user` "
                                               "(your user data directory) or `oo enact setup --branch` "
                                               "(an orphan branch shared through git)")
                root, how = found.root, found.how
        except RuntimeError as exc:
            raise click.ClickException(str(exc)) from exc
        click.echo(f"store: {root} ({how})")
        if (wide := _stores.find_global()) is not None:
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

    def enact_stores(self, *, remote: bool, root: Optional[str]) -> None:
        """List the stores a session can name here."""
        from _pygim._mcp import _stores

        scopes = _stores.discover(Path.cwd(), root)
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

    def enact_mailbox(self, *, text: Optional[str], kind: str, to: Optional[str], about: Optional[str],
                       resolves: Optional[str], show_all: bool, root: Optional[str]) -> None:
        """List the store's mailbox, or leave a message in it."""
        from pygim.enact import Enact

        memory = Enact(self._store(root))
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

    def enact_reload(self, *, signal_servers: bool = False) -> None:
        """Ask the running MCP servers to restart into the code on disk."""
        from _pygim._mcp import _stores

        asked = _stores.ask_reload(Path.cwd(), send_signal=signal_servers)
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

    def enact_ingest(self, *, corpus: str, root: Optional[str]) -> None:
        """Ingest a hand-written corpus file into the project's store."""
        from pygim.enact import Enact

        result = Enact(self._store(root)).ingest(corpus)
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

    def enact_accept(self, *, memory: Optional[str], pack: Optional[str], reason: str, replace: bool,
                      walk: bool = False, assume_yes: bool = False, root: Optional[str]) -> None:
        """Accept a generalisation (*memory*) or a drafted vocabulary pack (*pack*) in the project's store."""
        from pygim.enact import Enact

        if memory is not None and pack is not None:
            raise click.UsageError("accept one thing: a generalisation as MEMORY, or a vocabulary draft with --pack")
        if memory is None and pack is None:
            store = Enact(self._store(root))
            waiting = store.waiting_acceptance()
            if not waiting:
                click.echo("nothing is waiting for you")
                return
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

            done = _packs.accept(Path(self._store(root)), Path(pack).resolve(), replace=replace,
                                 project=_stores.project_root(Path.cwd()))
            if not done["ok"]:
                raise click.ClickException(done["errors"])
            click.echo(f"accepted pack `{done['pack']}`: {len(done['dimensions'])} dimension(s), {done['values']} value(s)"
                       + (f"; {len(done['inventory'])} document(s) added to the inventory" if done["inventory"] else ""))
            if done["removed"]:
                click.echo("removed values, carried by no memory: " + ", ".join(r["tag"] for r in done["removed"]))
            for warning in done["warnings"]:
                click.echo(f"  locator: {warning}")
            click.echo("a running MCP server picks it up at its next call")
            return
        store = Enact(self._store(root))
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

    def enact_status(self, *, root: Optional[str], standing: bool = False) -> None:
        """Print where the repository at *root* stands, or the standing knowledge of a session there."""
        from pygim.enact import Enact

        if standing:
            from _pygim._mcp.enact import EnactServer

            server = EnactServer(root=root, cwd=Path.cwd())
            data = server.standing()
            waiting = data.get("waiting") or []
            if not data["preferences"] and not data["procedures"] and not waiting:
                return
            click.echo("Standing knowledge from pygim memory. " + data["note"])
            if waiting:
                click.echo("\n" + _style.bad(f"Waiting in the mailbox ({len(waiting)})") +
                           " — read them with the mailbox tool:")
                for m in waiting:
                    who = f" to {m['to']}" if m.get("to") else ""
                    click.echo(f"- {m['id']} {m['kind']}{who} ({m['author']}): {m['text'].splitlines()[0][:100]}")
            for p in data["preferences"]:
                where = " (global)" if p["scope"] == "global" else ""
                click.echo("\n" + _style.title(f"## {p['memory']}{where} {p['title']}") + f"\n{p['text']}")
            if data["procedures"]:
                click.echo("\nProcedures — a read naming their artifact and task places the steps first:")
                for p in data["procedures"]:
                    click.echo(f"- {p['memory']} {p['title']} — {p['where']}")
            return

        store = self._store(root)
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

        if (wide := _stores.find_global()) is not None and Path(wide) != Path(store):
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
