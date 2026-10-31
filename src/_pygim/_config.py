# -*- coding: utf-8 -*-
"""Everything this program reads from outside itself, read once, where it is wired.

One value object and one function that fills it. No other module looks at the environment, the
working directory or the home directory to decide how to behave: they are handed an `Environment`
and use what it says. A component that cannot be built from plain values is reading configuration
it should have been given, and a test then has to arrange the machine to construct it — at which
point the test is no longer exercising the code that ships.

So this module is the only one that may touch `os.environ`, and `from_process` is the only function
in it that does: `read` is handed the variables, the working directory and the home directory, and
looks at nothing else — which is what lets a test call it with plain values. One composition root
calls it, `cli_oo` in `pygim.__main__`, for every command.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Mapping, Optional

NO_COLOUR = "PYGIM_NO_COLOR"


@dataclass(frozen=True)
class Environment:
    """What the program was started with. Values only — anything that has to be worked out was
    worked out by `read`, and anything that changes while running is state, not configuration."""

    cwd: Path
    home: Path
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


def read(variables: Mapping[str, str], *, cwd: Path, home: Path, colour: Optional[bool] = None) -> Environment:
    """The only function that inspects the environment. *colour* is what the command line said, and
    it wins — a flag is configuration too, and it arrives here rather than being read again further in."""
    if colour is None:
        # no-color.org: "when present and not an empty string (regardless of its value)"
        colour = not variables.get("NO_COLOR") and not variables.get(NO_COLOUR) and variables.get("TERM") != "dumb"
    return Environment(cwd=Path(cwd), home=Path(home), colour=colour)


def from_process(**overrides) -> Environment:
    """`read` against this process. Called at a composition root and nowhere else."""
    return read(os.environ, cwd=Path(os.getcwd()), home=Path.home(), **overrides)
