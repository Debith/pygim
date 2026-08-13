# type: ignore
"""Read a config file in one call.

``pygim.path(...)`` wraps a filesystem path in a ``file`` object that knows
how to read and decode itself. The decoder is chosen from the extension
(``.yaml``/``.yml``, ``.json``, ``.toml``) -- no loader imports, no glue.

This example demonstrates:
- One call from a path to native Python objects
- The result being plain dicts / lists / scalars with native types
"""

import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
config = Path(tmp.name) / "app.yaml"
config.write_text("""\
server:
  host: db.example.com
  port: 5432
features: [alpha, beta]
debug: false
""")

# ----------------------------------------------------------------------------
# 1. One call: the path reads and decodes itself
# ----------------------------------------------------------------------------
obj = pygim.path(config).read()

assert obj == {
    "server": {"host": "db.example.com", "port": 5432},
    "features": ["alpha", "beta"],
    "debug": False,
}

# ----------------------------------------------------------------------------
# 2. The values are native Python types, not stringly-typed
# ----------------------------------------------------------------------------
assert isinstance(obj["server"]["port"], int)
assert obj["debug"] is False

tmp.cleanup()
print("pathlike read example OK:", obj["server"]["host"])
