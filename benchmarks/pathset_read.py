"""PathSet.read_each() and read_all_files() against Python's open().read() loop, and a `**` glob
against pathlib's: the end-to-end rows of docs/design/pathset_storage.md ("Reading the members").

Sections:

1. **read** — every file of the corpus read whole: Python's loop (bytes, then str), read_all_files
   (a str per file), read_each (a view per file; and its text()), each warm (pages cached).
2. **cold** — the same with each file's pages dropped first (posix_fadvise DONTNEED): Linux, on a
   disk-backed file system; tmpfs keeps its pages whatever is asked, so there it measures warm.
3. **glob** — path(root).glob("**/*.py") and path(root).pathset("**/*.py") against
   pathlib.Path(root).rglob("*.py").

The corpus is --root DIR (CPython's Lib reproduces the design's table: 1,756 files, 32 MB), or else
20,000 generated files of about 260 bytes — the design's small-files row — in a temporary folder.

Run:  python benchmarks/pathset_read.py [--root DIR] [--cold] [--no-save]
Each run appends its raw measurements + environment metadata to
``results/pathset_read.jsonl`` (see ``_results.py``).
"""

import os
import pathlib  # the reference: pathlib's rglob, against path.glob('**/...')
import sys
import tempfile

from tabulate import tabulate

from pygim import path
from _results import corpus, save, wants_save

from _bench import REPS, argument, best_fresh, folder

SMALL_FILES = 20_000
SMALL_TEXT = "x = 1  # a line of a small module\n" * 7   # 259 bytes


def generated(root):
    """SMALL_FILES modules of SMALL_TEXT, 200 a folder."""
    for k in range(SMALL_FILES):
        folder = root / f"pkg{k // 200:03d}"
        folder.mkdir(exist_ok=True)
        (folder / f"m{k:05d}.py").write_text(SMALL_TEXT)
    return root


def is_utf8(content):
    """Whether a member's bytes, as read_each() read them, are UTF-8: every contender here makes a str of each."""
    try:
        content.text()
        return True
    except ValueError:
        return False


def drop_pages(files):
    """Each file's cached pages dropped, so the next read goes to the disk (Linux; ext4 honours it).
    *files* are the members' names: fadvise is a call per open file, which no PathSet operation makes."""
    for f in files:
        fd = os.open(f, os.O_RDONLY)
        try:
            os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
        finally:
            os.close(fd)


def reads(members, files):
    """What each contender does to the whole corpus: label -> call. Python's loop is handed the members'
    names (*files*), made before any timing, as a loop of its own would have them."""

    def python_bytes():
        out = []
        for f in files:
            with open(f, "rb") as fh:
                out.append(fh.read())
        return out

    def python_str():
        out = []
        for f in files:
            with open(f, encoding="utf-8") as fh:
                out.append(fh.read())
        return out

    return {
        "open().read() bytes": python_bytes,
        "open().read() str": python_str,
        "read_all_files() str": members.read_all_files,
        "read_each() views": lambda: [len(c) for c in members.read_each()],
        "read_each() text()": lambda: [c.text() for c in members.read_each()],
    }


def best_cold(call, files, reps=REPS):
    """best_fresh(), the pages dropped before each pass and outside its timing."""
    best = float("inf")
    for _ in range(reps):
        drop_pages(files)
        t, _ = best_fresh(call, reps=1)
        best = min(best, t)
    return best


def bench_reads(members, files, cold):
    rows, raw = [], {}
    for label, call in reads(members, files).items():
        t = best_cold(call, files) if cold else best_fresh(call)[0]
        raw[label] = {"seconds": t}
        rows.append([label, f"{t * 1e3:,.1f}"])
    return rows, raw


def bench_glob(root):
    rows, raw = [], {}
    for label, call in {"pathlib rglob('*.py')": lambda: list(pathlib.Path(root).rglob("*.py")),
                        "path.glob('**/*.py')": lambda: root.glob("**/*.py"),
                        "path.pathset('**/*.py')": lambda: root.pathset("**/*.py")}.items():
        t, found = best_fresh(call)
        raw[label] = {"seconds": t, "found": len(found)}
        rows.append([label, f"{t * 1e3:,.1f}", len(found)])
    return rows, raw


def main():
    given = argument("--root", folder)
    with tempfile.TemporaryDirectory() as scratch:
        root = given or generated(path(scratch))
        found = root.pathset("**/*.py", kind="file")   # sorted already
        members = found - [c.path for c in found.read_each() if not is_utf8(c)]   # CPython's Lib holds a few that are not, on purpose
        files, size = members.to_list(), sum(members.sizes())
        print(f"{len(members):,} UTF-8 files ({len(found) - len(members)} others left out), {size / 2**20:,.1f} MB "
              f"under {root if given else 'a generated tree'}; best of {REPS}, ms\n")
        sections = {"corpus": {"root": corpus(root) if given else "generated", "files": len(members), "bytes": size}}

        rows, sections["read"] = bench_reads(members, files, cold=False)
        print("read, warm\n" + tabulate(rows, headers=["", "ms"], disable_numparse=True) + "\n")
        if "--cold" in sys.argv:
            if not hasattr(os, "posix_fadvise"):
                print("cold: no posix_fadvise on this platform, skipped\n")
            else:
                rows, sections["cold"] = bench_reads(members, files, cold=True)
                print("read, cold (pages dropped; tmpfs keeps them)\n" +
                      tabulate(rows, headers=["", "ms"], disable_numparse=True) + "\n")
        rows, sections["glob"] = bench_glob(root)
        print("glob\n" + tabulate(rows, headers=["", "ms", "found"], disable_numparse=True))
    if wants_save():
        print(f"\nRun recorded -> {save('pathset_read', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
