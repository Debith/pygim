"""Drafted vocabulary packs and the citations they rest on.

A pack drafted by the ``prepare-vocabulary`` prompt is a proposal until a person accepts it
(overview §4.6). ``check`` loads a proposal beside the store's current vocabulary in a scratch
store, so every error comes back by file and line without touching the real one; ``accept`` is
the person's step that makes it live. ``cite`` turns a line of a project document into a locator
— the passage digest a value carries, and the document's version for the inventory.

Citation paths are relative to the project's root, not to the store: a store in a user directory
or on the ``memory`` branch lives outside the checkout, and every worktree must resolve the same
document from the same path.
"""
from __future__ import annotations

import re
import shutil
import tempfile
from pathlib import Path
from typing import Any, Dict, List

_PACK_NAME = re.compile(r"^pack:\s*([A-Za-z0-9_\-]+)\s*$", re.MULTILINE)
_INVENTORY_ID = re.compile(r"^([A-Za-z0-9_.\-]+):\s*$", re.MULTILINE)


def pack_name(text: str) -> str:
    m = _PACK_NAME.search(text)
    if not m:
        raise ValueError("a pack starts with `pack: <domain>` — the domain's name is the pack's name")
    return m.group(1)


def check(store: Path, proposal: Path) -> Dict[str, Any]:
    """Loads *proposal* as ``taxonomy/pack-<name>.yaml`` beside the store's vocabulary, in a scratch
    copy. ``ok`` with what the pack adds, or ``ok: False`` with the loader's messages, which name
    the proposal's own path and line."""
    from pygim.memory import Memory, VocabularyError

    text = proposal.read_text(encoding="utf-8")
    try:
        name = pack_name(text)
    except ValueError as exc:
        return {"ok": False, "errors": f"{proposal}:1: {exc}"}
    target = f"pack-{name}.yaml"
    with tempfile.TemporaryDirectory(prefix="pygim-pack-check-") as tmp:
        scratch = Path(tmp) / "store"
        Memory.init(str(scratch))
        for existing in sorted((store / "taxonomy").glob("*.yaml")):
            shutil.copyfile(existing, scratch / "taxonomy" / existing.name)
        (scratch / "taxonomy" / target).write_text(text, encoding="utf-8")
        try:
            memory = Memory(str(scratch))
        except VocabularyError as exc:
            message = str(exc).replace(str(scratch / "taxonomy" / target), str(proposal)).replace(f"taxonomy/{target}", str(proposal))
            return {"ok": False, "errors": message}
        dims = [d for d in memory.vocabulary()["dimensions"] if d.get("pack") == name]
        del memory
    return {"ok": True, "pack": name, "dimensions": [d["name"] for d in dims],
            "values": sum(sum(1 for v in d["values"] if not v.get("any")) for d in dims),
            "replaces": (store / "taxonomy" / target).exists()}


def accept(store: Path, proposal: Path, replace: bool = False) -> Dict[str, Any]:
    """A person's step: checks the proposal, copies it to ``taxonomy/pack-<name>.yaml``, and adds the
    documents of an ``inventory.yaml`` beside it to ``sources/inventory.yaml``. A pack of that name
    already live is replaced only when asked."""
    result = check(store, proposal)
    if not result["ok"]:
        return result
    if result["replaces"] and not replace:
        return {"ok": False, "errors": f"taxonomy/pack-{result['pack']}.yaml is already live — pass --replace to replace it"}
    shutil.copyfile(proposal, store / "taxonomy" / f"pack-{result['pack']}.yaml")
    result["inventory"] = _merge_inventory(store, proposal.parent / "inventory.yaml")
    return result


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


def cite(project: Path, path: str, line: int, lines: int = 1) -> Dict[str, Any]:
    """A locator into a project document: the passage at *line* (1-based) for *lines* lines."""
    from pygim.memory import digest

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
    return {
        "source": {"doc": doc, "line": line, "lines": lines, "passage": digest(passage.encode("utf-8"))},
        "inventory": {"id": doc, "kind": "text", "path": relative.as_posix(), "version": digest(content)},
        "text": passage,
    }
