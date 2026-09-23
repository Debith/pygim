# -*- coding: utf-8 -*-
"""What is already at hand here, against what this code actually reaches for.

Something you already have is invisible at the call site. `from pathlib import Path` reads as
correct Python whether or not the project ships a path library of its own, whether or not one of
its dependencies does. There is no error, no smell and no diff — the gap exists only in the join
between two lists, what is available and what is imported, which no file displays and nobody holds
in their head. Two models reviewed the same file in depth on 2026-09-23 and neither saw it, both
with the knowledge one query away (global memory #20). So it is computed, not remembered.

Nothing here knows about any particular project. `survey` is the judgement and is a pure function
of plain values; `from_machine` is the only part that looks at a machine, and it is called where
the command is wired. That split is what lets the interesting half be tested without arranging an
environment around it (global memory #9).

Use by a component's own tests and examples is counted apart from use by the project's own work,
because they answer different questions. A test or an example written *for* a component is
admiring it from outside; the feedback that makes dogfooding worth anything only arrives when
the component is used for something else.
"""
from __future__ import annotations

import ast
from dataclasses import dataclass
from pathlib import Path
from typing import Collection, Dict, Iterable, List, Mapping, NamedTuple, Optional, Sequence, Set, Tuple

# Where a project keeps code it offers to an importer, in the order a reader would look.
SOURCE = ("src", ".")
# Never a thing the project ships, whatever it looks like.
NOT_SHIPPED = {"tests", "test", "docs", "doc", "build", "dist", "examples", "benchmarks", "scripts"}
# Never this project's own code, whatever is found there.
NOT_OURS = {".git", "build", "dist", "__pycache__", "third_party", "node_modules",
            ".venv", "venv", ".tox", ".nox", ".mypy_cache", ".pytest_cache", "site-packages"}


def _parents(name: str) -> List[str]:
    """Every package a dotted name sits inside: `a.b.c` is vouched for by `a.b` and `a`."""
    parts = name.split(".")
    return [".".join(parts[:depth]) for depth in range(1, len(parts))]


class Use(NamedTuple):
    """How often a name is imported, split by what the importing file is for."""

    work: int = 0      # files that build the thing the project exists to build
    shown: int = 0     # tests and examples, written *for* the component rather than *with* it

    @property
    def total(self) -> int:
        return self.work + self.shown

    @property
    def only_shown(self) -> bool:
        """Exercised and demonstrated, never used — the case that looks like adoption and is not."""
        return self.shown > 0 and self.work == 0

    def plus(self, other: "Use") -> "Use":
        return Use(self.work + other.work, self.shown + other.shown)


@dataclass(frozen=True)
class Inventory:
    """What this project has, and what it uses."""

    root: Path
    files: int
    ships: Mapping[str, Use]
    third_party: Mapping[str, Use]
    standard: Mapping[str, Use]
    local: Mapping[str, Use]
    unresolved: Mapping[str, Use]
    declared_unused: Sequence[str]

    def _idle(self, pick) -> List[str]:
        """Shipped names matching *pick*, minus any package a used submodule already vouches for.

        `import lib.paths` runs `lib/__init__.py`, so reporting the bare package as untouched while
        one of its modules is in daily use is an artefact of listing both, not a finding — and a
        report with one false row in it gets read exactly once."""
        vouched = {parent for name, use in self.ships.items() if use.total
                   for parent in _parents(name)}
        return [name for name, use in self.ships.items() if pick(use) and name not in vouched]

    @property
    def unused(self) -> List[str]:
        return self._idle(lambda use: not use.total)

    @property
    def shown_only(self) -> List[str]:
        return self._idle(lambda use: use.only_shown)

    @property
    def used(self) -> List[Tuple[str, Use]]:
        return [(name, use) for name, use in self.ships.items() if use.work]


def survey(*, imports: Mapping[str, Use], ships: Sequence[str], available: Mapping[str, str],
           local: Collection[str] = (), declared: Sequence[str] = (), stdlib: Collection[str] = (),
           root: Path = Path("."), files: int = 0) -> Inventory:
    """Sort every imported name by where it came from, and every shipped name by who uses it.

    *imports* counts dotted module names; *ships* names this project offers an importer; *local*
    names modules the project contains but does not ship; *available* maps a top-level module to the
    distribution that installed it; *declared* names distributions a manifest asked for.

    A name nothing accounts for lands in `unresolved` rather than quietly among the third parties:
    an answer that stays silent about what it could not place cannot be told from a complete one.
    """
    shipped: Dict[str, Use] = {name: Use() for name in ships}
    third_party: Dict[str, Use] = {}
    standard: Dict[str, Use] = {}
    ours: Dict[str, Use] = {}
    unresolved: Dict[str, Use] = {}

    def add(where: Dict[str, Use], key: str, use: Use) -> None:
        where[key] = where.get(key, Use()).plus(use)

    for name, use in imports.items():
        head = name.split(".")[0]
        if name in shipped:
            add(shipped, name, use)
        elif head in shipped:
            add(shipped, head, use)
        elif head in local:
            add(ours, head, use)
        elif head in stdlib:
            add(standard, head, use)
        elif head in available:
            add(third_party, available[head], use)
        else:
            add(unresolved, head, use)

    imported = {name.split(".")[0] for name in imports}
    reached = {available[head] for head in imported if head in available} | imported
    by_use = lambda item: (-item[1].total, item[0])
    return Inventory(root=Path(root), files=files,
                     ships=dict(sorted(shipped.items())),
                     third_party=dict(sorted(third_party.items(), key=by_use)),
                     standard=dict(sorted(standard.items(), key=by_use)),
                     local=dict(sorted(ours.items(), key=by_use)),
                     unresolved=dict(sorted(unresolved.items(), key=by_use)),
                     declared_unused=sorted(d for d in declared
                                            if d not in reached and d.replace("-", "_") not in reached))


# ── gathering the facts, which is the half that has to touch a machine ────────


SHOWS = {"tests", "test", "docs", "doc", "examples", "example", "benchmarks", "benchmark", "samples"}


def is_shown(relative: str) -> bool:
    """Whether a file, named by its path within the project, demonstrates rather than builds.

    Tests, examples and benchmarks are all written *for* the component they import. Counting them
    as use is what makes an unused component look adopted: pygim's `ioc` container has three
    importers and every one of them is an example of the container.
    """
    parts = relative.split("/")
    name = parts[-1]
    return (bool(SHOWS & set(parts[:-1]))
            or name.startswith("test_") or name.endswith("_test.py") or name == "conftest.py")


def python_files(root: Path) -> List[Tuple[str, str]]:
    """Every .py file the project owns, as a path relative to *root* with its text.

    Walked and read by pygim's own path table: a tool whose whole subject is using what a project
    ships would be a poor joke if it walked the tree with pathlib. `read_all_files` skips a member
    that is not a regular file, so its list can be shorter than the membership, and the two are
    checked against each other rather than trusted.
    """
    from pygim.pathlike import PathSet, path

    found = PathSet(path(str(root)).rglob("*.py"))
    members, texts = found.to_list(), found.read_all_files()
    if len(members) != len(texts):
        return []
    here = Path(root)
    out = []
    for member, text in zip(members, texts):
        file = Path(str(member))
        relative = (file.relative_to(here) if file.is_relative_to(here) else file).as_posix()
        if not (NOT_OURS & set(relative.split("/"))):
            out.append((relative, text))
    return out


def ships_in(root: Path) -> List[str]:
    """What this project offers an importer: its public top-level packages and their submodules.

    Walked with pygim's own path objects, like everything else here. A package is a directory with
    an `__init__.py` under `src/` or beside the project, and its submodules are the public `.py` files, compiled extensions and subpackages one level inside —
    one level, because that is the depth people import from. A project that ships nothing (an
    application, a folder of scripts) gets an empty list, which is the right answer, not an error.
    """
    from pygim.pathlike import path

    names: List[str] = []
    for source in SOURCE:
        here = path(str(root)) / source
        if not here.is_dir():
            continue
        for child in sorted(here.iterdir(), key=str):
            if (not child.is_dir() or child.name.startswith((".", "_"))
                    or child.name in NOT_SHIPPED or not (child / "__init__.py").exists()):
                continue
            inside = child.iterdir()
            names.append(child.name)
            names.extend(f"{child.name}.{part}" for part in sorted(
                {member.name.split(".")[0] for member in inside
                 if member.suffix in (".py", ".so", ".pyd") and not member.name.startswith("_")}
                | {member.name for member in inside if member.is_dir()
                   and not member.name.startswith((".", "_")) and (member / "__init__.py").exists()}))
    return sorted(set(names))


def local_in(files: Iterable[Tuple[str, str]]) -> Set[str]:
    """Top-level names the project itself provides — its private packages and its loose modules.

    Without this a project's own internals are reported as somebody else's library, because the
    installed distribution's metadata claims them: pygim's `top_level.txt` names `_pygim`, so 40
    imports of the project's own implementation package were filed under a third-party dependency.
    """
    names: Set[str] = set()
    for relative, _ in files:
        parts = relative.split("/")
        names.add(parts[-1][:-3])
        for depth, part in enumerate(parts[:-1]):
            if part not in SOURCE and part not in NOT_SHIPPED:
                names.add(part)
    return names


def imports_in(files: Iterable[Tuple[str, str]], ships: Sequence[str]) -> Dict[str, Use]:
    """Every module imported by *files*, counted, and split between the project and its tests.

    A package importing its own parts is not a use of them by the project — it is the package being
    itself — so an import of `x.y` from inside the `x/` directory is not counted. Matching that
    directory has to be done on the path *within* the project: a repository whose own folder is
    named after the package it ships would otherwise swallow every import in the tree.
    """
    tops = {name.split(".")[0] for name in ships}
    counted: Dict[str, Use] = {}
    for relative, text in files:
        try:
            tree = ast.parse(text, filename=relative)
        except SyntaxError:
            continue
        parts = relative.split("/")[:-1]
        inside = next((part for part in parts if part in tops), None)
        one = Use(shown=1) if is_shown(relative) else Use(work=1)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                named = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                named = [node.module]
            else:
                continue
            for name in named:
                if name.split(".")[0] == inside:
                    continue
                counted[name] = counted.get(name, Use()).plus(one)
    return counted


def installed(distributions) -> Dict[str, str]:
    """Top-level module name to the distribution that provides it, for what is importable here."""
    found: Dict[str, str] = {}
    for dist in distributions:
        name = dist.metadata["Name"] if dist.metadata else None
        if not name:
            continue
        tops = (dist.read_text("top_level.txt") or "").split()
        if not tops:
            tops = sorted({Path(str(f)).parts[0] for f in (dist.files or [])
                           if not str(f).startswith(("..", "__pycache__"))
                           and Path(str(f)).suffix in (".py", ".so", ".pyd")})
        for top in tops:
            found.setdefault(top.removesuffix(".py"), name)
    return found


def declared_in(root: Path) -> List[str]:
    """The distributions a manifest asks for, or nothing when the project has no manifest.

    Many projects have none — the dnd project on this machine imports pygim and declares nothing —
    so an empty list is ordinary, and must not be read as "this project declares no dependencies".
    """
    manifest = root / "pyproject.toml"
    if not manifest.is_file():
        return []
    import re
    import tomllib

    try:
        data = tomllib.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []
    project = data.get("project", {})
    wanted = list(project.get("dependencies", []) or [])
    for extra in (project.get("optional-dependencies", {}) or {}).values():
        wanted.extend(extra)
    return sorted({re.split(r"[<>=!\[~; ]", line.strip())[0] for line in wanted if line.strip()})


def from_machine(root: Path, distributions=None, stdlib: Optional[Collection[str]] = None) -> Inventory:
    """`survey` against a real directory and a real environment. Called where a command is wired."""
    import sys
    from importlib import metadata

    files = python_files(root)
    ships = ships_in(root)
    return survey(imports=imports_in(files, ships), ships=ships, local=local_in(files),
                  available=installed(metadata.distributions() if distributions is None else distributions),
                  declared=declared_in(root),
                  stdlib=sys.stdlib_module_names if stdlib is None else stdlib,
                  root=root, files=len(files))


TEXT = (".py", ".pyi", ".h", ".hpp", ".cpp", ".c", ".md", ".rst", ".toml", ".yaml", ".yml", ".json",
        ".txt", ".cfg", ".ini", ".sh", ".cmake")


def text_files(root: Path) -> List[Tuple[str, str]]:
    """Every text file the project keeps, as a path relative to *root* with its text.

    In a git checkout that is what `git ls-files` lists, so ignored build output, virtual
    environments and caches are left out by the project's own rules rather than by a guess; outside
    one, the walk falls back to everything not in `NOT_OURS`. Read by pygim's path table either way.
    """
    from pygim.pathlike import PathSet, path
    from _pygim._mcp._stores import git

    listed = git(["ls-files", "-z"], Path(root))
    if listed is not None:
        names = [n for n in listed.split("\0") if n and n.endswith(TEXT)]
        found = PathSet([path(str(Path(root) / n)) for n in names])
    else:
        found = PathSet(path(str(root)).rglob("*"))
    members, texts = found.to_list(), found.read_all_files()
    if len(members) != len(texts):
        return []
    here, out = Path(root), []
    for member, text in zip(members, texts):
        file = Path(str(member))
        relative = (file.relative_to(here) if file.is_relative_to(here) else file).as_posix()
        if relative.endswith(TEXT) and not (NOT_OURS & set(relative.split("/"))):
            out.append((relative, text if isinstance(text, str) else text.decode("utf-8", "replace")))
    return out
