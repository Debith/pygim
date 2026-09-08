# type: ignore
"""The type tells you the format: yamlpath / jsonpath / tomlpath.

``pygim.path(...)`` returns a subclass of ``path`` whose TYPE mirrors the
engine the path resolves to, and ``.engine`` names it. Both answer the same
question — "how will this file be decoded?" — one for isinstance checks and
type hints, one for reading and logging.

This example demonstrates:
- .engine naming the resolved engine for each format
- isinstance() against the typed subclasses
- The type staying truthful through pins and derived paths
- Constructing a typed file directly, which pins its format
"""

import pygim
from pygim.pathlike import path, jsonpath, tomlpath, yamlpath

# ----------------------------------------------------------------------------
# 1. The resolved engine is visible on every path
# ----------------------------------------------------------------------------
# Engines are LIBRARIES, so .engine names the actual decoder implementation.
assert pygim.path("app.yaml").engine == "rapidyaml"
assert pygim.path("app.toml").engine == "toml++"
assert pygim.path("app.json").engine == "simdjson"
assert pygim.path("app.txt").engine is None      # nothing resolves -> no engine

# ----------------------------------------------------------------------------
# 2. ...and mirrored in the type
# ----------------------------------------------------------------------------
assert isinstance(pygim.path("app.yaml"), yamlpath)
assert isinstance(pygim.path("app.toml"), tomlpath)
assert isinstance(pygim.path("app.json"), jsonpath)
assert isinstance(pygim.path("app.yaml"), file)  # every typed file is a file
assert type(pygim.path("app.txt")) is path       # unresolved stays plain

# ----------------------------------------------------------------------------
# 3. The type stays truthful through pins and derived paths
# ----------------------------------------------------------------------------
assert isinstance(pygim.path("data.json", engine="yaml"), yamlpath)   # pin wins
assert isinstance(pygim.path("a.yaml").with_suffix(".json"), jsonpath)
assert isinstance(pygim.path("cfg.yaml").parent / "x.toml", tomlpath)

# ----------------------------------------------------------------------------
# 4. Constructing a typed file directly pins its format
# ----------------------------------------------------------------------------
#                    ┌─ yamlpath(p) == pygim.path(p, engine="yaml")
#                    ▼
p = yamlpath("legacy.dat")
assert p.engine == "rapidyaml"
assert isinstance(p.with_name("other.dat"), yamlpath)    # pin travels, type too

print("pathlike typed files example OK:", type(pygim.path("app.yaml")).__name__)
