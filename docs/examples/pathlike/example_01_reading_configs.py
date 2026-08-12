# type: ignore
"""Reading config files with ``pygim.path`` -- a path that decodes itself.

``pygim.path(...)`` wraps a filesystem path in a ``file`` object that knows
how to *read and decode* its own contents. The decoder ("engine") is chosen
from the extension via a compile-time dispatch table:

- ``.yaml`` / ``.yml``  -> YAML (rapidyaml), YAML 1.2 core schema scalars
- ``.json``             -> JSON (simdjson, SIMD-accelerated, strict)
- ``.toml``             -> TOML (toml++), dates become ``datetime`` objects

The object also models ``os.PathLike`` with pathlib-style components, so it
drops into ``open()``, ``pathlib.Path()``, and friends.

This example demonstrates:
- One-call decoding of YAML, JSON, and TOML into native Python objects
- YAML 1.2 scalar typing: what becomes int/float/bool/None -- and what stays
  a string (including the 1.1-isms pygim deliberately rejects)
- Exact big integers and hex/octal forms
- TOML dates/times materialising as ``datetime`` objects (tomllib parity)
- Strict JSON: YAML-isms in a .json file fail loudly, with the filename
"""

import datetime
import math
import os
import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()
root = Path(tmp.name)


def write(name, text):
    p = root / name
    p.write_text(text)
    return p


# ----------------------------------------------------------------------------
# 1. One call: path -> native Python objects
# ----------------------------------------------------------------------------
config = write("app.yaml", """\
server:
  host: db.example.com
  port: 5432
features: [alpha, beta]
ratio: 2.5
debug: false
comment: null
""")

obj = pygim.path(config).read()
assert obj == {
    "server": {"host": "db.example.com", "port": 5432},
    "features": ["alpha", "beta"],
    "ratio": 2.5,
    "debug": False,
    "comment": None,
}
assert isinstance(obj["server"]["port"], int) and isinstance(obj["ratio"], float)

# ----------------------------------------------------------------------------
# 2. It is an os.PathLike with pathlib-style components
# ----------------------------------------------------------------------------
p = pygim.path(config)
assert os.fspath(p) == str(config)
assert p.name == "app.yaml" and p.suffix == ".yaml" and p.stem == "app"
assert (p.parent / "app.yaml").read() == obj      # composition with `/`
with open(p) as fh:                               # drops straight into open()
    assert "db.example.com" in fh.read()

# ----------------------------------------------------------------------------
# 3. YAML 1.2 core schema scalars -- typed exactly, no folklore
# ----------------------------------------------------------------------------
# The classifiers are compile-time-proven against the spec: hex/octal are
# ints, huge integers convert exactly (never silently becoming floats), and
# YAML 1.1 forms (yes/on/1_000) stay strings -- unlike PyYAML.
scalars = write("scalars.yaml", """\
hex: 0x1A
octal: 0o17
big: 12345678901234567890123
one_one_bool: yes
underscored: 1_000
quoted: '123'
infinity: .inf
not_inf: inf
""")

s = pygim.path(scalars).read()
assert s["hex"] == 26 and s["octal"] == 15
assert s["big"] == 12345678901234567890123          # exact, arbitrary precision
assert s["one_one_bool"] == "yes"                   # 1.1 bool -> 1.2 string
assert s["underscored"] == "1_000"                  # 1.1 int  -> 1.2 string
assert s["quoted"] == "123"                         # quoting suppresses typing
assert s["infinity"] == math.inf                    # only the dot-form is float
assert s["not_inf"] == "inf"                        # bare "inf" is a string

# ----------------------------------------------------------------------------
# 4. TOML: dates and times arrive as datetime objects (tomllib parity)
# ----------------------------------------------------------------------------
tom = write("app.toml", """\
title = "svc"
[job]
runs_at = 10:30:00
day = 2024-01-15
stamp = 2024-01-15T10:30:00+02:00
""")

t = pygim.path(tom).read()
assert t["title"] == "svc"
assert t["job"]["day"] == datetime.date(2024, 1, 15)
assert t["job"]["runs_at"] == datetime.time(10, 30)
assert t["job"]["stamp"].utcoffset() == datetime.timedelta(hours=2)

# ----------------------------------------------------------------------------
# 5. JSON is strict -- and parse errors carry the filename
# ----------------------------------------------------------------------------
# {key: 1} is valid YAML but NOT valid JSON: the .json extension selects the
# real simdjson engine, so this fails loudly instead of being quietly parsed
# as YAML.
sloppy = write("sloppy.json", "{key: 1}")
try:
    pygim.path(sloppy).read()
except RuntimeError as e:
    assert "sloppy.json" in str(e)
else:
    raise AssertionError("Expected strict JSON to reject a YAML-ism")

tmp.cleanup()
print("pathlike reading example OK:", p)
