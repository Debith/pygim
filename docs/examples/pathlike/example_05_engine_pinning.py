# type: ignore
"""Choosing the engine yourself: pinning and per-call override.

The decoder resolves in precedence order:

1. ``read(engine=...)``            -- per-call override
2. ``pygim.path(p, engine=...)``   -- pinned at construction
3. the file extension              -- the compile-time table

This example demonstrates:
- Pinning an engine for files whose extension says nothing (".dat")
- The refusal to guess when nothing resolves
- The pin travelling with derived paths
- A per-call engine= winning over the pin
"""

import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)

legacy = root / "legacy.dat"
legacy.write_text("name: imported\nversion: 3\n")

# ----------------------------------------------------------------------------
# 1. Pin the engine when the extension says nothing
# ----------------------------------------------------------------------------
#                            ┌─ engine: pin the decoder for this object and
#                            │  everything derived from it (None = by extension)
#                            ▼
p = pygim.path(legacy, engine="yaml")
assert p.engine == "yaml"
assert p.read() == {"name": "imported", "version": 3}

# ----------------------------------------------------------------------------
# 2. Without a pin, an unknown extension refuses to guess
# ----------------------------------------------------------------------------
try:
    pygim.path(legacy).read()
except ValueError as e:
    assert "no engine" in str(e)
else:
    raise AssertionError("Expected unpinned .dat to refuse decoding")

# ----------------------------------------------------------------------------
# 3. The pin travels with derived paths
# ----------------------------------------------------------------------------
assert p.with_name("other.dat").engine == "yaml"
assert p.parent.engine == "yaml"

# ----------------------------------------------------------------------------
# 4. A per-call engine= overrides everything
# ----------------------------------------------------------------------------
doc = root / "doc.dat"
doc.write_text('{"a": 1}')
#                                       ┌─ per-call override: wins over the pin
#                                       ▼
assert pygim.path(doc, engine="yaml").read(engine="json") == {"a": 1}

tmp.cleanup()
print("pathlike engine-pinning example OK:", p.engine)
