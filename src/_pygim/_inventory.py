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

import csv
import re
import subprocess
import sys
from dataclasses import dataclass, field
from importlib import metadata
from pathlib import Path
from typing import Collection, Dict, Iterable, List, Mapping, NamedTuple, Optional, Sequence, Set, Tuple
from urllib.parse import urlparse
from urllib.request import url2pathname

from pygim.code import parse_source
from pygim.pathlike import PathSet, path

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
    declared_unused: Sequence[str]                 # installed here, and nothing imports it
    declared_run: Sequence[str] = ()               # installed as a program or a pytest plugin: run, not imported
    declared_absent: Sequence[str] = ()            # not installed where the report ran, so not judged
    providers: Mapping[str, str] = field(default_factory=dict)   # an imported name -> its distribution

    @property
    def third_party_in_work(self) -> List[Tuple[str, Use]]:
        """Libraries the project's own code uses — what it is built with."""
        return [(name, use) for name, use in self.third_party.items() if use.work]

    @property
    def third_party_shown_only(self) -> List[Tuple[str, Use]]:
        """Libraries only tests, examples and benchmarks import: test runners and answer keys, such
        as PyYAML for pathlike's YAML engine — used to check what is built, not to build it."""
        return [(name, use) for name, use in self.third_party.items() if use.only_shown]

    def provider(self, name: str) -> str:
        """The distribution that installs the module *name* — ``yaml`` comes from ``PyYAML``."""
        return self.providers.get(name, name)

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


def canonical(name: str) -> str:
    """A distribution name as packaging compares them: case ignored, and runs of ``-``, ``_`` and
    ``.`` alike (PEP 503) — ``pyyaml`` in a manifest is ``PyYAML`` in its metadata."""
    return re.sub(r"[-_.]+", "-", name).lower()


def survey(*, imports: Mapping[str, Use], ships: Sequence[str], available: Mapping[str, str],
           local: Collection[str] = (), declared: Sequence[str] = (), stdlib: Collection[str] = (),
           installed: Optional[Collection[str]] = None, commands: Collection[str] = (),
           root: Path = Path("."), files: int = 0) -> Inventory:
    """Sort every imported name by where it came from, and every shipped name by who uses it.

    *imports* counts dotted module names; *ships* names this project offers an importer; *local*
    names modules the project contains but does not ship; *available* maps a top-level module to the
    distribution that installed it; *declared* names distributions a manifest asked for; *installed*
    names the distributions present where this runs (None: not known), and *commands* those among
    them that install a program or a pytest plugin — both in `canonical` form.

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
        # the nearest package that is shipped: `lib.sub.mod` is a use of `lib.sub` when it ships, else of `lib`
        owner = next((p for p in (name, *reversed(_parents(name))) if p in shipped), None)
        if owner is not None:
            add(shipped, owner, use)
        elif head in local:
            add(ours, head, use)
        elif head in stdlib:
            add(standard, head, use)
        elif head in available:
            add(third_party, head, use)                  # the name the code says, not the distribution's
        else:
            add(unresolved, head, use)

    imported = {name.split(".")[0] for name in imports}
    reached = {canonical(available[head]) for head in imported if head in available} | {canonical(i) for i in imported}
    idle = [d for d in declared if canonical(d) not in reached]
    absent = [d for d in idle if installed is not None and canonical(d) not in installed]
    run = [d for d in idle if d not in absent and canonical(d) in commands]
    by_use = lambda item: (-item[1].total, item[0])
    return Inventory(root=Path(root), files=files,
                     ships=dict(sorted(shipped.items())),
                     third_party=dict(sorted(third_party.items(), key=by_use)),
                     standard=dict(sorted(standard.items(), key=by_use)),
                     local=dict(sorted(ours.items(), key=by_use)),
                     unresolved=dict(sorted(unresolved.items(), key=by_use)),
                     declared_unused=sorted(d for d in idle if d not in absent and d not in run),
                     declared_run=sorted(run), declared_absent=sorted(absent),
                     providers={head: available[head] for head in third_party})


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
    ships would be a poor joke if it walked the tree with pathlib. What is not the project's
    (NOT_OURS: build output, vendored code) is left out before anything is read, and at the top
    of the tree before anything is walked (a checkout's .git and build/ were most of the walk). The reads pair
    each text with the file it came from (read_each names it), so a file removed between the walk
    and the read is only absent, and one that is not UTF-8 is read with its bad bytes replaced:
    neither takes the rest of the project with it.
    """

    top = path(str(root))
    walked = PathSet(top.glob("*.py"))
    for entry in top.iterdir():   # NOT_OURS at the top (.git, build, .venv) is never walked into
        if entry.name not in NOT_OURS and entry.is_dir():
            walked = walked + PathSet(entry.rglob("*.py"))
    ours = {str(p): "/".join(member.parts) for member, p in zip(walked.relative_to(str(root)), walked)   # the same members, in order
            if not (NOT_OURS & set(member.parts))}
    return [(ours[str(content.path)], str(memoryview(content), "utf-8", "replace"))
            for content in PathSet(list(ours)).read_each()]


def ships_in(root: Path) -> List[str]:
    """What this project offers an importer: its public top-level packages and their submodules.

    Walked with pygim's own path objects, like everything else here. A package is a directory with
    an `__init__.py` under `src/` or beside the project, and its submodules are the public `.py` files, compiled extensions and subpackages one level inside —
    one level, because that is the depth people import from. A project that ships nothing (an
    application, a folder of scripts) gets an empty list, which is the right answer, not an error.
    """

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


def from_import(module: str, names: Sequence[str], shipped: Collection[str]) -> List[str]:
    """The modules `from module import names` imports: each name that is a shipped submodule, and
    the module itself once if any name is not."""
    subs = [f"{module}.{name}" for name in names if f"{module}.{name}" in shipped]
    return subs if len(subs) == len(names) else [*subs, module]


def imports_in(files: Iterable[Tuple[str, str]], ships: Sequence[str]) -> Dict[str, Use]:
    """Every module imported by *files*, counted, and split between the project and its tests.

    A package importing its own parts is not a use of them by the project — it is the package being
    itself — so an import of `x.y` from inside the `x/` directory is not counted. Matching that
    directory has to be done on the path *within* the project: a repository whose own folder is
    named after the package it ships would otherwise swallow every import in the tree.

    Parsed by pygim's own parser (pygim.code), as Python 3.14 whatever Python runs it: on pygim's
    tree 20 ms, where ast's parse and walk took 357 and were most of every session's start. A
    relative import is the package itself, and a module that is not Python is passed over.

    `from lib import sub` imports the submodule `lib.sub` when lib ships one, as `import lib.sub`
    does, and is counted as its use; the package is counted once more only for a name that is no
    shipped submodule (`from lib import sub, helper`).
    """
    tops = {name.split(".")[0] for name in ships}
    shipped = set(ships)
    counted: Dict[str, Use] = {}
    for relative, text in files:
        try:
            tree = parse_source(text, relative)
        except SyntaxError:
            continue
        parts = relative.split("/")[:-1]
        inside = next((part for part in parts if part in tops), None)
        one = Use(shown=1) if is_shown(relative) else Use(work=1)
        named = [alias.field("name") for node in tree.find("Import") for alias in node.field("names")]
        for node in tree.find("ImportFrom"):
            if node.field("module") and not node.field("level"):
                named += from_import(node.field("module"), [alias.field("name") for alias in node.field("names")], shipped)
        for name in named:
            if name.split(".")[0] != inside:
                counted[name] = counted.get(name, Use()).plus(one)
    return counted


def name_of(dist) -> Optional[str]:
    """A distribution's name: the Name field of its METADATA (an egg's PKG-INFO), read off the
    headers, which end at the first blank line. importlib's `.metadata` runs the email parser over
    all of it, the description too, at five times the read: a third of a session's start (2026-10-30)."""
    text = dist.read_text("METADATA") or dist.read_text("PKG-INFO") or ""
    for line in text.partition("\n\n")[0].splitlines():
        if not line.strip():
            break
        if line[:5].lower() == "name:":
            return line[5:].strip() or None
    return None


def recorded(dist) -> List[str]:
    """The files a distribution recorded, by name: its RECORD read as text. `dist.files` stats every
    one to leave out the missing — 100,000 stats over this machine's environment, a fifth of a second
    a session start — and only the names are asked here. Without a RECORD (an egg-info install), as
    importlib reads it."""
    text = dist.read_text("RECORD")
    if text is None:
        return [str(f) for f in (dist.files or [])]
    return [row[0] for row in csv.reader(text.splitlines()) if row]


def folder_of(recorded_path: str) -> str:
    """The name of the folder a recorded file is in — "bin" for ../../../bin/ruff, "" at the top —
    read off the text: a pathlib object a row cost more than the rest of the reading."""
    parts = recorded_path.replace("\\", "/").rsplit("/", 2)
    return parts[-2] if len(parts) > 1 else ""


def modules_recorded(dist) -> List[str]:
    """The top-level modules a distribution recorded, for one without a top_level.txt: the folder each
    .py, .so or .pyd file is in at the top, or a top-level file's name up to its first dot, as it
    is imported (_yaml for _yaml.cpython-312-x86_64-linux-gnu.so). What is no name an import can
    spell — a dist-info, a .data folder, ../bin — is no module."""
    tops = set()
    for f in recorded(dist):
        f = f.replace("\\", "/")
        if f.endswith((".py", ".so", ".pyd")):
            first, inside, _ = f.partition("/")
            top = first if inside else first.partition(".")[0]
            if top.isidentifier() and top != "__pycache__":
                tops.add(top)
    return sorted(tops)


def installed(distributions) -> Dict[str, str]:
    """Top-level module name to the distribution that provides it, for what is importable here."""
    found: Dict[str, str] = {}
    for dist in distributions:
        name = name_of(dist)
        if not name:
            continue
        for top in (dist.read_text("top_level.txt") or "").split() or modules_recorded(dist):
            found.setdefault(top, name)
    return found


def installed_names(distributions) -> Set[str]:
    """Every distribution present, in `canonical` form."""
    return {canonical(name) for name in map(name_of, distributions) if name}


def commands_of(distributions) -> Set[str]:
    """The distributions that install a program — a console script, or any file put in a ``bin``
    or ``Scripts`` folder, as ruff's binary is — or plug into pytest, in `canonical` form. Such a
    dependency is run rather than imported, so its absence from the imports is by design."""
    found: Set[str] = set()
    for dist in distributions:
        name = name_of(dist)
        if not name:
            continue
        groups = {entry.group for entry in (dist.entry_points or [])}
        if groups & {"console_scripts", "gui_scripts", "pytest11"} or places_a_program(dist):
            found.add(canonical(name))
    return found


PROGRAM_FOLDERS = ("bin", "Scripts")


def places_a_program(dist) -> bool:
    """Whether a file the distribution recorded is in a bin or Scripts folder. A RECORD that names
    neither anywhere — most do not — is passed over without parsing a row of it."""
    text = dist.read_text("RECORD")
    if text is not None and not any(folder + sep in text for folder in PROGRAM_FOLDERS for sep in "/\\"):
        return False
    return any(folder_of(f) in PROGRAM_FOLDERS for f in recorded(dist))


def manifest_of(root: Path) -> dict:
    """The project's pyproject.toml as data, or {} when it has none or it is not TOML.

    Read by pygim's own TOML engine, not `tomllib`: that arrived in Python 3.11, and on 3.10
    importing it crashed the inventory and the project map every session starts with."""

    manifest = path(str(root)) / "pyproject.toml"
    if not manifest.is_file():
        return {}
    try:
        data = manifest.read()
    except (OSError, RuntimeError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def declared_in(root: Path) -> List[str]:
    """The distributions a manifest asks for, or nothing when the project has no manifest.

    Many projects have none — the dnd project on this machine imports pygim and declares nothing —
    so an empty list is ordinary, and must not be read as "this project declares no dependencies".
    """
    project = manifest_of(root).get("project", {})
    wanted = list(project.get("dependencies", []) or [])
    for extra in (project.get("optional-dependencies", {}) or {}).values():
        wanted.extend(extra)
    return sorted({re.split(r"[<>=!\[~; ]", line.strip())[0] for line in wanted if line.strip()})


class _ReadOnce:
    """A distribution each of whose files is read once for the questions from_machine asks of it
    (importlib reads again on every ask: RECORD was read for its modules and for its programs);
    the rest is the distribution's own."""

    def __init__(self, dist):
        self._dist = dist
        self._texts: Dict[str, Optional[str]] = {}

    def read_text(self, filename: str) -> Optional[str]:
        if filename not in self._texts:
            self._texts[filename] = self._dist.read_text(filename)
        return self._texts[filename]

    def __getattr__(self, name):
        return getattr(self._dist, name)


def from_machine(root: Path, distributions=None, stdlib: Optional[Collection[str]] = None) -> Inventory:
    """`survey` against a real directory and a real environment. Called where a command is wired."""
    files = python_files(root)
    ships = ships_in(root)
    dists = [_ReadOnce(d) for d in (metadata.distributions() if distributions is None else distributions)]
    return survey(imports=imports_in(files, ships), ships=ships, local=local_in(files),
                  available=installed(dists), declared=declared_in(root),
                  installed=installed_names(dists), commands=commands_of(dists),
                  stdlib=standard_library() if stdlib is None else stdlib,
                  root=root, files=len(files))


def standard_library() -> Collection[str]:
    """The top-level names the running Python's standard library provides, as it lists them
    (`sys.stdlib_module_names`). Part of what `from_machine` looks at, so called only from there."""
    return frozenset(sys.stdlib_module_names)


TEXT = (".py", ".pyi", ".h", ".hpp", ".cpp", ".c", ".md", ".rst", ".toml", ".yaml", ".yml", ".json",
        ".txt", ".cfg", ".ini", ".sh", ".cmake")


def git(args: Sequence[str], cwd: Path) -> Optional[str]:
    """``git <args>`` in *cwd*: its stripped output, or None when git is missing or it fails."""
    try:
        done = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def text_files(root: Path) -> List[Tuple[str, str]]:
    """Every text file the project keeps, as a path relative to *root* with its text.

    In a git checkout that is what `git ls-files` lists, so ignored build output, virtual
    environments and caches are left out by the project's own rules rather than by a guess; outside
    one, the walk falls back to everything not in `NOT_OURS`. Read by pygim's path table either way.

    Only regular files are read. A tracked file deleted from disk is still listed by git, and the walk
    lists folders; `read_all_files` skips both, its list no longer lined up with the members, and the
    whole result was dropped — so mid-work, with one file removed, nothing was read at all.
    """
    here = path(str(root))
    listed = git(["ls-files", "-z"], Path(root))
    named = (here / n for n in listed.split("\0") if n) if listed is not None else here.rglob("*")
    found = PathSet([p for p in named if p.name.endswith(TEXT)
                     and not (NOT_OURS & set(p.relative_to(here).parts)) and p.is_file()])   # skipped before any read
    texts = found.read_all_files()
    if len(found) != len(texts):                  # a file removed between the check and the read
        return []
    return [("/".join(member.parts), text if isinstance(text, str) else text.decode("utf-8", "replace"))
            for member, text in zip(found.relative_to(str(root)), texts)]      # the same members, in order


CONDA_ROOTS = ("miniconda3", "anaconda3", "miniforge3", "mambaforge", ".conda")
LOCAL_ENVS = (".venv", "venv", "env")
# Where an environment keeps what is installed in it: lib/python3.12/site-packages on POSIX,
# Lib\site-packages on Windows.
SITE_PACKAGES = ("lib/python*/site-packages", "Lib/site-packages")


@dataclass(frozen=True)
class Host:
    """An environment a project runs in, and how that is known."""

    prefix: Path
    kind: str                 # "conda env" or "venv"
    name: str
    python: str               # "3.12", or "" when it cannot be told
    how: str                  # what showed it: an editable install, a venv inside the project, environment.yml
    installs: Tuple[str, ...] = ()
    shared: Tuple[str, ...] = ()   # files two installs of the checkout both own — removing either removes them

    def tool(self, name: str) -> Optional[Path]:
        """A command installed in this environment, by path — a session's shell does not activate it.
        POSIX environments keep commands in bin/; Windows ones in Scripts\\, as .exe, with a conda
        environment's python.exe at its root."""

        for found in (self.prefix / "bin" / name, self.prefix / "Scripts" / f"{name}.exe",
                      self.prefix / "Scripts" / name, self.prefix / f"{name}.exe"):
            if path(str(found)).exists():
                return found
        return None


def _dist(folder: str) -> str:
    """`pygim-0.0.8.post1.dev69+dirty.dist-info` -> `pygim 0.0.8.post1.dev69+dirty`."""
    stem = folder[: -len(".dist-info")]
    name, _, version = stem.partition("-")
    return f"{name} {version}".strip()


def environments(root: Path, home: Path) -> List[Host]:
    """Where this project runs: every environment on the machine that holds it as an editable install,
    a virtual environment inside it, and a conda environment its environment.yml names.

    Finding the execution environment is part of discovery. A session's shell activates nothing, so
    `oo` and the project's own `python` are not on its PATH; on 2026-09-23 one of two sessions found
    the conda environment by itself and the other gave up on the inventory. An editable install says
    which checkout it came from (`direct_url.json`), which is what makes this work for any project
    developed with `pip install -e`, not only this one. *home* is the one the program was given."""
    here = path(str(root)).resolve()
    root = Path(str(here))
    candidates: List[Tuple[Path, str, str]] = []
    for base in CONDA_ROOTS:
        conda = path(str(home)) / base
        if (conda / "conda-meta").is_dir():
            candidates.append((Path(str(conda)), "conda env", "base"))
        if (conda / "envs").is_dir():
            candidates += [(Path(str(env)), "conda env", env.name) for env in (conda / "envs").iterdir()]
    candidates += [(root / name, "venv", name) for name in LOCAL_ENVS if (here / name / "pyvenv.cfg").is_file()]
    named = ""
    environment = here / "environment.yml"
    if environment.is_file():
        for line in environment.read_bytes().decode("utf-8", "replace").splitlines():
            if line.startswith("name:"):
                named = line.split(":", 1)[1].strip()

    hosts: List[Host] = []
    for prefix, kind, name in candidates:
        seen, installs, owned = set(), [], []
        records = [record for layout in SITE_PACKAGES
                   for record in path(str(prefix)).glob(layout + "/*.dist-info/direct_url.json")]
        for record in records:
            real = record.resolve()
            if str(real) in seen:                 # lib/python3.1 may be a link to lib/python3.12
                continue
            seen.add(str(real))
            try:
                data = real.read()                # pygim's JSON engine
            except (OSError, RuntimeError, ValueError):
                continue
            if not isinstance(data, dict):
                continue
            url = urlparse(data.get("url", ""))          # pip writes file:///C:/… on Windows
            if url.scheme == "file" and (data.get("dir_info") or {}).get("editable") \
                    and path(url2pathname(url.path)).resolve() == here:
                installs.append(_dist(real.parent.name))
                record = real.parent / "RECORD"
                owned.append({line.split(",", 1)[0] for line in record.read_bytes().decode("utf-8").splitlines()
                              if line and "dist-info" not in line.split(",", 1)[0]} if record.is_file() else set())
        inside = kind == "venv"
        if not (installs or inside or (named and name == named)):
            continue
        how = ("installed editable as " + " and ".join(sorted(installs)) if installs else
               "a virtual environment inside the project" if inside else "environment.yml names it")
        # Two installs of one checkout usually both own its console scripts. On 2026-09-23 the older one
        # was uninstalled as stale, took `bin/oo` and `bin/pygim` with it, and every hook and server
        # start failed until they were restored — the files both claim are what makes a duplicate matter.
        shared = sorted(set.intersection(*owned)) if len(owned) > 1 else []
        hosts.append(Host(prefix, kind, name, _python_version(prefix),
                          how, tuple(sorted(installs)), tuple(Path(s).name for s in shared)))
    return hosts


def _python_version(prefix: Path) -> str:
    """"3.12", read off what the environment keeps — its python link on POSIX, else its pyvenv.cfg,
    else conda-meta's record of the python package, which is all a Windows conda environment has;
    "" when none of them says."""

    env = path(str(prefix))
    linked = (env / "bin" / "python").resolve().name
    if linked.startswith("python3"):
        return linked[len("python"):]
    config = env / "pyvenv.cfg"
    if config.is_file():
        for line in config.read_bytes().decode("utf-8", "replace").splitlines():
            key, _, value = line.partition("=")
            if key.strip() in ("version", "version_info"):
                return ".".join(value.strip().split(".")[:2])
    if (env / "conda-meta").is_dir():
        for record in (env / "conda-meta").glob("python-3*.json"):
            return ".".join(record.name[len("python-"):].split(".")[:2])
    return ""


def _shown(target: Path, home: Path) -> str:
    """*target* as a person types it: under *home* as ~/…, and with forward slashes, which every shell
    a session runs commands in accepts, Windows' included."""

    try:
        return "~/" + ("/".join(path(str(target)).relative_to(str(home)).parts) or ".")
    except ValueError:
        return Path(target).as_posix()


LANGUAGES = {".py": "Python", ".pyi": "Python", ".h": "C++", ".hpp": "C++", ".cpp": "C++", ".cc": "C++",
             ".c": "C", ".rs": "Rust", ".go": "Go", ".js": "JavaScript", ".ts": "TypeScript",
             ".java": "Java", ".md": "Markdown", ".rst": "reStructuredText", ".ipynb": "notebooks"}
ROLES = {"src": "code", "lib": "code", "tests": "tests", "test": "tests", "docs": "docs", "doc": "docs",
         "examples": "examples", "benchmarks": "benchmarks", "scripts": "scripts", "tools": "tools",
         "data": "data", "py": "code"}
MANIFESTS = ("pyproject.toml", "setup.py", "setup.cfg", "requirements.txt", "CMakeLists.txt", "Makefile",
             "package.json", "Cargo.toml", "go.mod", "environment.yml", "tox.ini", "noxfile.py")


def project_map(root: Path, *, home: Optional[Path] = None, recent: int = 3) -> str:
    """What this project is, before any file in it is opened — the discovery every task starts with.

    Two models reviewed one file of pygim in depth on 2026-09-23 and neither looked at the project
    first; neither saw that the project ships the components the file should have used. A step that
    has to be remembered is a step that gets skipped, so this is computed at every session's start
    and delivered unasked: what the project says it is, how it is laid out, what it ships and leaves
    idle in its own work, what it reaches for, how it is run, and what changed last. Under 1.5 KB.
    """
    root = Path(root)
    here = path(str(root))
    listed = git(["ls-files"], root)
    names = [n for n in (listed or "").splitlines() if n]
    lines: List[str] = []

    title, about = root.name, ""
    for readme in ("README.md", "README.rst", "README.txt", "README"):
        if (here / readme).is_file():
            text = (here / readme).read_bytes().decode("utf-8", "replace")
            heads = [ln.strip("#= ").strip() for ln in text.splitlines() if ln.startswith("#")]
            heads = [h for h in heads if h and not h.startswith(("!", "<", "["))]
            title = heads[0] if heads else title
            prose = [ln.strip() for ln in text.splitlines()
                     if ln.strip() and not ln.startswith(("#", "=", "-", "[", "!", "<", "|", "`", ">"))]
            about = prose[0] if prose else ""
            break
    lines.append(f"{title}" + (f" — {about[:160]}" if about else ""))

    tops: Dict[str, int] = {}
    for name in names:
        head = name.split("/")[0]
        if "/" in name and not head.startswith(".") and head not in NOT_OURS:
            tops[head] = tops.get(head, 0) + 1
    if tops:
        lines.append("layout: " + " · ".join(
            f"{d}/ ({ROLES[d] + ', ' if d in ROLES else ''}{n} files)"
            for d, n in sorted(tops.items(), key=lambda kv: -kv[1])[:8]))
    manifests = [m for m in MANIFESTS if (here / m).is_file()]
    if manifests:
        lines.append("built by: " + ", ".join(manifests))

    counts: Dict[str, int] = {}
    for name in names:
        language = LANGUAGES.get(Path(name).suffix)
        if language:
            counts[language] = counts.get(language, 0) + 1
    if counts:
        lines.append("languages: " + " · ".join(f"{k} {v}" for k, v in sorted(counts.items(), key=lambda kv: -kv[1])[:5]))

    hosts = environments(root, home) if home is not None else []
    inventory = "`oo inventory`"
    for host in hosts:
        how = host.how
        if host.installs:
            names = [i.split(" ", 1)[0] for i in host.installs]
            how = "installed editable as " + " and ".join(names) + (
                f" ({len(names)} installs of one checkout" + (f", both owning {', '.join(host.shared)} — "
                "uninstalling either removes them; reinstall the other after" if host.shared else "") + ")"
                if len(names) > 1 else "")
        lines.append(f"runs in: {host.kind} `{host.name}`" + (f", Python {host.python}" if host.python else "")
                     + f", {how}; not active in a session's shell — use "
                     + _shown(host.tool("python") or host.prefix / "bin" / "python", home))
        if host.tool("oo"):
            inventory = f"`{_shown(host.tool('oo'), home)} inventory`"
    if home is not None and not hosts:
        lines.append("runs in: not found — no environment on this machine installs it editable, and it has no "
                     "virtual environment or environment.yml of its own; find how it runs before running it")
    try:
        found = from_machine(root)
    except Exception:                                    # a map is never worth failing a session start for
        found = None
    if found is not None:
        if found.ships:
            idle = found.unused + found.shown_only
            lines.append(f"ships: {', '.join(sorted({n.split('.')[0] for n in found.ships}))} "
                         f"({len(found.ships)} modules)"
                         + (f"; not used by its own work: {', '.join(n.split('.', 1)[-1] for n in idle)}"
                            f" — {inventory} for the join" if idle else ""))
        else:
            lines.append("ships: nothing importable — an application")
        elsewhere = list(found.third_party)[:8]
        if elsewhere:
            lines.append("reaches for: " + ", ".join(elsewhere))

    scripts = manifest_of(root).get("project", {}).get("scripts", {})
    if scripts:
        lines.append("run as: " + ", ".join(sorted(scripts)))

    log = git(["log", f"-{recent}", "--format=%h %s"], root)
    if log:
        lines.append("recent: " + " | ".join(line[:80] for line in log.splitlines()))
    return "\n".join(lines)
