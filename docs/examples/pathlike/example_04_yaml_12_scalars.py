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

import pygim

tmp = tempfile.TemporaryDirectory()


def read(text):
    p = pygim.path(tmp.name) / "doc.yaml"
    p.write_text(text)
    return p.read()


# ----------------------------------------------------------------------------
# 1. Integers: hex and octal are ints; big integers convert exactly
# ----------------------------------------------------------------------------
s = read("""\
hex: 0x1A
octal: 0o17
big: 12345678901234567890123
""")
assert s == {"hex": 26, "octal": 15, "big": 12345678901234567890123}   # big: never a float

# ----------------------------------------------------------------------------
# 2. YAML 1.1 forms stay strings in 1.2 (PyYAML would type these!)
# ----------------------------------------------------------------------------
s = read("""\
bool_ish: yes
underscored: 1_000
binary_ish: 0b1
""")
assert s == {"bool_ish": "yes", "underscored": "1_000", "binary_ish": "0b1"}

# ----------------------------------------------------------------------------
# 3. Floats: only the dot-forms are special; bare "inf" is a string
# ----------------------------------------------------------------------------
s = read("""\
finite: 2.5
exp: 1e3
infinity: .inf
not_special: inf
""")
assert s == {"finite": 2.5, "exp": 1000.0, "infinity": math.inf, "not_special": "inf"}

# ----------------------------------------------------------------------------
# 4. Quoting always wins: a quoted scalar is a string, full stop
# ----------------------------------------------------------------------------
s = read("""\
quoted: '123'
plain: 123
""")
assert s == {"quoted": "123", "plain": 123}

tmp.cleanup()
print("pathlike YAML 1.2 scalars example OK")
