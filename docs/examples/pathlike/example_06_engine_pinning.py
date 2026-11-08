# type: ignore
"""Choosing the engine yourself: pinning, per-call override, the registry.

The decoder resolves in precedence order:

1. ``read(engine=...)``            -- per-call override
2. ``pygim.path(p, engine=...)``   -- pinned at construction
3. the file extension              -- the compile-time table

Each is one lookup in a table built at compile time, and what it finds is
the engine's own entry: the same entry reads, writes and picks the path's
type. ``pathlike.ENGINES`` is that registry as data.

This example demonstrates:
- Pinning an engine for files whose extension says nothing (".dat")
- The refusal to guess when nothing resolves
- The pin travelling with derived paths
- A per-call engine= winning over the pin
- One engine reading, writing and typing the path
- ``pathlike.ENGINES``: every name an engine answers to, read-only
"""

import tempfile
from pathlib import Path

import pygim
from pygim import pathlike
from pygim.pathlike import yamlpath

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
assert p.engine == "rapidyaml"
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
assert p.with_name("other.dat").engine == "rapidyaml"
assert p.parent.engine == "rapidyaml"

# ----------------------------------------------------------------------------
# 4. A per-call engine= overrides everything
# ----------------------------------------------------------------------------
doc = root / "doc.dat"
doc.write_text('{"a": 1}')
#                                       ┌─ per-call override: wins over the pin
#                                       ▼
assert pygim.path(doc, engine="yaml").read(engine="json") == {"a": 1}

# ----------------------------------------------------------------------------
# 5. One engine reads, writes and types the path
# ----------------------------------------------------------------------------
# The pin is the engine's entry, held by the path: writing goes through it as
# reading does, and the path's type comes from it too.
assert isinstance(p, yamlpath)
p.write({"name": "exported", "version": 4})
assert legacy.read_text() == "name: exported\nversion: 4\n"
assert p.read() == {"name": "exported", "version": 4}

p.write({"a": 1}, engine="json")                 # a per-call engine, as for read
assert legacy.read_text() == '{"a": 1}'

# ----------------------------------------------------------------------------
# 6. pathlike.ENGINES: the registry as data
# ----------------------------------------------------------------------------
# One entry per engine, each the engine's own descriptor.
engines = {entry.name: entry for entry in pathlike.ENGINES}
yaml = engines["yaml"]
assert type(yaml) is pathlike.EngineInfo
assert yaml.label == "rapidyaml"                 # the library: what .engine says
assert yaml.extensions == (".yaml", ".yml")      # what the extension table maps
assert yaml.aliases == ("yml",)                  # one more name engine= takes

# engine= takes the name, the label or an alias; an extension matches in any
# case. All of them find this one engine.
assert pygim.path(legacy, engine="yaml").engine == "rapidyaml"        # name
assert pygim.path(legacy, engine="rapidyaml").engine == "rapidyaml"   # label
assert pygim.path(legacy, engine="yml").engine == "rapidyaml"         # alias
assert pygim.path(root / "cfg.YML").engine == "rapidyaml"             # extension

# A name is spelt exactly: only extensions fold case.
try:
    pygim.path(legacy, engine="YAML")
except ValueError as e:
    assert "unknown engine: 'YAML'" in str(e)
else:
    raise AssertionError("Expected engine='YAML' to be unknown")

# The same holds for every engine the registry lists.
for entry in pathlike.ENGINES:
    for name in (entry.name, entry.label, *entry.aliases):
        assert pygim.path(legacy, engine=name).engine == entry.label
    for ext in entry.extensions:
        assert pygim.path(root / ("file" + ext.upper())).engine == entry.label

# The entries are read-only, and only the registry makes one.
try:
    yaml.name = "yml"
except AttributeError:
    pass
else:
    raise AssertionError("Expected an ENGINES entry to be read-only")

try:
    pathlike.EngineInfo()
except TypeError:
    pass
else:
    raise AssertionError("Expected EngineInfo() to be refused")

tmp.cleanup()
print("pathlike engine-pinning example OK:", p.engine)
