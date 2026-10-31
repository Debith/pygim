# type: ignore
"""Reading many files while the CPU works: ``PathSet.read_each()`` and ``read_all_files()``.

``read_each()`` hands over each member's contents in member order while C++ threads read the
next files with the GIL released — so the disk and your code are busy at the same time. Each
item is a ``FileContent``: a view of bytes held in C++, not a Python bytes or str object.
``memoryview(content)`` reads them without a copy; ``text()`` decodes only when you ask.

How many threads: none but yours at first; more while the rest of the job is worth them — up
to the cores for reads that compute (a warm cache), past them for reads that wait (a cold
disk, a network mount). ``threads=n`` fixes it. How far ahead: at most 1,024 files and
about ``ahead_bytes`` of them, so memory stays bounded however many files there are.

This example demonstrates:
- Views in member order, with the member each came from
- memoryview without a copy, and text() on demand
- Folders and missing paths left out; a file the OS refuses raised in its place
- Fixing the width, and bounding the read-ahead
- read_all_files() for the whole set as str
"""

import os
import sys
import tempfile
import zlib
from pathlib import Path

from pygim import PathSet

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)
for i in range(300):
    (root / f"part_{i:03d}.txt").write_text(f"part {i}\n" + "x" * (i * 13))
members = sorted(root.glob("*.txt"))

# ----------------------------------------------------------------------------
# 1. Views in member order, while threads read ahead
# ----------------------------------------------------------------------------
checksum = 0
first_lines = []
for content in PathSet(members).read_each():       # threads read the next files meanwhile
    checksum = zlib.crc32(content, checksum)         # a buffer: zlib reads it in place
    first_lines.append(content.text().split("\n", 1)[0])
assert first_lines == [f"part {i}" for i in range(300)]          # member order, every run
assert checksum == zlib.crc32(b"".join(m.read_bytes() for m in members))

# ----------------------------------------------------------------------------
# 2. A view is the bytes, without a copy
# ----------------------------------------------------------------------------
content = next(PathSet([members[1]]).read_each())
#                          ┌─ the memoryview's owner is the view itself: nothing was copied
#                          ▼
assert memoryview(content).obj is content
assert memoryview(content).readonly
assert len(content) == members[1].stat().st_size
# bytes() copies, when you want one
assert bytes(content) == members[1].read_bytes()
assert content.path.name == "part_001.txt"                       # the member it came from

# ----------------------------------------------------------------------------
# 3. What is not a file is left out; a refused file raises in its place
# ----------------------------------------------------------------------------
mixed = PathSet([members[0], root, root / "missing.txt", members[2]])
assert [c.path.name for c in mixed.read_each()] == ["part_000.txt", "part_002.txt"]

if sys.platform != "win32" and os.geteuid() != 0:   # permission bits bind a non-root POSIX user
    secret = root / "secret.txt"
    secret.write_text("no")
    secret.chmod(0)
    reading = PathSet([members[0], secret, members[1]]).read_each()
    assert next(reading).path.name == "part_000.txt"
    try:
        next(reading)
        raise AssertionError("a refused file raises")
    except PermissionError:
        # as pathlib's read_bytes() raises; the reading ends
        pass
    secret.chmod(0o600)

# ----------------------------------------------------------------------------
# 4. Fixing the width, bounding the read-ahead
# ----------------------------------------------------------------------------
#                                    ┌─ threads: how many read at once (None: as the job rewards)
#                                    │          ┌─ ahead_bytes: how much may wait for you, read
#                                    ▼          ▼
bounded = PathSet(members).read_each(threads=4, ahead_bytes=64 * 1024)
fixed = [c.text() for c in bounded]
assert fixed == [m.read_text() for m in members]                 # same results, whatever the width

# ----------------------------------------------------------------------------
# 5. read_all_files(): the whole set as str, in one call
# ----------------------------------------------------------------------------
texts = PathSet(members).read_all_files()                        # the reads run without the GIL
assert texts == [m.read_text() for m in members]

tmp.cleanup()
print(f"read_each example OK: {len(first_lines)} files, crc32 {checksum:#010x}")
