# type: ignore
"""JSON Lines: one document per line, a list in Python.

``.jsonl`` / ``.ndjson`` files are sequences of JSON documents separated by
newlines -- the natural shape for logs, event streams and append-style
records. ``pygim.path(...)`` reads one as a list (one item per document,
through simdjson's document stream) and writes a list back as one compact
JSON document per line.

This example demonstrates:
- Reading a .jsonl file into a list of native objects
- Blank lines being ignored, and parse errors naming the file and line
- Writing a list back, one line per item, and the round-trip
"""

import json
import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

# ----------------------------------------------------------------------------
# 1. Read: every line is a document, the file is a list
# ----------------------------------------------------------------------------
log = root / "events.jsonl"
log.write_text("""\
{"id": 1, "kind": "start", "ok": true}

{"id": 2, "kind": "note", "text": "two lines\\nin one string"}
{"id": 3, "kind": "stop", "took": 2.5}
""")

events = pygim.path(log).read()
assert len(events) == 3                       # the blank line is not a document
assert events[0] == {"id": 1, "kind": "start", "ok": True}
assert events[1]["text"] == "two lines\nin one string"
assert isinstance(events[2]["took"], float)

# ----------------------------------------------------------------------------
# 2. A broken line fails loudly, with the file and line number
# ----------------------------------------------------------------------------
bad = root / "bad.jsonl"
bad.write_text('{"id": 1}\n{oops}\n')
try:
    pygim.path(bad).read()
except RuntimeError as e:
    assert "bad.jsonl" in str(e) and "line 2" in str(e)
else:
    raise AssertionError("Expected the malformed line to be reported")

# ----------------------------------------------------------------------------
# 3. Write: a list in, one compact document per line out
# ----------------------------------------------------------------------------
out = pygim.path(root / "out.jsonl")
out.write([{"id": 1, "tags": ["a", "b"]}, {"id": 2, "tags": []}])

lines = (root / "out.jsonl").read_text().splitlines()
assert len(lines) == 2
assert [json.loads(line) for line in lines] == out.read()   # stdlib agrees, line by line

tmp.cleanup()
print("pathlike JSONL example OK:", len(events), "events")
