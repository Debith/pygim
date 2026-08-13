# type: ignore
"""The type tells you the format: yamlfile / jsonfile / tomlfile.

``pygim.path(...)`` returns a subclass of ``file`` whose TYPE mirrors the
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
from pygim.pathlike import file, jsonfile, tomlfile, yamlfile

# ----------------------------------------------------------------------------
# 1. The resolved engine is visible on every path
# ----------------------------------------------------------------------------
assert pygim.path("app.yaml").engine == "yaml"
assert pygim.path("app.toml").engine == "toml"
assert pygim.path("app.json").engine == "json"
assert pygim.path("app.txt").engine is None      # nothing resolves -> no engine

# ----------------------------------------------------------------------------
# 2. ...and mirrored in the type
# ----------------------------------------------------------------------------
assert isinstance(pygim.path("app.yaml"), yamlfile)
assert isinstance(pygim.path("app.toml"), tomlfile)
assert isinstance(pygim.path("app.json"), jsonfile)
assert isinstance(pygim.path("app.yaml"), file)  # every typed file is a file
assert type(pygim.path("app.txt")) is file       # unresolved stays plain

# ----------------------------------------------------------------------------
# 3. The type stays truthful through pins and derived paths
# ----------------------------------------------------------------------------
assert isinstance(pygim.path("data.json", engine="yaml"), yamlfile)   # pin wins
assert isinstance(pygim.path("a.yaml").with_suffix(".json"), jsonfile)
assert isinstance(pygim.path("cfg.yaml").parent / "x.toml", tomlfile)

# ----------------------------------------------------------------------------
# 4. Constructing a typed file directly pins its format
# ----------------------------------------------------------------------------
#                    ┌─ yamlfile(p) == pygim.path(p, engine="yaml")
#                    ▼
p = yamlfile("legacy.dat")
assert p.engine == "yaml"
assert isinstance(p.with_name("other.dat"), yamlfile)    # pin travels, type too

print("pathlike typed files example OK:", type(pygim.path("app.yaml")).__name__)
