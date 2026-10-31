"""Persist benchmark runs as JSON Lines so development can be followed over time.

Each run appends ONE record to ``results/<bench>.jsonl``: the raw measurements
(numeric, full precision — formatting is the printer's job) plus the metadata
needed to compare runs honestly later: commit, dirty flag, Python version,
platform and CPU model. Records from different machines or dirty trees are
still recorded — the metadata is what lets an analysis *filter* them, so a
trend line never silently mixes incomparable environments. Never which
machine it was (Debith, 2026-10-31): no hostname, and no path on it — a
corpus is named by ``corpus()``, and ``save`` refuses a record holding a path.
"""

import json  # one record appended as a line: pygim's JSONL engine writes a whole file
import os
import platform
import re
import subprocess
import sys
from datetime import datetime, timezone

from pygim import path

HERE = path(__file__).resolve().parent
RESULTS_DIR = HERE / "results"
SCHEMA = 1


def _git(*args, cwd=None):
    try:
        return subprocess.run(
            ["git", *args], cwd=str(cwd or HERE),
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def corpus(folder):
    """A corpus as a record names it: its checkout's folder name and the path below it
    (``cpython/Lib``, ``llvm-project``), never where on this machine it lies; a folder outside any
    checkout by its own name."""
    here = path(os.path.expanduser(str(folder))).resolve()
    top = _git("rev-parse", "--show-toplevel", cwd=here if here.is_dir() else here.parent)
    if not top:
        return here.name
    checkout = path(top).resolve()
    return "/".join([checkout.name, *here.relative_to(checkout).parts])


def _local_paths(value, where):
    """(where, text) of each text in *value* that is a path on this machine."""
    if isinstance(value, dict):
        for key, item in value.items():
            yield from _local_paths(item, f"{where}.{key}")
    elif isinstance(value, (list, tuple)):
        for item in value:
            yield from _local_paths(item, f"{where}[]")
    elif isinstance(value, str) and (os.path.isabs(value) or value.startswith(("/", "~", "\\\\"))
                                     or re.match(r"^[A-Za-z]:[\\/]", value)):
        yield where, value


def _cpu_model():
    if platform.system() == "Linux":
        try:
            for line in path("/proc/cpuinfo").read_bytes().decode().splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    return platform.processor() or platform.machine()


# The convention: results ride IN the merge commit they describe, so the
# record cannot name that commit's hash (a file cannot contain the hash of
# the commit it is part of). The file's location is the attribution.
IN_TREE_NOTE = ("record is committed into the commit it measures; the file's "
                "location in history is the attribution.")


def save(bench, sections, *, reps, in_tree=True):
    """Append one run record to ``results/<bench>.jsonl``; return the file path.

    ``sections`` is the benchmark's own structure of raw measurements
    (seconds as floats). ``reps`` records the best-of-N protocol used.
    ``in_tree`` (the default, matching the one-record-per-merge convention)
    omits the self-referential commit hash; pass ``in_tree=False`` for a
    record that stays outside history and names the commit it measured.
    """
    for where, text in _local_paths(sections, "sections"):
        raise ValueError(f"{where} holds {text}, a path on this machine: name the corpus with corpus()")
    record = {
        "schema": SCHEMA,
        "bench": bench,
        "utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "commit": None if in_tree else _git("rev-parse", "--short", "HEAD"),
        "commit_note": IN_TREE_NOTE if in_tree else None,
        "branch": _git("rev-parse", "--abbrev-ref", "HEAD"),
        "dirty": bool(_git("status", "--porcelain")),
        "python": platform.python_version(),
        "impl": platform.python_implementation(),
        "platform": platform.platform(),
        "cpu": _cpu_model(),
        "reps": reps,
        "sections": sections,
    }
    RESULTS_DIR.mkdir(exist_ok=True)
    out = RESULTS_DIR / f"{bench}.jsonl"
    with open(str(out), "a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")
    return out


def wants_save(argv=None):
    """True unless ``--no-save`` was passed (measure-only run)."""
    return "--no-save" not in (sys.argv[1:] if argv is None else argv)
