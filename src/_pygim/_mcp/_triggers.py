# -*- coding: utf-8 -*-
"""Which problem space a path is in — the first, hand-written context policy.

A rule reaches an agent at session start and is inert two hundred calls later, when the decision it
governs is actually made. Standing knowledge answers "was it delivered"; this answers "was it
delivered *near*". The moment a file is about to be written is a moment that can be classified, and
the classification is a path.

The map lives in the store, at ``triggers.yaml`` beside ``policy.yaml`` — both say how this store
behaves, as opposed to what it knows. Not under ``taxonomy/``: the vocabulary loader globs that
directory and reads every file in it as a pack, and said so when this was first put there.

    # pattern: [tags]
    "tests/**": [artifact=test, task=implement]
    "src/_pygim_fast/**": [language=cpp, task=implement]

Patterns are matched with fnmatch against the path as given and against its project-relative form;
every pattern that matches contributes its tags, and the union is the space. Unknown tags are
dropped rather than refused: a trigger file is allowed to outlive a vocabulary value.

This is deliberately the dumbest thing that could work, and it is the seam the end state needs
(design 00 §4.13). A learned controller replaces the file, not the shape: something decides which
space a moment is in, and the index does the selecting.
"""
from __future__ import annotations

import re
from fnmatch import fnmatch
from pathlib import Path
from typing import Dict, List, Optional

TRIGGERS = "triggers.yaml"
_LINE = re.compile(r'^\s*["\']?([^"\':#]+?)["\']?\s*:\s*\[(.*?)\]\s*(?:#.*)?$')


def load(store: Optional[Path]) -> Dict[str, List[str]]:
    """The store's trigger map, or empty when it has none. Order is the file's."""
    if store is None:
        return {}
    file = Path(store) / TRIGGERS
    try:
        text = file.read_text(encoding="utf-8")
    except OSError:
        return {}
    out: Dict[str, List[str]] = {}
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        found = _LINE.match(line)
        if found:
            tags = [t.strip() for t in found.group(2).split(",") if t.strip()]
            if tags:
                out[found.group(1).strip()] = tags
    return out


def tags_for(path: str, triggers: Dict[str, List[str]], project: Optional[Path] = None) -> List[str]:
    """The tags every matching pattern contributes, in first-seen order, without duplicates."""
    candidates = [str(path).replace("\\", "/")]
    if project is not None:
        try:
            candidates.append(Path(path).resolve().relative_to(Path(project).resolve()).as_posix())
        except (ValueError, OSError):
            pass
    out: List[str] = []
    for pattern, tags in triggers.items():
        if any(fnmatch(c, pattern) for c in candidates):
            for tag in tags:
                if tag not in out:
                    out.append(tag)
    return out


def split_term(tags):
    """A trigger may carry `term=<word>` beside its tags. Tags say where in the taxonomy a moment
    sits; a term says what it is *about*, which is the only handle on a store whose vocabulary is
    deliberately unspecific — the global one answers every project, so it names almost nothing that
    a path could match. Returns (tags, term)."""
    kept, term = [], ""
    for tag in tags:
        if tag.startswith("term="):
            term = tag.split("=", 1)[1].strip()
        else:
            kept.append(tag)
    return kept, term


def one_line(text: str) -> str:
    """A memory's own summary, if it wrote one. Memories here follow a house style whose second
    element is a line beginning `One line:` — so the compact form a moment can afford is already
    written, by the person who knew what mattered."""
    for line in text.splitlines():
        stripped = line.strip().lstrip("*_ ")
        if stripped.lower().startswith("one line:"):
            return stripped.split(":", 1)[1].strip().strip("*_ ")
    return ""
