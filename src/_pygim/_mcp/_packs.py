"""Drafted vocabulary packs and the citations they rest on.

A pack drafted by the ``prepare-vocabulary`` prompt is a proposal until a person accepts it
(overview §4.6). ``check`` loads a proposal beside the store's current vocabulary in a scratch
store, so every error comes back by file and line without touching the real one; ``accept`` is
the person's step that makes it live. ``cite`` turns a line of a project document into a locator
— the passage digest a value carries, and the document's version for the inventory — under the id
the store's inventory already gives that document, so a pack and a memory name it the same way.

Citation paths are relative to the project's root, not to the store: a store in a user directory
or on the ``memory`` branch lives outside the checkout, and every worktree must resolve the same
document from the same path.
"""
from __future__ import annotations

import os
import re
import shutil  # the scratch stores `check` builds, never the live one
import tempfile
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

def _yaml(file: Path) -> Any:
    """A YAML file as pygim's own engine reads it (rapidyaml, through `pygim.pathlike`).

    The inventory and a pack's name were read with regular expressions until 2026-09-23, when review
    sessions showed the hand reader getting four of five valid forms wrong — a trailing comment kept
    in the path, a flow mapping dropped, a nested `path:` taking over the document's own — while the
    library this module ships reads YAML correctly. An empty file, or one holding only comments, is
    None."""
    from pygim.pathlike import path

    try:
        return path(str(file)).read()
    except Exception as exc:                          # the engine names the line; say which file it was
        raise ValueError(f"{file}: not valid YAML — {exc}") from None


def _yaml_text(data: Any) -> str:
    """*data* as the engine writes it, quoted wherever YAML needs quoting."""
    from pygim.pathlike import path

    with tempfile.TemporaryDirectory(prefix="pygim-yaml-") as tmp:
        out = Path(tmp) / "out.yaml"
        path(str(out)).write(data)
        return out.read_text(encoding="utf-8")


def pack_name(proposal: Path) -> str:
    data = _yaml(proposal)
    name = data.get("pack") if isinstance(data, dict) else None
    if not isinstance(name, str) or not name.strip():
        raise ValueError("a pack starts with `pack: <domain>` — the domain's name is the pack's name")
    return name.strip()


def _scratch(tmp: str, name: str, store: Path, skip: str, add: Optional[str] = None) -> Any:
    """A scratch store holding the store's vocabulary files, less *skip*, plus *add* as *skip*."""
    from pygim.enact import Enact

    scratch = Path(tmp) / name
    Enact.init(str(scratch))
    for existing in sorted((store / "taxonomy").glob("*.yaml")):
        if existing.name != skip:
            shutil.copyfile(existing, scratch / "taxonomy" / existing.name)
    if add is not None:
        (scratch / "taxonomy" / skip).write_text(add, encoding="utf-8")
    return Enact(str(scratch))


def _tags(vocabulary: Dict[str, Any]) -> List[str]:
    return [v["tag"] for d in vocabulary["dimensions"] for v in d["values"] if not v.get("any")]


def check(store: Path, proposal: Path, *, project: Optional[Path] = None, memory: Any = None) -> Dict[str, Any]:
    """Loads *proposal* as ``taxonomy/pack-<name>.yaml`` beside the store's vocabulary, in a scratch
    copy. ``ok`` with what the pack adds, or ``ok: False`` with the loader's messages, which name
    the proposal's own path and line. What it adds is every tag the vocabulary gains, wherever it
    lands: a pack that only extends a dimension it does not own, or only brings its own domain,
    once reported `values: 0` — which reads as a pack that adds nothing.

    Two things the loader cannot see come back beside a pack that loads. ``warnings``: each locator
    the pack adds, checked against its document in *project* — not inventoried, missing, a passage
    that is not the cited one, or text that also occurs elsewhere, so a locator found by matching
    text may point at the wrong occurrence. ``removed``: each value live now that the pack would
    remove, with the heads of *memory* (the live store) still carrying it."""
    from pygim.enact import VocabularyError

    text = proposal.read_text(encoding="utf-8")
    try:
        name = pack_name(proposal)
    except ValueError as exc:
        return {"ok": False, "errors": f"{proposal}:1: {exc}"}
    target = f"pack-{name}.yaml"
    with tempfile.TemporaryDirectory(prefix="pygim-pack-check-") as tmp:
        try:
            drafted = _scratch(tmp, "store", store, target, text)
        except VocabularyError as exc:
            scratch = Path(tmp) / "store"
            message = str(exc).replace(str(scratch / "taxonomy" / target), str(proposal)).replace(f"taxonomy/{target}", str(proposal))
            return {"ok": False, "errors": message}
        vocabulary = drafted.vocabulary()
        without = _scratch(tmp, "without", store, target)
        before = {(x["tag"], x["doc"], x["line"], x["lines"], x["passage"]) for x in without.sources()}
        added = [x for x in drafted.sources() if (x["tag"], x["doc"], x["line"], x["lines"], x["passage"]) not in before]
        live = memory.vocabulary() if memory is not None else _scratch(tmp, "live", store, "").vocabulary()
        del drafted, without
    dims = [d for d in vocabulary["dimensions"] if d.get("pack") == name]
    remaining = set(_tags(vocabulary))
    removed = []
    for tag in _tags(live):
        if tag in remaining:
            continue
        carriers = [f"{h['memory']} {h['title']}" for h in memory.heads([tag])] if memory is not None else []
        removed.append({"tag": tag, "carried_by": carriers})
    gained = sorted(remaining - set(_tags(live)))
    return {"ok": True, "pack": name, "dimensions": [d["name"] for d in dims],
            "values": len(gained), "adds": gained,
            "replaces": (store / "taxonomy" / target).exists(),
            "removed": removed,
            "warnings": _locator_warnings(added, store, proposal.parent / "inventory.yaml", project)}


def accept(store: Path, proposal: Path, replace: bool = False, *, project: Optional[Path] = None) -> Dict[str, Any]:
    """A person's step: checks the proposal, copies it to ``taxonomy/pack-<name>.yaml``, and adds the
    documents of an ``inventory.yaml`` beside it to ``sources/inventory.yaml``. A pack of that name
    already live is replaced only when asked, and never while a memory still carries a value the
    replacement removes: those memories would lose the tag without a word, and on a hard dimension
    no read could find them. Retag them first."""
    from pygim.enact import Enact

    memory = Enact(str(store))
    result = check(store, proposal, project=project, memory=memory)
    if not result["ok"]:
        return result
    if result["replaces"] and not replace:
        return {"ok": False, "errors": f"taxonomy/pack-{result['pack']}.yaml is already live — pass --replace to replace it"}
    carried = [r for r in result["removed"] if r["carried_by"]]
    if carried:
        lines = [f"  {r['tag']}: {', '.join(r['carried_by'])}" for r in carried]
        return {"ok": False, "errors": "the pack removes values that memories still carry — unlink or retag them first:\n" + "\n".join(lines)}
    target = f"taxonomy/pack-{result['pack']}.yaml"
    if not memory.write_file(target, proposal.read_text(encoding="utf-8"), _digest_of(store / target)):
        return {"ok": False, "errors": f"{target} changed while it was being accepted — run the accept again"}
    result["inventory"], result["inventory_kept"] = _merge_inventory(store, proposal.parent / "inventory.yaml", memory)
    return result


ENTRY_FIELDS = ("brief", "full", "when", "when_not", "example")


def accept_concept(store: Path, concept: Dict[str, Any], *, project: Optional[Path] = None) -> Dict[str, Any]:
    """A person's step for one concept the vocabulary lacks (overview §4.6, "During work"): the value,
    with the entry the proposal described, is written into the store's pack that holds its dimension,
    and that pack is accepted again — the same checks as any pack (`accept`). The memories that asked
    for it gain the tag, with source `proposed`, when the store next opens. The pack is edited as
    text, so the person's comments in it stay.

    A proposal for a whole dimension is refused: a dimension comes with its values and roles, and
    that is a drafted pack (`accept`), not one line."""
    name, dimension = concept["concept"], concept.get("dimension") or ""
    if not dimension:
        return {"ok": False, "errors": f"`{name}` is a new dimension, not a value — a dimension comes with its roles "
                                       "and values in a drafted pack: `oo enact accept --pack FILE`"}
    entry = {k: concept["entry"][k] for k in ENTRY_FIELDS if concept["entry"].get(k)}
    import pygim
    from pygim.pathlike import PathStore

    taxonomy = pygim.path(str(store / "taxonomy"), store=PathStore())
    packs = sorted((Path(os.fspath(p)) for p in taxonomy.pathset("pack-*.yaml")), key=lambda p: p.name)
    text = None
    for file in packs:                                    # the pack that already holds the dimension
        data = _yaml(file) or {}
        if dimension in (data.get("dimensions") or {}):
            text = _insert_under(file.read_text(encoding="utf-8"), ["dimensions", dimension, "values"], {name: {"entry": entry}})
        elif dimension in (data.get("extends") or {}):
            text = _insert_under(file.read_text(encoding="utf-8"), ["extends", dimension], {name: {"entry": entry}})
        if text is not None:
            break
    if text is None and len(packs) == 1:                  # nobody extends it yet: the store's one pack does
        file = packs[0]
        body = file.read_text(encoding="utf-8")
        text = _insert_under(body, ["extends"], {dimension: {name: {"entry": entry}}})
        if text is None:
            text = body.rstrip("\n") + "\n\n" + _yaml_text({"extends": {dimension: {name: {"entry": entry}}}})
    if text is None:
        names = ", ".join(p.name for p in packs) or "none"
        return {"ok": False, "errors": f"no pack in {store / 'taxonomy'} holds `{dimension}` to add `{name}` to "
                                       f"(packs: {names}) — add it to one by hand and `oo enact accept --pack` it"}
    with tempfile.TemporaryDirectory(prefix="pygim-concept-") as tmp:
        draft = Path(tmp) / file.name
        draft.write_text(text, encoding="utf-8")
        result = accept(store, draft, replace=True, project=project)
    result["into"] = file.name
    return result


def _insert_under(text: str, keys: List[str], item: Dict[str, Any]) -> Optional[str]:
    """*text* with *item* written as YAML at the end of the block under the key path *keys*, indented
    as that block's children are; None when the path is not there. Blank lines and comments are
    left where they are, so the file reads as its author left it."""
    lines = text.split("\n")
    stack: List[Tuple[int, str]] = []
    found = None
    for i, line in enumerate(lines):
        bare = line.strip()
        if not bare or bare.startswith("#"):
            continue
        indent = len(line) - len(line.lstrip(" "))
        while stack and stack[-1][0] >= indent:
            stack.pop()
        key = re.match(r"([A-Za-z0-9_.-]+):(\s|$)", bare)
        if key:
            stack.append((indent, key.group(1)))
            if [k for _, k in stack] == keys:
                found = (i, indent)
                break
    if found is None:
        return None
    at, indent = found
    end, child = len(lines), None
    for j in range(at + 1, len(lines)):
        bare = lines[j].strip()
        if not bare or bare.startswith("#"):
            continue
        deeper = len(lines[j]) - len(lines[j].lstrip(" "))
        if deeper <= indent:
            end = j
            break
        child = deeper if child is None else child
    while end > at + 1 and not lines[end - 1].strip():   # join the block, not the gap after it
        end -= 1
    pad = " " * (child if child is not None else indent + 2)
    block = [pad + line if line else line for line in _yaml_text(item).rstrip("\n").split("\n")]
    return "\n".join(lines[:end] + block + lines[end:])


def _digest_of(file: Path) -> str:
    """What `write_file` compares against: the digest of *file* as it is now, or empty when it is not."""
    from pygim.enact import digest

    return digest(file.read_bytes()) if file.is_file() else ""


def entries(path: Path) -> Dict[str, Dict[str, Any]]:
    """The documents of an inventory file, each id with its whole entry (kind, path, version)."""
    if not path.is_file():
        return {}
    data = _yaml(path)
    if data is None:
        return {}
    if not isinstance(data, dict):
        raise ValueError(f"{path}: an inventory is a mapping of document id to its entry")
    return {str(doc): (entry if isinstance(entry, dict) else {}) for doc, entry in data.items()}


def inventory(path: Path) -> Dict[str, str]:
    """The documents of an inventory file: id to path, as written."""
    return {doc: str(entry.get("path") or "") for doc, entry in entries(path).items()}


def _lines(file: Path) -> List[str]:
    """A document's lines. \\r\\n is a line ending, never part of a line (corpus.h), and the newline
    that ends the last line does not begin another: split on "\\n", a file of three lines ending in a
    newline gave four, and the fourth — empty — was accepted as a line. Every such line digests to the
    digest of nothing, so a locator to it verified against any document on the machine."""
    text = file.read_bytes().replace(b"\r\n", b"\n").decode("utf-8")
    if not text:
        return []
    lines = text.split("\n")
    return lines[:-1] if text.endswith("\n") else lines


BLANK = ("is blank — a blank passage digests the same in every document, so it would verify against any "
         "of them; cite the lines that say it")


_UNREAD = object()


class Sources:
    """A store's sources: the documents its vocabulary and memories cite, where each one lives, and the
    passages cited from them.

    One store and the project it serves, held together. Seven functions took them apart and passed
    them to each other — `(project, store, inventory file)` threaded through every one, four of them
    reaching in to turn a document id into a file (global #14; found by a review session, 2026-09-23).
    The served checkout is worked out once and kept: it was read from the store's policy file on every
    lookup, which is configuration read in the middle of a library (global #9)."""

    def __init__(self, store: Optional[Path], project: Optional[Path] = None) -> None:
        self.store = store
        self.project = project
        self._served: Any = _UNREAD

    @property
    def listed(self) -> Optional[Path]:
        """The store's inventory file, or None for no store."""
        return self.store / "sources" / "inventory.yaml" if self.store is not None else None

    def documents(self) -> Dict[str, str]:
        """Each inventoried document id with its path, as the store keeps it."""
        return inventory(self.listed) if self.listed is not None else {}

    def served(self) -> Optional[Path]:
        """The checkout the store says it serves, read from its policy once and kept."""
        if self._served is _UNREAD:
            from . import _stores

            self._served = _stores.project_of(self.store) if self.store is not None else None
        return self._served

    def bases(self, inventory_file: Path) -> List[Path]:
        """Where an inventoried path may be relative to: the project's root (02 §5.2); the checkout the
        store says it serves, for a store kept outside it; the store's own root, for a body of knowledge
        with its own sources and no checkout; and the inventory file, for stores written before the
        first rule."""
        seen: List[Path] = []
        for base in (self.project, self.served(), self.store, inventory_file.parent):
            if base is not None and base not in seen:
                seen.append(base)
        return seen

    def resolve(self, inventory_file: Path, path: str) -> Optional[Path]:
        for base in self.bases(inventory_file):
            candidate = (base / path).resolve()
            if candidate.is_file():
                return candidate
        return None

    def warnings(self, sources: List[Dict[str, Any]], drafted: Path) -> List[str]:
        """Each locator the pack adds, checked as it will stand once the pack is accepted.

        `accept` keeps the store's entry for a document it already lists and adds the draft's only for a
        new one, so the check reads the documents the same way — the store's path wins, and a draft that
        disagrees is named. It once checked the draft's path instead, and so warned about a file the
        accept would never use, and passed a dead one it would keep."""
        from pygim.enact import digest

        listed = self.listed
        try:
            live, draft = inventory(listed), inventory(drafted)
        except ValueError as exc:
            return [str(exc)]
        known = {**{doc: p for doc, p in draft.items() if doc not in live}, **live}
        texts: Dict[str, Optional[List[str]]] = {}
        out, told = [], set()
        for s in sources:
            where = f"{s['tag']}: {s['doc']}:L{s['line']}"
            doc = s["doc"]
            if doc not in known:
                out.append(f"{where}: {doc} is not in the inventory — add it, or cite an inventoried document")
                continue
            if doc in live and doc in draft and draft[doc] != live[doc] and doc not in told:
                told.add(doc)
                out.append(f"{doc}: the store keeps {live[doc]}; the draft's {draft[doc]} is ignored — "
                           f"edit sources/inventory.yaml if the document moved")
            if doc not in texts:
                file = self.resolve(listed, known[doc])
                texts[doc] = _lines(file) if file else None
            text = texts[doc]
            if text is None:
                looked = ", ".join(str(b) for b in self.bases(listed))
                out.append(f"{where}: the document {known[doc]} was not found under {looked}")
                continue
            first, count = s["line"] - 1, s["lines"]
            if first < 0 or count < 1 or first + count > len(text):
                out.append(f"{where}: the document has {len(text)} lines")
                continue
            passage = text[first:first + count]
            if s["passage"] and digest("\n".join(passage).encode("utf-8")) != s["passage"]:
                out.append(f"{where}: the lines there are not the cited passage — the document changed, or the line is wrong")
                continue
            wanted = [x.strip() for x in passage]
            if not any(wanted):
                out.append(f"{where}: the passage {BLANK}")
                continue
            also = [i + 1 for i in range(len(text) - count + 1)
                    if i != first and [x.strip() for x in text[i:i + count]] == wanted]
            if also:
                shown = ", ".join(f"L{n}" for n in also[:5]) + (" …" if len(also) > 5 else "")
                out.append(f"{where}: the cited text also occurs at {shown} — check this is the occurrence meant")
        return out

    def merge(self, drafted: Path, memory: Any = None) -> Tuple[List[str], List[str]]:
        """Appends the drafted inventory's documents that the store's inventory lacks, and returns their
        ids — and, apart, each document the store already lists at another path, which it keeps.

        The second list exists because the merge once said nothing about them: after the 2026-09-22
        rename a draft could give `design-memory-00` its new path, the store kept the old one, and the
        pack's locators went on pointing at a file that no longer existed. Keeping the live entry is
        still right — a pack must not move another pack's documents — but it has to be said.

        What is appended is written by the YAML engine, so a path that needs quoting is quoted; and it
        starts on a line of its own. A hand-edited inventory whose last line had no newline once took the
        first appended id onto that line — `path: README.mdguide:` — and read back as one document
        pointing at the other's file, while the accept reported success.

        The store writes it (`write_file`: atomically, under the commit lock, only if the inventory still
        holds what was read here), and a change in between is read again rather than written over."""
        if not drafted.is_file():
            return [], []
        if memory is None:
            from pygim.enact import Enact

            memory = Enact(str(self.store))
        live = self.listed
        wanted = entries(drafted)
        for _ in range(3):                                # another writer between the read and the write: read again
            before = live.read_bytes() if live.is_file() else b""
            known = inventory(live)
            new = {doc: entry for doc, entry in wanted.items() if doc not in known}
            kept = [f"{doc}: kept {known[doc]}, the draft says {entry.get('path', '')}"
                    for doc, entry in wanted.items() if doc in known and known[doc] != str(entry.get("path") or "")]
            if not new:
                return [], kept
            text = before.decode("utf-8")
            lead = ("# The documents this vocabulary cites. Paths are relative to the project's root.\n" if not text
                    else "" if text.endswith("\n") else "\n")
            if memory.write_file("sources/inventory.yaml", text + lead + _yaml_text(new),
                                 _digest_of(live) if before else ""):
                return list(new), kept
        raise RuntimeError(f"{live} kept changing while the accept tried to add to it — run the accept again")

    def passage(self, locator: str) -> str:
        """The text a locator names, for a writer to check before citing it — and, by raising, the only
        check available at write time: that the document is inventoried here and the lines exist. It
        cannot tell whether the passage says what it is cited *for*; two of this store's own eight
        locators were in range and still quoted the wrong sentence."""
        m = _LOCATOR.match(locator.strip())
        if not m:
            raise ValueError(f"{locator!r} is not a locator — write document:L<line> or document:L<from>-<to>")
        doc, first = m.group("doc"), int(m.group("from"))
        last = int(m.group("to") or first)
        if first < 1:
            raise ValueError(f"{locator}: lines are counted from 1")
        if last < first:
            raise ValueError(f"{locator}: the range ends before it starts")
        listed = self.listed
        known = self.documents()
        if doc not in known:
            have = ", ".join(sorted(known)) or "nothing"
            raise KeyError(f"{doc} is not in this store's source inventory — it lists: {have}")
        file = self.resolve(listed, known[doc])
        if file is None:
            raise ValueError(f"{doc} is inventoried as {known[doc]!r}, which is not a file under this store or its project")
        text = _lines(file)
        if last > len(text):
            raise ValueError(f"{doc} has {len(text)} lines; {locator} runs past its end")
        return "\n".join(text[first - 1:last])

    def cite(self, path: str, line: int, lines: int = 1) -> Dict[str, Any]:
        """A locator into a project document: the passage at *line* (1-based) for *lines* lines. A document
        the *store*'s inventory already lists keeps that id; any other gets one made from its path.
        ``locator`` is the form a memory's ``cites`` takes."""
        from pygim.enact import digest

        file = (self.project / path).resolve() if not Path(path).is_absolute() else Path(path).resolve()
        try:
            relative = file.relative_to(self.project.resolve())
        except ValueError:
            raise ValueError(f"{file} is outside the project {self.project} — cite the project's own documents") from None
        # \r\n is a line ending, never part of a line, as for hand-written memories (corpus.h): a checkout
        # with Windows line endings must cite the same passage, and the same version, as one without.
        content = file.read_bytes().replace(b"\r\n", b"\n")
        text = _lines(file)
        if line < 1 or lines < 1 or line + lines - 1 > len(text):
            raise ValueError(f"{relative} has {len(text)} lines; line {line} for {lines} is out of range")
        passage = "\n".join(text[line - 1:line - 1 + lines])
        if not passage.strip():
            raise ValueError(f"{relative}:{line}: the passage {BLANK}")
        doc = doc_id(relative)
        if self.store is not None:
            listed = self.listed
            known_ids = inventory(listed)
            for known, known_path in known_ids.items():
                if self.resolve(listed, known_path) == file:
                    doc = known
                    break
            else:
                # `docs/a-b.md` and `docs/a/b.md` both flatten to `docs-a-b`; a new document never takes an
                # id another already has, or its locators would resolve to that document's file
                base, n = doc, 2
                while doc in known_ids:
                    doc, n = f"{base}-{n}", n + 1
        return {
            "source": {"doc": doc, "line": line, "lines": lines, "passage": digest(passage.encode("utf-8"))},
            "locator": f"{doc}:L{line}" + (f"-{line + lines - 1}" if lines > 1 else ""),
            "inventory": {"id": doc, "kind": "text", "path": relative.as_posix(), "version": digest(content)},
            "text": passage,
        }


# The functions callers have always used, each now one line over a `Sources`.


def _bases(inventory_file: Path, project: Optional[Path], store: Optional[Path]) -> List[Path]:
    return Sources(store, project).bases(inventory_file)


def _resolve(inventory_file: Path, path: str, project: Optional[Path], store: Optional[Path] = None) -> Optional[Path]:
    return Sources(store, project).resolve(inventory_file, path)


def _locator_warnings(sources: List[Dict[str, Any]], store: Path, drafted: Path, project: Optional[Path]) -> List[str]:
    return Sources(store, project).warnings(sources, drafted)


def _merge_inventory(store: Path, drafted: Path, memory: Any = None) -> Tuple[List[str], List[str]]:
    return Sources(store).merge(drafted, memory)


def passage(project: Optional[Path], store: Optional[Path], locator: str) -> str:
    """The text a locator names — see `Sources.passage`."""
    return Sources(store, project).passage(locator)


def cite(project: Path, path: str, line: int, lines: int = 1, store: Optional[Path] = None) -> Dict[str, Any]:
    """A locator into a project document — see `Sources.cite`."""
    return Sources(store, project).cite(path, line, lines)


def doc_id(relative: Path) -> str:
    """A stable, readable id for a document: its project-relative path with separators as dashes."""
    stem = relative.with_suffix("") if relative.suffix in (".md", ".rst", ".txt") else relative
    return re.sub(r"[^A-Za-z0-9_.]+", "-", stem.as_posix()).strip("-").lower()


_LOCATOR = re.compile(r"^(?P<doc>[^:\s]+):L(?P<from>\d+)(?:-(?P<to>\d+))?$")
