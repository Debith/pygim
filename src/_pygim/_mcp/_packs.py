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

import re
import shutil
import tempfile
from pathlib import Path
from typing import Any, Dict, List, Optional

_PACK_NAME = re.compile(r"^pack:\s*([A-Za-z0-9_\-]+)\s*$", re.MULTILINE)
_INVENTORY_ID = re.compile(r"^([A-Za-z0-9_.\-]+):\s*$", re.MULTILINE)


def pack_name(text: str) -> str:
    m = _PACK_NAME.search(text)
    if not m:
        raise ValueError("a pack starts with `pack: <domain>` — the domain's name is the pack's name")
    return m.group(1)


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
        name = pack_name(text)
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
    shutil.copyfile(proposal, store / "taxonomy" / f"pack-{result['pack']}.yaml")
    result["inventory"] = _merge_inventory(store, proposal.parent / "inventory.yaml")
    return result


def inventory(path: Path) -> Dict[str, str]:
    """The documents of an inventory file: id to path, as written."""
    out: Dict[str, str] = {}
    if not path.is_file():
        return out
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        m = _INVENTORY_ID.match(line)
        if m:
            current = m.group(1)
            out[current] = ""
            continue
        field = re.match(r"^\s+path:\s*(.*?)\s*$", line)
        if field and current is not None:
            out[current] = field.group(1).strip("\"'")
    return out


def _bases(inventory_file: Path, project: Optional[Path], store: Optional[Path]) -> List[Path]:
    """Where an inventoried path may be relative to: the project's root (02 §5.2); the checkout the
    store says it serves, for a store kept outside it; the store's own root, for a body of knowledge
    with its own sources and no checkout; and the inventory file, for stores written before the
    first rule."""
    from . import _stores

    served = _stores.project_of(store) if store is not None else None
    seen: List[Path] = []
    for base in (project, served, store, inventory_file.parent):
        if base is not None and base not in seen:
            seen.append(base)
    return seen


def _resolve(inventory_file: Path, path: str, project: Optional[Path], store: Optional[Path] = None) -> Optional[Path]:
    for base in _bases(inventory_file, project, store):
        candidate = (base / path).resolve()
        if candidate.is_file():
            return candidate
    return None


def _lines(file: Path) -> List[str]:
    return file.read_bytes().replace(b"\r\n", b"\n").decode("utf-8").split("\n")


def _locator_warnings(sources: List[Dict[str, Any]], store: Path, drafted: Path, project: Optional[Path]) -> List[str]:
    from pygim.enact import digest

    known = {doc: (store / "sources" / "inventory.yaml", path) for doc, path in inventory(store / "sources" / "inventory.yaml").items()}
    known.update({doc: (drafted, path) for doc, path in inventory(drafted).items()})
    texts: Dict[str, Optional[List[str]]] = {}
    out = []
    for s in sources:
        where = f"{s['tag']}: {s['doc']}:L{s['line']}"
        if s["doc"] not in known:
            out.append(f"{where}: {s['doc']} is not in the inventory — add it, or cite an inventoried document")
            continue
        if s["doc"] not in texts:
            file = _resolve(known[s["doc"]][0], known[s["doc"]][1], project, store)
            texts[s["doc"]] = _lines(file) if file else None
        text = texts[s["doc"]]
        if text is None:
            looked = ", ".join(str(b) for b in _bases(known[s["doc"]][0], project, store))
            out.append(f"{where}: the document {known[s['doc']][1]} was not found under {looked}")
            continue
        first, count = s["line"] - 1, s["lines"]
        if first + count > len(text):
            out.append(f"{where}: the document has {len(text)} lines")
            continue
        passage = text[first:first + count]
        if s["passage"] and digest("\n".join(passage).encode("utf-8")) != s["passage"]:
            out.append(f"{where}: the lines there are not the cited passage — the document changed, or the line is wrong")
            continue
        wanted = [x.strip() for x in passage]
        if not any(wanted):
            continue
        also = [i + 1 for i in range(len(text) - count + 1)
                if i != first and [x.strip() for x in text[i:i + count]] == wanted]
        if also:
            shown = ", ".join(f"L{n}" for n in also[:5]) + (" …" if len(also) > 5 else "")
            out.append(f"{where}: the cited text also occurs at {shown} — check this is the occurrence meant")
    return out


def _merge_inventory(store: Path, drafted: Path) -> List[str]:
    """Appends the drafted inventory's documents that the store's inventory lacks; returns their ids."""
    if not drafted.is_file():
        return []
    live = store / "sources" / "inventory.yaml"
    live.parent.mkdir(parents=True, exist_ok=True)
    have = set(_INVENTORY_ID.findall(live.read_text(encoding="utf-8"))) if live.is_file() else set()
    blocks = re.split(r"(?m)^(?=[A-Za-z0-9_.\-]+:\s*$)", drafted.read_text(encoding="utf-8"))
    added, keep = [], []
    for block in blocks:
        m = _INVENTORY_ID.match(block)
        if m and m.group(1) not in have:
            added.append(m.group(1))
            keep.append(block.rstrip() + "\n")
    if added:
        header = "" if live.is_file() else "# The documents this vocabulary cites. Paths are relative to the project's root.\n"
        with live.open("a", encoding="utf-8") as fh:
            fh.write(header + "".join(keep))
    return added


def doc_id(relative: Path) -> str:
    """A stable, readable id for a document: its project-relative path with separators as dashes."""
    stem = relative.with_suffix("") if relative.suffix in (".md", ".rst", ".txt") else relative
    return re.sub(r"[^A-Za-z0-9_.]+", "-", stem.as_posix()).strip("-").lower()


_LOCATOR = re.compile(r"^(?P<doc>[^:\s]+):L(?P<from>\d+)(?:-(?P<to>\d+))?$")


def passage(project: Optional[Path], store: Optional[Path], locator: str) -> str:
    """The text a locator names, for a writer to check before citing it — and, by raising, the only
    check available at write time: that the document is inventoried here and the lines exist. It
    cannot tell whether the passage says what it is cited *for*; two of this store's own eight
    locators were in range and still quoted the wrong sentence."""
    m = _LOCATOR.match(locator.strip())
    if not m:
        raise ValueError(f"{locator!r} is not a locator — write document:L<line> or document:L<from>-<to>")
    doc, first = m.group("doc"), int(m.group("from"))
    last = int(m.group("to") or first)
    if last < first:
        raise ValueError(f"{locator}: the range ends before it starts")
    listed = (store / "sources" / "inventory.yaml") if store is not None else None
    known = inventory(listed) if listed is not None else {}
    if doc not in known:
        have = ", ".join(sorted(known)) or "nothing"
        raise KeyError(f"{doc} is not in this store's source inventory — it lists: {have}")
    file = _resolve(listed, known[doc], project, store)
    if file is None:
        raise ValueError(f"{doc} is inventoried as {known[doc]!r}, which is not a file under this store or its project")
    text = file.read_bytes().replace(b"\r\n", b"\n").decode("utf-8").split("\n")
    if last > len(text):
        raise ValueError(f"{doc} has {len(text)} lines; {locator} runs past its end")
    return "\n".join(text[first - 1:last])


def cite(project: Path, path: str, line: int, lines: int = 1, store: Optional[Path] = None) -> Dict[str, Any]:
    """A locator into a project document: the passage at *line* (1-based) for *lines* lines. A document
    the *store*'s inventory already lists keeps that id; any other gets one made from its path.
    ``locator`` is the form a memory's ``cites`` takes."""
    from pygim.enact import digest

    file = (project / path).resolve() if not Path(path).is_absolute() else Path(path).resolve()
    try:
        relative = file.relative_to(project.resolve())
    except ValueError:
        raise ValueError(f"{file} is outside the project {project} — cite the project's own documents") from None
    # \r\n is a line ending, never part of a line, as for hand-written memories (corpus.h): a checkout
    # with Windows line endings must cite the same passage, and the same version, as one without.
    content = file.read_bytes().replace(b"\r\n", b"\n")
    text = content.decode("utf-8").split("\n")
    if line < 1 or line + lines - 1 > len(text):
        raise ValueError(f"{relative} has {len(text)} lines; line {line} for {lines} is out of range")
    passage = "\n".join(text[line - 1:line - 1 + lines])
    doc = doc_id(relative)
    if store is not None:
        listed = store / "sources" / "inventory.yaml"
        for known, known_path in inventory(listed).items():
            if _resolve(listed, known_path, project, store) == file:
                doc = known
                break
    return {
        "source": {"doc": doc, "line": line, "lines": lines, "passage": digest(passage.encode("utf-8"))},
        "locator": f"{doc}:L{line}" + (f"-{line + lines - 1}" if lines > 1 else ""),
        "inventory": {"id": doc, "kind": "text", "path": relative.as_posix(), "version": digest(content)},
        "text": passage,
    }
