"""path.read_bytes() against pathlib's, and the JSON engines' path.read() against Python's json, each
pair in one process (docs/definition_of_done.md: an A/B, never two runs).

Sections:

1. **bytes** — one file's bytes: ``pathlib.Path.read_bytes`` against ``path.read_bytes`` at
   100 bytes (a thousand reads), 1 MB and 50 MB, of random bytes. pygim reads with the system's
   calls straight into the bytes object, the GIL released (``platform::read_whole_into``).
2. **json** — a document of 400,000 records (about 37 MB): ``json.load`` against
   ``path.read()`` (simdjson, parsed where it was read).
3. **jsonl** — the same records one a line: ``json.loads`` of each line against ``path.read()``.

Each pair runs interleaved, best of ``REPS``, its objects dropped before each pass. The files are
generated in a temporary folder, read warm (their pages cached).

Run:  python benchmarks/pathlike_read.py [--no-save]
Each run appends its raw measurements + environment metadata to
``results/pathlike_read.jsonl`` (see ``_results.py``).
"""

import json  # the reference: Python's json, against the JSON and JSONL engines
import os
import pathlib  # the reference: pathlib's read_bytes, against path.read_bytes
import random
import tempfile

from tabulate import tabulate

from pygim import path
from _results import save, wants_save

from _bench import REPS, best_fresh

RECORDS = 400_000
SMALL_READS = 1_000


def records():
    """RECORDS rows of mixed fields, the same every run."""
    rng = random.Random(1)
    return [{"id": i, "name": f"user{i}", "score": rng.random(), "tags": ["a", "b", "c"][: i % 4], "ok": i % 2 == 0}
            for i in range(RECORDS)]


def pair(label, reference, ours):
    """Both calls timed interleaved, best of REPS each: (row, raw)."""
    best = {"reference": float("inf"), "pygim": float("inf")}
    for _ in range(REPS):
        for side, call in (("reference", reference), ("pygim", ours)):
            best[side] = min(best[side], best_fresh(call, reps=1)[0])
    ratio = best["reference"] / best["pygim"]
    return [label, f"{best['reference'] * 1e3:,.2f}", f"{best['pygim'] * 1e3:,.2f}", f"{ratio:.2f}x"], best


def bench_bytes(root):
    rows, raw = [], {}
    for name, size, reads in (("100 B", 100, SMALL_READS), ("1 MB", 1 << 20, 1), ("50 MB", 50_000_000, 1)):
        f = root / f"blob_{size}.bin"
        f.write_bytes(os.urandom(size))
        reference = pathlib.Path(str(f))
        assert f.read_bytes() == reference.read_bytes()
        label = f"{name} x {reads:,}" if reads > 1 else name
        row, raw[label] = pair(label, lambda r=reference, n=reads: [r.read_bytes() for _ in range(n)],
                               lambda p=f, n=reads: [p.read_bytes() for _ in range(n)])
        rows.append(row)
    return rows, raw


def bench_json(root, rows_of_data):
    rows, raw = [], {}
    document = root / "records.json"
    document.write(rows_of_data)    # pygim's own writers make the files both sides read
    lines = root / "records.jsonl"
    lines.write(rows_of_data)

    def python_json():
        with open(str(document), "rb") as fh:
            return json.load(fh)

    def python_jsonl():
        with open(str(lines), "rb") as fh:
            return [json.loads(line) for line in fh]

    assert document.read() == python_json()
    assert lines.read() == python_jsonl()
    for label, file, reference in (("json", document, python_json), ("jsonl", lines, python_jsonl)):
        size = f"{label} ({file.size() / 2**20:,.0f} MB)"
        row, raw[label] = pair(size, reference, lambda p=file: p.read())
        raw[label]["bytes"] = file.size()
        rows.append(row)
    return rows, raw


def main():
    headers = ["", "reference ms", "pygim ms", "pygim faster by"]
    with tempfile.TemporaryDirectory() as scratch:
        root = path(scratch)
        print(f"best of {REPS}, interleaved; reference: pathlib for bytes, Python's json for JSON\n")
        sections = {}
        rows, sections["bytes"] = bench_bytes(root)
        print("read_bytes() against pathlib's\n" + tabulate(rows, headers=headers, disable_numparse=True) + "\n")
        rows, sections["json"] = bench_json(root, records())
        print("read() against Python's json\n" + tabulate(rows, headers=headers, disable_numparse=True))
    if wants_save():
        print(f"\nRun recorded -> {save('pathlike_read', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
