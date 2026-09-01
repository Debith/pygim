"""Persist benchmark runs as JSON Lines so development can be followed over time.

Each run appends ONE record to ``results/<bench>.jsonl``: the raw measurements
(numeric, full precision — formatting is the printer's job) plus the metadata
needed to compare runs honestly later: commit, dirty flag, Python version,
CPU model and hostname. Records from different machines or dirty trees are
still recorded — the metadata is what lets an analysis *filter* them, so a
trend line never silently mixes incomparable environments.
"""

import json
import platform
import socket
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

RESULTS_DIR = Path(__file__).resolve().parent / "results"
SCHEMA = 1


def _git(*args):
    try:
        return subprocess.run(
            ["git", *args], cwd=Path(__file__).resolve().parent,
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def _cpu_model():
    if platform.system() == "Linux":
        try:
            for line in Path("/proc/cpuinfo").read_text().splitlines():
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
        "hostname": socket.gethostname(),
        "reps": reps,
        "sections": sections,
    }
    RESULTS_DIR.mkdir(exist_ok=True)
    out = RESULTS_DIR / f"{bench}.jsonl"
    with out.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")
    return out


def wants_save(argv=None):
    """True unless ``--no-save`` was passed (measure-only run)."""
    return "--no-save" not in (sys.argv[1:] if argv is None else argv)
