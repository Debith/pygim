# -*- coding: utf-8 -*-
"""Stub (``.pyi``) blocks derived from the compiled modules.

pathlike's engine registry is open — one header per engine, discovered by the
build — so the engine-dependent part of ``pathlike.pyi`` (the ``Engine``
selector literal and the ``<name>file`` typed classes) is generated from the
built module's ``ENGINES`` record rather than hand-written. ``pygim stubs``
rewrites the marked block; ``tests/unittests/test_pathlike.py`` asserts it is
current, so a new engine cannot land with a stale stub.
"""

from __future__ import annotations

from pathlib import Path

__all__ = ["BEGIN", "END", "engine_block", "render", "stub_path", "update"]

BEGIN = "# --- generated: engines (regenerate with `pygim stubs`) ---"
END = "# --- end generated ---"


def engine_block() -> str:
    """The generated block, rendered from the built ``pygim.pathlike`` module."""
    from pygim import pathlike

    lines = [
        BEGIN,
        "# Selection accepts FORMAT names, LIBRARY labels and aliases; .engine reports the label.",
        "Engine = Literal[",
    ]
    for e in pathlike.ENGINES:
        lines.append("    " + ", ".join(f'"{s}"' for s in (e.name, e.label, *e.aliases)) + ",")
    lines.append("]")
    for e in pathlike.ENGINES:
        typed = getattr(pathlike, f"{e.name}file")   # its docstring is composed once, in C++ (bind_one)
        lines += [
            "",
            f"class {e.name}file(file):",
            f'    """{typed.__doc__}"""',
            "    def __init__(self, path: str | os.PathLike[str]) -> None: ...",
        ]
    lines += ["", "ENGINES: tuple[EngineInfo, ...]", END]
    return "\n".join(lines) + "\n"


def stub_path() -> Path:
    return Path(__file__).with_name("pathlike.pyi")


def _split(text: str) -> tuple[str, str, str]:
    """(head, generated block, tail) of a stub; raises if the markers are missing."""
    start, end = text.find(BEGIN), text.find(END)
    if start < 0 or end < 0 or end < start:
        raise ValueError(f"stub is missing the {BEGIN!r} / {END!r} markers")
    end += len(END) + 1  # the marker's own newline
    return text[:start], text[start:end], text[end:]


def render(text: str) -> str:
    """*text* with its generated block replaced by the current one."""
    head, _, tail = _split(text)
    return head + engine_block() + tail


def update(path: Path | None = None, *, check: bool = False) -> bool:
    """Bring the stub's generated block up to date. Returns True when the file
    was out of date; with ``check=True`` nothing is written."""
    path = path or stub_path()
    text = path.read_text(encoding="utf-8")
    new = render(text)
    if new == text:
        return False
    if not check:
        path.write_text(new, encoding="utf-8")
    return True
