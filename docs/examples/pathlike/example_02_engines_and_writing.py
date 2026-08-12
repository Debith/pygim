# type: ignore
"""Choosing engines explicitly, and writing data back.

The decoder resolves in precedence order:

1. ``read(engine=...)`` / ``write(obj, engine=...)`` -- per-call override
2. ``pygim.path(p, engine=...)``                     -- pinned at construction
3. the file extension                                -- compile-time table

``write(obj)`` is the read side's mirror: it serialises dicts/lists/scalars
with the resolved engine, and quotes strings *exactly* when an unquoted
spelling would read back as a different type -- the same compile-time
classifiers gate both directions, so write/read round-trips by construction.

This example demonstrates:
- Pinning an engine for files whose extension says nothing (".dat", ".cfg")
- The pin inheriting through derived paths, and per-call override winning
- Round-tripping data with "trap" strings ("true", "0x1A", ".inf")
- YAML vs JSON policies for non-finite floats
- TOML being read-only (write refuses loudly)
"""

import json
import math
import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

# ----------------------------------------------------------------------------
# 1. Pin the engine when the extension says nothing
# ----------------------------------------------------------------------------
legacy = root / "legacy.dat"
legacy.write_text("name: imported\nversion: 3\n")

#                            ┌─ engine: pin the decoder for this object and
#                            │  everything derived from it (None = by extension)
#                            ▼
p = pygim.path(legacy, engine="yaml")
assert p.engine == "yaml"
assert p.read() == {"name": "imported", "version": 3}

# Without a pin the same file refuses to guess:
try:
    pygim.path(legacy).read()
except ValueError as e:
    assert "no engine" in str(e)
else:
    raise AssertionError("Expected unpinned .dat to refuse decoding")

# ----------------------------------------------------------------------------
# 2. The pin travels with derived paths; a per-call engine= overrides it
# ----------------------------------------------------------------------------
sibling = p.with_name("other.dat")            # derived path, same pin
assert sibling.engine == "yaml"

as_json = root / "doc.dat"
as_json.write_text('{"a": 1}')
#                                       ┌─ per-call override: wins over the pin
#                                       ▼
assert pygim.path(as_json, engine="yaml").read(engine="json") == {"a": 1}

# ----------------------------------------------------------------------------
# 3. write(): the round-trip is guaranteed, even for trap strings
# ----------------------------------------------------------------------------
# Every string below would read back as a DIFFERENT type if written unquoted.
# write() consults the same classifiers read() uses and quotes exactly those.
data = {
    "traps": ["true", "null", "0x1A", "1e3", ".inf", "010"],
    "typed": [True, None, 26, 1000.0, math.inf, 10],
    "big": 12345678901234567890123,
    "nested": {"empty_list": [], "empty_map": {}},
}
out = pygim.path(root / "out.yaml")
out.write(data)
back = out.read()
assert back == data
assert repr(back) == repr(data)                # types survive, not just equality

# ----------------------------------------------------------------------------
# 4. Non-finite floats: YAML has spellings, JSON refuses (like json.dumps)
# ----------------------------------------------------------------------------
yml = pygim.path(root / "inf.yaml")
yml.write({"v": math.inf})
assert yml.read() == {"v": math.inf}

try:
    pygim.path(root / "inf.json").write({"v": math.inf})
except ValueError as e:
    assert "non-finite" in str(e)
else:
    raise AssertionError("Expected JSON write to reject infinity")

# ----------------------------------------------------------------------------
# 5. Written JSON is real JSON -- the stdlib agrees byte-for-byte on meaning
# ----------------------------------------------------------------------------
jout = pygim.path(root / "data.json")
payload = {"rows": [{"id": 1, "name": "a"}, {"id": 2, "name": "b"}]}
jout.write(payload)
assert json.loads((root / "data.json").read_text()) == payload
assert jout.read() == payload

# ----------------------------------------------------------------------------
# 6. TOML is a read-only engine (for now): write refuses loudly
# ----------------------------------------------------------------------------
try:
    pygim.path(root / "cfg.toml").write({"a": 1})
except ValueError as e:
    assert "toml write" in str(e)
else:
    raise AssertionError("Expected TOML write to be refused")

tmp.cleanup()
print("pathlike engines & writing example OK:", out)
