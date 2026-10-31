# -*- coding: utf-8 -*-
"""
Command-Line Interface Application for Python Gimmicks.
"""

from __future__ import annotations  # `str | None` in signatures on Python 3.9

from subprocess import Popen, DEVNULL
import fnmatch
import os
import sys
import shutil
import functools
from pathlib import Path
from dataclasses import dataclass
from importlib import import_module
import click

from _pygim._config import Environment
from _pygim._cli import _style
from pygim.pathlike import path

__all__ = ["GimmicksCliApp", "flag_opt"]


def _echo(msg, quiet):
    if not quiet:
        click.echo(msg)


flag_opt = functools.partial(click.option, is_flag=True, default=False)

def _hidden(parts) -> bool:
    """Whether a walked path is under a directory a walk of sources passes over: a dot directory or
    __pycache__ (`parts` below the walk's root)."""
    return any(part.startswith(".") or part == "__pycache__" for part in parts[:-1])


def _excluded(parts, patterns) -> bool:
    """Whether -x patterns leave a path out: a pattern matching the path (`parts` joined) or one of
    its components."""
    return any(fnmatch.fnmatch(os.path.join(*parts), pat) or any(fnmatch.fnmatch(part, pat) for part in parts)
               for pat in patterns)


def _side(pc, pairs, x):
    """One side of the similar table: `qualname  file:line` per pair, joined in Arrow's kernels
    (a dictionary's to_pylist wraps every value). `pc` is pyarrow.compute, which only similar()
    loads (see there)."""
    where = pc.binary_join_element_wise(pairs[f"{x}_origin"].cast("string"), pairs[f"{x}_line"].cast("string"), ":")
    return pc.binary_join_element_wise(pairs[f"{x}_qualname"].cast("string"), where, "  ").to_pylist()


def _plain_table(headers, columns) -> str:
    """`columns` (one list of strings each) under `headers` as tabulate's plain table lays text out —
    tabulate(rows, headers, disable_numparse=True): each column as wide as its widest cell and at
    least its header and two more, cells left-aligned and two spaces apart, the last unpadded, a
    rule of dashes under the headers. A line at a time, where tabulate took 0.5 s for 23,000 rows."""
    widths = [max(len(h) + 2, max(map(len, c), default=0)) for h, c in zip(headers, columns)]
    padded = [[cell.ljust(w) for cell in c] for c, w in zip(columns[:-1], widths)]   # a column at a time: 48,000 rows 100 -> 25 ms
    head = "  ".join([*(h.ljust(w) for h, w in zip(headers[:-1], widths)), headers[-1]])
    return "\n".join([head, "  ".join("-" * w for w in widths), *map("  ".join, zip(*padded, columns[-1]))])


@dataclass
class GimmicksCliApp:
    def clean_up(self, yes, build_dirs, pycache_dirs, compiled_files, quiet, all):
        root = path(os.getcwd())
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
                   rebuild: str | None = None, reload: bool = True) -> None:
        """Serve a docs directory locally with the ✎ commenter and image-drop endpoint; with
        *reload*, it restarts itself when its own code changes.

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
            _docs_serve.serve(doc_root, port=port, host=host, index=index, store=store, reload=reload)
        except (FileNotFoundError, _docs_serve.ServeError) as exc:
            raise click.ClickException(str(exc)) from exc

    def inventory(self, *, where: Environment, path: str | None, brief: bool = False) -> None:
        """What is already at hand in a project, against what its code actually reaches for.

        The point is the join, not either list: a component you already have is invisible at the
        call site, so nobody notices reaching past it. Printed, never failed — a first run finds
        things that are fine on purpose, and a check that cries wolf is read once and then never
        again. What should fail is decided after the exemptions are written down."""
        from _pygim import _inventory
        from _pygim._inventory import canonical

        import pygim

        root = Path(path) if path else Path(where.cwd)
        if not pygim.path(str(root)).is_dir():
            raise click.ClickException(f"{root} is not a directory")
        if brief:
            click.echo(_inventory.project_map(root, home=Path(where.home)))
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
        def library(name):
            dist = found.provider(name)
            return name if canonical(dist) == canonical(name) else f"{name} ({dist})"

        band("declared as a dependency, installed, never imported", list(found.declared_unused),
             "from pyproject.toml")
        band("declared, and run rather than imported", list(found.declared_run),
             "a program or a pytest plugin — never imported, by design")
        band("declared, and not installed here", list(found.declared_absent),
             "cannot be judged from this environment")
        band("imported, and nothing here provides it",
             [f"{name:<28}{use.total:>4}" for name, use in found.unresolved.items()],
             "a sibling checkout or a missing install")
        band("available here, and the work uses it",
             [f"{library(name):<28}{use.work:>4} in the work{('  +' + str(use.shown) + ' shown') if use.shown else ''}"
              for name, use in found.third_party_in_work],
             "what this project is built with from elsewhere")
        band("available here, used only by tests, examples and benchmarks",
             [f"{library(name):<28}{use.shown:>4} shown" for name, use in found.third_party_shown_only],
             "test runners and answer keys — they check what is built, and are not part of it")

        idle = found.unused + found.shown_only
        if idle:
            click.echo(_style.bad(f"{len(idle)} of {len(found.ships)} shipped module(s) are not "
                                  f"used by this project's own code."))
            click.echo(_style.muted("Each is a decision — worth recording either way. Nothing here "
                                    "fails; the join is the finding."))
        elif found.ships:
            click.echo(_style.good("every shipped module is used by this project's own code."))

    @staticmethod
    def _capture(run) -> str:
        """What a command would have printed, as text; nothing when it ended in a click error."""
        import contextlib
        import io

        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            try:
                run()
            except click.ClickException:
                return ""
        return buffer.getvalue()

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

    def similar(self, *, paths, threshold: float = 0.9, kind: str = "function", min_size: int = 20,
                limit: int | None = None, exclude=()) -> int:
        """Print every pair of *kind* units of at least *min_size* nodes
        scoring at least *threshold*, per
        language: one ``similarity.Index`` per language found under *paths*,
        so a Python function is never compared with a C++ one. A file its
        frontend cannot read is reported and skipped. Returns the number of
        pairs printed."""
        if threshold == 0 and not limit:   # refused before anything is read: the ranking would hold every pair first
            raise click.UsageError("-t 0 shows every pair of every unit: give -n N for the best N, or a threshold above 0")
        # Loaded here, not with the module: every oo and pygim command imports this module, and these
        # cost ~60 ms of its start (2026-10-29).
        import pyarrow.compute as pc
        from pygim import similarity

        files = {}   # each file once, by its first spelling: overlapping roots name one tree twice
        for root in map(Path, paths or ["."]):
            if root.is_file():
                if not _excluded((root.name,), exclude) and not _excluded((str(root),), exclude):
                    files.setdefault(os.path.realpath(root), str(root))
            elif root.is_dir():
                top = path(str(root))   # pygim's own walker and its own paths: pathlib's relative_to took 0.4 s on CPython's Lib
                for p in top.rglob("*"):
                    parts = p.relative_to(top).parts
                    if p.is_file() and not _hidden(parts) and not _excluded(parts, exclude):
                        files.setdefault(os.path.realpath(str(p)), str(p))
            else:
                raise click.ClickException(f"{root}: is neither a file nor a directory")

        by_language = {}
        for f in sorted(files.values(), key=lambda f: os.path.normcase(f).split(os.sep)):   # pathlib's order: its parts, case-folded where the OS folds
            language = similarity.language_of(f)
            if language is not None:
                by_language.setdefault(language, []).append(f)
        # Each language's files read, parsed and fingerprinted on every core, in this order (the
        # ids add_file gives them one by one); what a frontend cannot read is reported, by path.
        indexes, left_out = {}, []
        for language, paths_of in by_language.items():
            indexes[language] = similarity.Index(language)
            left_out += indexes[language].add_files(paths_of)
        for _, message in sorted(left_out):
            click.echo(f"skipped: {message}", err=True)

        shown = 0
        for language in sorted(indexes):
            # A pyarrow.Table, best first; with a limit the sweep keeps (and computes) only the best.
            pairs = indexes[language].pairs(kind, threshold, min_size, limit=limit or 0, columns=[
                "score", "exact", "a_qualname", "a_origin", "a_line", "b_qualname", "b_origin", "b_line"])
            if not pairs.num_rows:
                continue
            how_many = f"the best {pairs.num_rows}" if limit and pairs.num_rows == limit else f"{pairs.num_rows}"
            click.echo(f"\n{language}: {how_many} {kind} pair(s) scoring >= {threshold}")
            score = [f"{s:.3f}" + (" =" if e else "") for s, e in zip(pairs["score"].to_pylist(), pairs["exact"].to_pylist())]
            click.echo(_plain_table(("score", "a", "b"), [score, _side(pc, pairs, "a"), _side(pc, pairs, "b")]))
            shown += pairs.num_rows
        if not shown:
            click.echo(f"No {kind} pairs scoring >= {threshold}.")
        return shown

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
