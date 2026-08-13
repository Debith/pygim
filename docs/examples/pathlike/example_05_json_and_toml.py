# type: ignore
"""The other engines: strict JSON (simdjson) and TOML (toml++).

Each extension selects a real, dedicated parser -- .json is NOT quietly
routed through the YAML engine, and .toml produces the same objects the
stdlib's tomllib would.

This example demonstrates:
- JSON decoding, and its strictness (YAML-isms fail with the filename)
- TOML decoding with dates/times as real datetime objects
"""

import datetime
import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

# ----------------------------------------------------------------------------
# 1. JSON: fast and strict
# ----------------------------------------------------------------------------
good = root / "data.json"
good.write_text('{"rows": [1, 2.5, true, null], "name": "x"}')
assert pygim.path(good).read() == {"rows": [1, 2.5, True, None], "name": "x"}

# {key: 1} is valid YAML but NOT valid JSON -- the error names the file.
sloppy = root / "sloppy.json"
sloppy.write_text("{key: 1}")
try:
    pygim.path(sloppy).read()
except RuntimeError as e:
    assert "sloppy.json" in str(e)
else:
    raise AssertionError("Expected strict JSON to reject a YAML-ism")

# ----------------------------------------------------------------------------
# 2. TOML: dates and times arrive as datetime objects (tomllib parity)
# ----------------------------------------------------------------------------
cfg = root / "app.toml"
cfg.write_text("""\
title = "svc"
[job]
day = 2024-01-15
runs_at = 10:30:00
stamp = 2024-01-15T10:30:00+02:00
""")

t = pygim.path(cfg).read()
assert t["title"] == "svc"
assert t["job"]["day"] == datetime.date(2024, 1, 15)
assert t["job"]["runs_at"] == datetime.time(10, 30)
assert t["job"]["stamp"].utcoffset() == datetime.timedelta(hours=2)

tmp.cleanup()
print("pathlike JSON & TOML example OK:", t["title"])
