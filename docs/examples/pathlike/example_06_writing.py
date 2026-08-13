# type: ignore
"""Writing data back: the read side's mirror.

``write(obj)`` serialises dicts/lists/scalars with the resolved engine
(same precedence as read). Strings are quoted exactly when an unquoted
spelling would read back as a different type -- the same compile-time
classifiers gate both directions, so write/read round-trips by construction.

This example demonstrates:
- A basic YAML write and round-trip
- Trap strings ("true", "0x1A", ".inf") surviving as strings
- JSON writing, and its refusal of non-finite floats (like json.dumps)
- TOML writing: mapping root required, datetimes supported
"""

import json
import math
import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

# ----------------------------------------------------------------------------
# 1. Write YAML, read it back
# ----------------------------------------------------------------------------
out = pygim.path(root / "out.yaml")
out.write({"name": "job", "retries": 3, "ratio": 2.5, "active": True})
assert out.read() == {"name": "job", "retries": 3, "ratio": 2.5, "active": True}

# ----------------------------------------------------------------------------
# 2. Trap strings: values that would read back TYPED if written unquoted
# ----------------------------------------------------------------------------
# write() quotes exactly these, so the round-trip preserves types.
data = {"traps": ["true", "null", "0x1A", "1e3", ".inf"]}
out.write(data)
back = out.read()
assert back == data
assert all(isinstance(v, str) for v in back["traps"])

# ----------------------------------------------------------------------------
# 3. JSON: real JSON out (stdlib agrees); non-finite floats refused
# ----------------------------------------------------------------------------
jout = pygim.path(root / "data.json")
jout.write({"rows": [{"id": 1}, {"id": 2}]})
assert json.loads((root / "data.json").read_text()) == {"rows": [{"id": 1}, {"id": 2}]}

try:
    jout.write({"v": math.inf})
except ValueError as e:
    assert "non-finite" in str(e)
else:
    raise AssertionError("Expected JSON write to reject infinity")

# YAML has spellings for them, so there it round-trips:
yml = pygim.path(root / "inf.yaml")
yml.write({"v": math.inf})
assert yml.read() == {"v": math.inf}

# ----------------------------------------------------------------------------
# 4. TOML: mapping-rooted documents, with datetime support
# ----------------------------------------------------------------------------
# TOML documents ARE tables, so the root must be a mapping; and TOML has no
# null, so None is rejected — both loudly, at write time.
import datetime

tml = pygim.path(root / "cfg.toml")
tml.write({"title": "svc", "port": 8080, "day": datetime.date(2024, 1, 15)})
assert tml.read() == {"title": "svc", "port": 8080, "day": datetime.date(2024, 1, 15)}

try:
    tml.write([1, 2])
except ValueError as e:
    assert "mapping" in str(e)
else:
    raise AssertionError("Expected TOML write to require a mapping root")

tmp.cleanup()
print("pathlike writing example OK:", out.name)
