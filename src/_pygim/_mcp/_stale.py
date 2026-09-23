# -*- coding: utf-8 -*-
"""Whether what a memory names still exists.

A card is followed literally. `oo memory reload` stayed in a standing card for a day after the
command was renamed, and running it printed a sentence and exited 0 — an agent following the rule
saw success and went on trusting a server that never restarted (global memory #21). Nothing checked,
because a memory is prose and prose does not fail.

So this reads what a memory puts in code font and asks whether each thing still exists: a command
and its options, a path, a name in the project's code, a link to another memory. The same
reference is judged by where it sits. In the **card** — when, not, do, why, the steps — it is
followed, so a dead one is stale. In the **body** it may be history on purpose (#21 names the dead
command to explain the rule), so it is only worth a look. A memory written before the card format
has no card, is delivered from its text, and is judged as a card would be.

Nothing here knows any particular project. What exists is asked of resolvers the caller builds.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Callable, Dict, Iterable, List, Mapping, Optional, Sequence, Set, Tuple

from . import _cards


class Doubt(str):
    """A problem the resolver is not sure of: a bare word, an illustrative path. It is listed to
    look at, never counted as stale — a check that cries wolf is read once and then never again."""


_CODE = re.compile(r"`([^`\n]+)`")
_LINK = re.compile(r"\[\[([a-z0-9-]+)\]\]")
_GLOBAL_REF = re.compile(r"\bglobal(?: memory)? #(\d+)\b")
_PATH = re.compile(r"^(?:~/|/|\.{0,2}/?)?[\w.@-]+(?:/[\w.@-]+)+/?(?::L?\d+(?:-\d+)?)?$")
_FILE = re.compile(r"^[\w.-]+\.(?:py|pyi|h|hpp|cpp|md|yaml|yml|toml|json|txt|cfg|ini|sh)(?::L?\d+(?:-\d+)?)?$")
_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*(?:\(\))?$")
_SKIP = re.compile(r"[<>{}*|$]|://|\s")          # placeholders, globs, pipes, URLs, shell


@dataclass(frozen=True)
class Finding:
    """One thing a memory names that does not hold any more."""

    memory: str
    title: str
    part: str          # "card" — followed literally; "body" — perhaps history on purpose
    what: str          # command, option, path, name, link, reference
    named: str
    problem: str

    certain: bool = True

    @property
    def stale(self) -> bool:
        """Followed literally and certainly gone. Anything less is worth a look, not a rewrite."""
        return self.part == "card" and self.certain


@dataclass
class Resolvers:
    """What exists, asked of the caller. Each answers None when the thing is fine, or a sentence
    saying what is wrong. A resolver left as None is not checked — a store with no project has no
    paths or code names to check against, and says nothing about them rather than guessing."""

    command: Optional[Callable[[List[str]], Optional[str]]] = None
    path: Optional[Callable[[str], Optional[str]]] = None
    name: Optional[Callable[[str], Optional[str]]] = None
    link: Optional[Callable[[str], Optional[str]]] = None
    reference: Optional[Callable[[int], Optional[str]]] = None


def spans(text: str) -> List[str]:
    return [m.group(1).strip() for m in _CODE.finditer(text or "")]


def classify(span: str) -> Optional[Tuple[str, str]]:
    """What sort of reference a code span is, or None when it is not one this can check."""
    if span.startswith("oo ") or span == "oo":
        return "command", span
    if _SKIP.search(span) or span.startswith("-"):
        return None
    if _PATH.match(span) or _FILE.match(span):
        return "path", span
    bare = span[:-2] if span.endswith("()") else span
    if _NAME.match(span) and len(bare.replace(".", "")) >= 4 and not bare.isdigit():
        return "name", bare
    return None


def _parts(memory: Mapping[str, object]) -> List[Tuple[str, str]]:
    card = _cards.parse(str(memory.get("text") or ""))
    if card.legacy:
        return [("card", card.body)]
    head = "\n".join([card.when, card.not_, card.do, card.why, *card.steps])
    return [("card", str(memory.get("title") or "") + "\n" + head), ("body", card.body)]


def check(memory: Mapping[str, object], resolve: Resolvers) -> List[Finding]:
    """Every reference in *memory* that no longer holds, each judged by the part it sits in."""
    ref, title = str(memory.get("memory", "")), str(memory.get("title", ""))
    found: List[Finding] = []
    seen: Set[Tuple[str, str]] = set()

    def note(part: str, what: str, named: str, problem: Optional[str]) -> None:
        if problem and (part, named) not in seen:
            seen.add((part, named))
            found.append(Finding(ref, title, part, what, named, str(problem), not isinstance(problem, Doubt)))

    for part, text in _parts(memory):
        for span in spans(text):
            sort = classify(span)
            if sort is None:
                continue
            what, value = sort
            resolver = getattr(resolve, what)
            if resolver is not None:
                note(part, what, value, resolver(value.split() if what == "command" else value))
        if resolve.link is not None:
            for slug in _LINK.findall(text):
                note(part, "link", f"[[{slug}]]", resolve.link(slug))
        if resolve.reference is not None:
            for number in _GLOBAL_REF.findall(text):
                note(part, "reference", f"global #{number}", resolve.reference(int(number)))
    return found


# ── resolvers built from what a machine holds ────────────────────────────────


def command_resolver(root_command) -> Callable[[List[str]], Optional[str]]:
    """Walk *words* down a click command tree: every group word must name a command, and every
    `--option` given to the command reached must be one it takes. Arguments are not judged."""
    import click
    import difflib

    def resolve(words: List[str]) -> Optional[str]:
        command, path = root_command, [words[0]]
        rest = words[1:]
        while isinstance(command, click.Group) and rest and not rest[0].startswith("-"):
            name = rest.pop(0)
            sub = command.commands.get(name)
            if sub is None:
                near = difflib.get_close_matches(name, list(command.commands), n=1, cutoff=0.6)
                return (f"no such command: `{' '.join(path + [name])}`"
                        + (f" — did you mean `{' '.join(path + [near[0]])}`?" if near else
                           f"; `{' '.join(path)}` has {', '.join(sorted(command.commands))}"))
            command, path = sub, path + [name]
        if isinstance(command, click.Group):
            return None                                  # a group named on its own is still a group
        options = {o for p in command.params for o in getattr(p, "opts", []) + getattr(p, "secondary_opts", [])}
        for word in rest:
            flag = word.split("=", 1)[0]
            if flag.startswith("--") and flag not in options:
                return f"`{' '.join(path)}` takes no `{flag}` — it takes {', '.join(sorted(o for o in options if o.startswith('--')))}"
        return None
    return resolve


def path_resolver(project, home, files: Sequence[str] = ()) -> Callable[[str], Optional[str]]:
    """A path relative to the project, or absolute, or under `~` — which means *home*, the one the
    program was given, never the process's own.

    Certain only when it starts at something the project has at its top (`src/…`, `tests/…`); a
    partial path that ends a real one (`_mcp/enact.py`) is fine; anything else — `pkg/c.py` in an
    explanation, `add/add` the git conflict — is only worth a look."""
    from pathlib import Path

    from pygim.pathlike import path

    top = {entry.name for entry in path(str(project)).iterdir()} if project and Path(project).is_dir() else set()
    known = [f.rstrip("/") for f in files]
    dirs = {"/".join(f.split("/")[:n]) for f in known for n in range(1, f.count("/") + 1)}

    def resolve(named: str) -> Optional[str]:
        where, _, line = named.partition(":")
        where = where.rstrip("/")
        if where.startswith("~/"):
            target, certain = Path(home) / where[2:], True
        elif where.startswith("/"):
            target, certain = Path(where), True
        else:
            bare = where[2:] if where.startswith("./") else where
            target = Path(project) / bare
            certain = bare.split("/")[0] in top
            if not target.exists():
                if any(k == bare or k.endswith("/" + bare) for k in known) or \
                        any(d == bare or d.endswith("/" + bare) for d in dirs):
                    return None
                return "does not exist" if certain else Doubt("found nowhere in the project — an example, or moved?")
        if not target.exists():
            return "does not exist" if certain else Doubt("does not exist")
        if line and target.is_file():
            number = int(line.lstrip("L").split("-")[0])
            count = target.read_text(encoding="utf-8", errors="replace").count("\n") + 1
            if number > count:
                return f"points at line {number}; the file has {count}"
        return None
    return resolve


def name_resolver(words: Set[str], ours: Set[str] = frozenset(),
                  elsewhere: Set[str] = frozenset()) -> Callable[[str], Optional[str]]:
    """A name in code font that appears nowhere in the project's own files has been renamed or
    removed — certainly, when it is dotted under one of the project's own packages (*ours*); not at
    all, when it belongs to another library (*elsewhere*: the standard library, what is installed);
    and only perhaps for a bare word, which may be a tool, a flag or an example."""
    def resolve(named: str) -> Optional[str]:
        parts = named.split(".")
        if parts[0] in elsewhere and parts[0] not in ours:
            return None
        missing = [part for part in parts if part not in words]
        if not missing:
            return None
        problem = f"`{missing[0]}` appears nowhere in the project's files"
        return problem if (len(parts) > 1 and parts[0] in ours) else Doubt(problem)
    return resolve


def link_resolver(heads: Mapping[str, str], superseded: Mapping[str, str]) -> Callable[[str], Optional[str]]:
    """*heads*: slug to memory number for every current memory; *superseded*: slug of a replaced
    memory to the slug of the head that replaced it."""
    def resolve(slug: str) -> Optional[str]:
        if slug in heads:
            return None
        if slug in superseded:
            return f"points at a superseded memory; link [[{superseded[slug]}]] instead"
        return "no memory has this slug"
    return resolve


def reference_resolver(global_heads: Mapping[int, str], replaced_by: Mapping[int, int]) -> Callable[[int], Optional[str]]:
    def resolve(number: int) -> Optional[str]:
        if number in global_heads:
            return None
        if number in replaced_by:
            return f"global #{number} was superseded by global #{replaced_by[number]}"
        return f"global #{number} is not a memory in the global store"
    return resolve


def words_in(texts: Iterable[str]) -> Set[str]:
    words: Set[str] = set()
    for text in texts:
        words.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text))
    return words


def cited_in_code(files: Iterable[Tuple[str, str]], resolve: Callable[[int], Optional[str]]) -> List[Tuple[str, str]]:
    """Code that cites a global memory by number in a docstring or a design document, and now points
    at one that was superseded. A number names one version of a memory, so rewriting the memory
    leaves every citation of it behind; a reader who follows one gets the old version. Found on the
    first run: `test_layering.py` citing the dogfood rule by the number it had before its rewrite."""
    out = []
    for relative, text in files:
        for n, line in enumerate(text.splitlines(), 1):
            for number in _GLOBAL_REF.findall(line):
                problem = resolve(int(number))
                if problem:
                    out.append((f"{relative}:{n}", problem))
    return out
