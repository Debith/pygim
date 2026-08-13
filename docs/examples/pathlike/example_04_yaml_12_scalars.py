# type: ignore
"""How YAML scalars become Python types -- the 1.2 core schema, exactly.

pygim implements YAML 1.2 (not 1.1, which PyYAML follows). The classifiers
are compile-time-proven against the spec, so what you see here is not
folklore: it is pinned behaviour.

This example demonstrates:
- Integer forms: decimal, hex (0x), octal (0o), and EXACT big integers
- What deliberately stays a string: the YAML 1.1-isms and bare "inf"
- Float forms, including the .inf / .nan dot-spellings
"""

import math
import tempfile
from pathlib import Path

import pygim

tmp = tempfile.TemporaryDirectory()


def read(text):
    p = Path(tmp.name) / "doc.yaml"
    p.write_text(text)
    return pygim.path(p).read()


# ----------------------------------------------------------------------------
# 1. Integers: hex and octal are ints; big integers convert exactly
# ----------------------------------------------------------------------------
s = read("hex: 0x1A\noctal: 0o17\nbig: 12345678901234567890123\n")
assert s["hex"] == 26
assert s["octal"] == 15
assert s["big"] == 12345678901234567890123      # never silently a float

# ----------------------------------------------------------------------------
# 2. YAML 1.1 forms stay strings in 1.2 (PyYAML would type these!)
# ----------------------------------------------------------------------------
s = read("bool_ish: yes\nunderscored: 1_000\nbinary_ish: 0b1\n")
assert s["bool_ish"] == "yes"
assert s["underscored"] == "1_000"
assert s["binary_ish"] == "0b1"

# ----------------------------------------------------------------------------
# 3. Floats: only the dot-forms are special; bare "inf" is a string
# ----------------------------------------------------------------------------
s = read("finite: 2.5\nexp: 1e3\ninfinity: .inf\nnot_special: inf\n")
assert s["finite"] == 2.5 and s["exp"] == 1000.0
assert s["infinity"] == math.inf
assert s["not_special"] == "inf"

# ----------------------------------------------------------------------------
# 4. Quoting always wins: a quoted scalar is a string, full stop
# ----------------------------------------------------------------------------
s = read("quoted: '123'\nplain: 123\n")
assert s["quoted"] == "123" and s["plain"] == 123

tmp.cleanup()
print("pathlike YAML 1.2 scalars example OK")
