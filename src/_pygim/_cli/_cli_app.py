# -*- coding: utf-8 -*-
"""
Command-Line Interface Application for Python Gimmicks.
"""

from __future__ import annotations  # `str | None` in signatures on Python 3.9

from subprocess import Popen, DEVNULL
import sys
import shutil
import functools
from typing import Optional
from pathlib import Path
from dataclasses import dataclass
from importlib import import_module
import click

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

    def memory_init(self, *, root: str) -> None:
        """Create a memory repository at *root* with the base vocabulary."""
        from pygim.memory import Memory

        try:
            Memory.init(root)
        except RuntimeError as exc:
            raise click.ClickException(str(exc)) from exc
        m = Memory(root)
        click.echo(f"created {m.root} at v{m.version} — add a pack under taxonomy/, or start with the base")

    def memory_mcp(self, *, root: Optional[str]) -> None:
        """Serve the project's store over MCP on stdio; the server starts even without one."""
        from _pygim._mcp import memory as server
        from pygim.memory import VocabularyError

        try:
            server.run(root)
        except (RuntimeError, VocabularyError) as exc:
            raise click.ClickException(str(exc)) from exc

    @staticmethod
    def _store(root: Optional[str]) -> str:
        """The store for this project, or a ClickException that says how to make one."""
        from _pygim._mcp import _stores

        found = _stores.find(root)
        if found is None or not found.exists:
            raise click.ClickException(_stores.guidance())
        return str(found.root)

    def memory_setup(self, *, kind: Optional[str], name: Optional[str], path: Optional[str], source: Optional[str],
                     register: bool) -> None:
        """Find or create the project's store, point the clone at it, and register the server."""
        from _pygim._mcp import _stores

        cwd = Path.cwd()
        try:
            if kind == "user":
                root = _stores.setup_user(cwd, name, Path(source) if source else None)
                how = "a user-level store"
            elif kind == "branch":
                root = _stores.setup_branch(cwd, Path(path) if path else None, Path(source) if source else None)
                how = f"the `{_stores.BRANCH}` branch"
            else:
                found = _stores.find(cwd=cwd)
                if found is None or not found.exists:
                    raise click.ClickException("no store yet — choose where it lives: `oo memory setup --user` "
                                               "(your user data directory) or `oo memory setup --branch` "
                                               "(an orphan branch shared through git)")
                root, how = found.root, found.how
        except RuntimeError as exc:
            raise click.ClickException(str(exc)) from exc
        click.echo(f"store: {root} ({how})")
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

    def memory_accept_pack(self, *, proposal: str, replace: bool, root: Optional[str]) -> None:
        """Check a drafted pack and make it live in the project's store."""
        from _pygim._mcp import _packs

        store = Path(self._store(root))
        result = _packs.accept(store, Path(proposal).resolve(), replace=replace)
        if not result["ok"]:
            raise click.ClickException(result["errors"])
        click.echo(f"accepted pack `{result['pack']}`: {len(result['dimensions'])} dimension(s), {result['values']} value(s)"
                   + (f"; {len(result['inventory'])} document(s) added to the inventory" if result["inventory"] else ""))
        click.echo("a running MCP server picks it up at its next call")

    def memory_ingest(self, *, corpus: str, root: Optional[str]) -> None:
        """Ingest a hand-written corpus file into the project's store."""
        from pygim.memory import Memory

        result = Memory(self._store(root)).ingest(corpus)
        click.echo(f"{result['added']} added, {result['superseded']} superseded, {result['unchanged']} unchanged")
        for line in result["refused"]:
            click.echo(f"  refused {line}")
        if result["refused"]:
            raise click.exceptions.Exit(1)

    def memory_accept(self, *, memory: str, reason: str, root: Optional[str]) -> None:
        """Accept a generalisation in the repository at *root*."""
        from pygim.memory import Memory

        result = Memory(self._store(root)).accept(memory, reason=reason)
        if not result["ok"]:
            raise click.ClickException(f"{result['refused']}: {result['message']}"
                                       + "".join(f"\n  {fact}" for fact in result["facts"]))
        click.echo(f"accepted {memory} — its instances fold from the next read (report: {result['report']})")

    def memory_status(self, *, root: Optional[str]) -> None:
        """Print where the repository at *root* stands."""
        from pygim.memory import Memory

        store = self._store(root)
        info = Memory(store).session()
        click.echo(f"{store}: v{info['version']}, {info['memories']} memories, vocabulary {info['taxonomy'][:12]}")
        for r in info["reviews"]:
            click.echo(f"  review ({r['kind']}): {r['text']}")
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
