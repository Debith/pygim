# -*- coding: utf-8 -*-
"""No Python C API call may run while an extension is loaded: only once a call is in progress.

On Windows, CPython loads an extension DLL with the GIL released — Python/dynload_win.c wraps
LoadLibraryExW in Py_BEGIN_ALLOW_THREADS — and a C++ static initializer runs inside that load. A
namespace-scope or class-scope `inline` variable initialized by a C API call (interning a name,
packing a tuple) therefore runs with no thread state there, and the DLL fails to load: "DLL load failed
while importing traits: A dynamic link library (DLL) initialization routine failed" (df8223f, Windows
CI, 2026-10-20). Linux's dlopen keeps the GIL, so nothing failed locally. Such a value is made on
first use instead, as a function-local static: under the GIL, since every call into pygim holds it.

This scans the native sources for the shape, so the mistake is caught on any platform.
"""

import re
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[2] / "src" / "_pygim_fast"
# An `inline` variable is never local to a function, so these match namespace and class scope only:
# one initialized by a C API call, and one holding a Python object initialized by any call — a wrapper
# (`interned("co_names")`) hides the C API call it makes.
INITIALIZED_BY_PYTHON = re.compile(
    r"^\s*(?:static\s+)?inline\s+[^;(]*?=\s*[^;]*\bPy[A-Z]\w*\s*\("
    r"|^\s*(?:static\s+)?inline\s+(?:const\s+)?(?:PyObject\s*\*|py::\w+)\s*(?:const\s+)?\w+\s*=\s*[^;]*\("
    # and a global or file-static written at the left margin, as namespace-scope definitions are here
    r"|^(?:static\s+)?(?:const\s+)?(?:PyObject\s*\*|py::\w+)\s*(?:const\s+)?\w+\s*=\s*[^;]*\(",
    re.MULTILINE)


def test_no_inline_variable_is_initialized_by_a_python_call():
    found = []
    for path in sorted(NATIVE.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp"} or "third_party" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in INITIALIZED_BY_PYTHON.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            found.append(f"{path.relative_to(NATIVE)}:{line}: {match.group(0).strip()}")
    assert not found, "made while the extension loads, with no GIL on Windows:\n" + "\n".join(found)
