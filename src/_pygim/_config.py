# -*- coding: utf-8 -*-
"""Everything this program reads from outside itself, read once, where it is wired.

One value object and one function that fills it. No other module looks at the environment, the
working directory or the platform to decide how to behave: they are handed an `Environment` and
use what it says. A component that cannot be built from plain values is reading configuration it
should have been given, and a test then has to arrange the machine to construct it — at which
point the test is no longer exercising the code that ships.

So this module is the only one that may touch `os.environ`, and `from_process` is the only function
in it that does: `read` is handed the variables, the working directory and the home directory, and
looks at nothing else — which is what lets a test call it with plain values. One composition root
calls it, `cli_oo` in `pygim.__main__`, for every command including the server, which `enact.run`
is handed.

Environment variables at a *process boundary* are legitimate — a reloaded server resuming the
session number it had cannot pass an argument to its own successor — and that boundary is here,
in the wiring, not one layer in.
"""
from __future__ import annotations

import os
import sys
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
TRUE = ("1", "true", "yes", "on")


@dataclass(frozen=True)
class Environment:
    """What the program was started with. Values only — anything that has to be worked out was
    worked out by `read` (`user_data` included: which of two directories holds the stores is a look
    at the disk, made once, here), and anything that changes while running is state, not
    configuration."""

    cwd: Path
    home: Path
    user_data: Path
    store_root: Optional[Path] = None
    store_root_from: str = ""     # what to tell a person named it: `--root` or `$PYGIM_ENACT_ROOT`
    global_root: Optional[Path] = None
    session: Optional[int] = None
    reloaded: bool = False
    colour: bool = True

    def at(self, cwd: Path) -> "Environment":
        """The same environment seen from another directory — what a command does when it is given
        a path to work in. A new value; nothing is mutated."""
        return replace(self, cwd=Path(cwd))

    def path(self, named: str) -> Path:
        """*named* as this environment reads it: `~` is its home, a relative path lies under its working
        directory. `expanduser()` and `resolve()` ask the process instead, whatever this environment
        says — which is how a test that named its own home got the developer's (2026-09-23)."""
        text = str(named)
        if text == "~" or text.startswith("~/"):
            found = self.home / text[2:]
        else:
            found = Path(text) if Path(text).is_absolute() else self.cwd / text
        return Path(os.path.normpath(found))

    def with_root(self, root: Optional[str], how: str = "--root") -> "Environment":
        return self if root is None else replace(self, store_root=self.path(str(root)), store_root_from=how)

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
    return here if _holds(here) or not _holds(was) else was


def _holds(directory: Path) -> bool:
    """Whether *directory* holds anything. Asking only whether it existed let a stray empty
    `pygim/enact/` hide every store kept in `pygim/memory/`."""
    from pygim.pathlike import path

    return directory.is_dir() and bool(path(str(directory)).iterdir())


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
    named_reload = _first(variables, RELOADEDS)
    if colour is None:
        # no-color.org: "when present and not an empty string (regardless of its value)"
        colour = not variables.get("NO_COLOR") and not variables.get(NO_COLOUR) and variables.get("TERM") != "dumb"
    number = named_session[1].strip() if named_session else ""
    base = Environment(cwd=Path(cwd), home=Path(home),
                       user_data=user_data(variables, Path(home), platform == "win32", platform == "darwin"))
    where = replace(
        base,
        store_root=base.path(named_root[1]) if named_root else None,
        store_root_from=f"${named_root[0]}" if named_root else "",
        global_root=base.path(named_global[1]) if named_global else None,
        session=int(number) if number.isdecimal() and number.isascii() else None,  # "²" is a digit; int() refuses it
        reloaded=bool(named_reload) and named_reload[1].strip().lower() in TRUE,
        colour=colour,
    )
    return where.with_root(root).with_session(session)


def from_process(**overrides) -> Environment:
    """`read` against this process. Called at a composition root and nowhere else."""
    return read(os.environ, cwd=Path(os.getcwd()), home=Path.home(), platform=sys.platform, **overrides)
