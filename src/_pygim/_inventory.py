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
            add(third_party, head, use)                  # the name the code says, not the distribution's
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


CONDA_ROOTS = ("miniconda3", "anaconda3", "miniforge3", "mambaforge", ".conda")
LOCAL_ENVS = (".venv", "venv", "env")


@dataclass(frozen=True)
class Host:
    """An environment a project runs in, and how that is known."""

    prefix: Path
    kind: str                 # "conda env" or "venv"
    name: str
    python: str               # "3.12", or "" when it cannot be told
    how: str                  # what showed it: an editable install, a venv inside the project, environment.yml
    installs: Tuple[str, ...] = ()

    def tool(self, name: str) -> Optional[Path]:
        """A command installed in this environment, by path — a session's shell does not activate it."""
        found = self.prefix / "bin" / name
        return found if found.exists() else None


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
    import json
    import os
    from urllib.parse import unquote, urlparse
    from pygim.pathlike import path

    root = Path(root).resolve()
    candidates: List[Tuple[Path, str, str]] = []
    for base in CONDA_ROOTS:
        conda = Path(home) / base
        if (conda / "conda-meta").is_dir():
            candidates.append((conda, "conda env", "base"))
        if (conda / "envs").is_dir():
            candidates += [(Path(str(env)), "conda env", env.name) for env in path(str(conda / "envs")).iterdir()]
    candidates += [(root / name, "venv", name) for name in LOCAL_ENVS if (root / name / "pyvenv.cfg").is_file()]
    named = ""
    if (root / "environment.yml").is_file():
        for line in (root / "environment.yml").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("name:"):
                named = line.split(":", 1)[1].strip()

    hosts: List[Host] = []
    for prefix, kind, name in candidates:
        seen, installs = set(), []
        for record in path(str(prefix)).glob("lib/python*/site-packages/*.dist-info/direct_url.json"):
            real = os.path.realpath(str(record))
            if real in seen:                      # lib/python3.1 may be a link to lib/python3.12
                continue
            seen.add(real)
            try:
                data = json.loads(Path(real).read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            url = urlparse(data.get("url", ""))
            if url.scheme == "file" and (data.get("dir_info") or {}).get("editable") \
                    and Path(unquote(url.path)).resolve() == root:
                installs.append(_dist(Path(real).parent.name))
        inside = kind == "venv"
        if not (installs or inside or (named and name == named)):
            continue
        python = os.path.basename(os.path.realpath(str(prefix / "bin" / "python")))
        how = ("installed editable as " + " and ".join(sorted(installs)) if installs else
               "a virtual environment inside the project" if inside else "environment.yml names it")
        hosts.append(Host(prefix, kind, name, python.removeprefix("python") if python.startswith("python3") else "",
                          how, tuple(sorted(installs))))
    return hosts


def _where(host: Host, home: Path) -> str:
    text = str(host.prefix)
    return "~" + text[len(str(home)):] if text.startswith(str(home)) else text


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
    from _pygim._mcp._stores import git

    root = Path(root)
    listed = git(["ls-files"], root)
    names = [n for n in (listed or "").splitlines() if n]
    lines: List[str] = []

    title, about = root.name, ""
    for readme in ("README.md", "README.rst", "README.txt", "README"):
        if (root / readme).is_file():
            text = (root / readme).read_text(encoding="utf-8", errors="replace")
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
    manifests = [m for m in MANIFESTS if (root / m).is_file()]
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
                f" ({len(names)} installs of one checkout)" if len(names) > 1 else "")
        lines.append(f"runs in: {host.kind} `{host.name}`" + (f", Python {host.python}" if host.python else "")
                     + f", {how}; not active in a session's shell — use {_where(host, home)}/bin/python")
        if host.tool("oo"):
            inventory = f"`{_where(host, home)}/bin/oo inventory`"
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

    manifest = root / "pyproject.toml"
    if manifest.is_file():
        import tomllib

        try:
            scripts = tomllib.loads(manifest.read_text(encoding="utf-8")).get("project", {}).get("scripts", {})
        except ValueError:
            scripts = {}
        if scripts:
            lines.append("run as: " + ", ".join(sorted(scripts)))

    log = git(["log", f"-{recent}", "--format=%h %s"], root)
    if log:
        lines.append("recent: " + " | ".join(line[:80] for line in log.splitlines()))
    return "\n".join(lines)
