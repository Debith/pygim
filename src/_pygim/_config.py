# -*- coding: utf-8 -*-
"""Everything this program reads from outside itself, read once, where it is wired.

One value object and one function that fills it. No other module looks at the environment, the
working directory or the platform to decide how to behave: they are handed an `Environment` and
use what it says. A component that cannot be built from plain values is reading configuration it
should have been given, and a test then has to arrange the machine to construct it — at which
point the test is no longer exercising the code that ships.

So this module is the only one that may touch `os.environ`, and `read` is the only function in it
that does. Both entry points call it once and pass the result down: `pygim.__main__` for the
commands, `enact.run` for the server.

Environment variables at a *process boundary* are legitimate — a reloaded server resuming the
session number it had cannot pass an argument to its own successor — and that boundary is here,
in the wiring, not one layer in.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Mapping, Optional

ROOT = "PYGIM_ENACT_ROOT"
GLOBAL = "PYGIM_ENACT_GLOBAL"
SESSION = "PYGIM_ENACT_SESSION"
RELOADED = "PYGIM_ENACT_RELOADED"
NO_COLOUR = "PYGIM_NO_COLOR"

# The names this reads as well as the ones above, newest first: the system was called `memory`
# until 2026-09-22 and a shell, a script or a host may still set the old spelling.
ROOTS = (ROOT, "PYGIM_MEMORY_ROOT")
GLOBALS = (GLOBAL, "PYGIM_MEMORY_GLOBAL")
SESSIONS = (SESSION, "PYGIM_MEMORY_SESSION")
RELOADEDS = (RELOADED, "PYGIM_MEMORY_RELOADED")


@dataclass(frozen=True)
class Environment:
    """What the program was started with. Values only — anything that has to be worked out was
    worked out by `read`, and anything that changes while running is state, not configuration."""

    cwd: Path
    home: Path
    user_data: Path
    store_root: Optional[str] = None
    store_root_from: str = ""     # what to tell a person named it: `--root` or `$PYGIM_ENACT_ROOT`
    global_root: Optional[Path] = None
    session: Optional[int] = None
    reloaded: bool = False
    colour: bool = True

    def at(self, cwd: Path) -> "Environment":
        """The same environment seen from another directory — what a command does when it is given
        a path to work in. A new value; nothing is mutated."""
        return replace(self, cwd=Path(cwd))

    def with_root(self, root: Optional[str], how: str = "--root") -> "Environment":
        return self if root is None else replace(self, store_root=str(root), store_root_from=how)

    def with_session(self, session: Optional[int]) -> "Environment":
        return self if session is None else replace(self, session=int(session))


def user_data(variables: Mapping[str, str], home: Path, windows: bool, macos: bool) -> Path:
    """Where user-level stores live. The new place if it holds anything, else the one the stores
    already on this machine are in — nothing is moved by a rename."""
    if windows:
        base = Path(variables.get("LOCALAPPDATA") or home / "AppData" / "Local")
    elif macos:
        base = home / "Library" / "Application Support"
    else:
        base = Path(variables.get("XDG_DATA_HOME") or home / ".local" / "share")
    here, was = base / "pygim" / "enact", base / "pygim" / "memory"
    return here if here.is_dir() or not was.is_dir() else was


def _first(variables: Mapping[str, str], names) -> Optional[tuple]:
    for name in names:
        value = variables.get(name)
        if value:
            return name, value
    return None


def read(variables: Mapping[str, str], *, cwd: Path, home: Path, platform: str,
         root: Optional[str] = None, session: Optional[int] = None,
         colour: Optional[bool] = None) -> Environment:
    """The only function that inspects the environment. *root*, *session* and *colour* are what the
    command line said, and they win — a flag is configuration too, and it arrives here rather than
    being read again further in."""
    named_root = _first(variables, ROOTS)
    named_global = _first(variables, GLOBALS)
    named_session = _first(variables, SESSIONS)
    if colour is None:
        colour = (variables.get("NO_COLOR") is None and variables.get(NO_COLOUR) is None
                  and variables.get("TERM") != "dumb")
    where = Environment(
        cwd=Path(cwd),
        home=Path(home),
        user_data=user_data(variables, Path(home), platform == "win32", platform == "darwin"),
        store_root=named_root[1] if named_root else None,
        store_root_from=f"${named_root[0]}" if named_root else "",
        global_root=Path(named_global[1]).expanduser().resolve() if named_global else None,
        session=int(named_session[1]) if named_session and named_session[1].isdigit() else None,
        reloaded=_first(variables, RELOADEDS) is not None,
        colour=colour,
    )
    return where.with_root(root).with_session(session)


def from_process(**overrides) -> Environment:
    """`read` against this process. Called at a composition root and nowhere else."""
    import sys

    return read(os.environ, cwd=Path(os.getcwd()), home=Path.home(), platform=sys.platform, **overrides)
