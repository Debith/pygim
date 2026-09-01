# -*- coding: utf-8 -*-
"""Tests for setup.py build logic.

setup.py cannot be imported (it executes ``setup()``), so the functions under
test are extracted from its AST and exec'd in isolation. This keeps the build
system's logic — which otherwise only ever runs inside pip on CI — under the
same test discipline as the library.
"""

import ast
import os
import pathlib
import stat
import sys

import pytest

SETUP_PY = pathlib.Path(__file__).resolve().parents[2] / "setup.py"


def _extract(*names):
    """exec the named top-level defs/assigns from setup.py into a namespace."""
    tree = ast.parse(SETUP_PY.read_text(encoding="utf-8"))
    wanted = [
        node for node in tree.body
        if (isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in names)
        or (isinstance(node, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id in names for t in node.targets))
    ]
    assert len(wanted) == len(names), f"setup.py no longer defines all of {names}"
    ns = {"os": os, "sys": sys}
    exec(compile(ast.Module(body=wanted, type_ignores=[]), str(SETUP_PY), "exec"), ns)
    return ns


def _fake_compiler(tmp_path, accepted_flags):
    """A stand-in c++ that exits 0 only for the given -std= flags."""
    script = tmp_path / "fakexx"
    accepted = " ".join(f"-std={f}" for f in accepted_flags)
    script.write_text(
        "#!/bin/sh\n"
        f'case " $* " in\n'
        + "".join(f'  *" -std={f} "*) exit 0 ;;\n' for f in accepted_flags)
        + "  *) exit 1 ;;\nesac\n"
    )
    script.chmod(script.stat().st_mode | stat.S_IEXEC)
    return str(script)


@pytest.mark.skipif(sys.platform == "win32", reason="probe is POSIX-only (MSVC uses /std:c++latest)")
@pytest.mark.parametrize("accepted,requested,expected", [
    (["c++26", "c++2c", "c++23"], "c++26", "c++26"),   # modern compiler: keep the pin
    (["c++2c", "c++23"], "c++26", "c++2c"),            # GCC 14-era alias
    (["c++23"], "c++26", "c++23"),                     # GCC 13: the CI failure case
    ([], "c++26", "c++26"),                            # nothing accepted: let the build report it
])
def test_std_probe_walks_down_to_supported_flag(tmp_path, monkeypatch, accepted, requested, expected):
    ns = _extract("_first_supported_std", "_STD_PROBE_CACHE")
    monkeypatch.setenv("CXX", _fake_compiler(tmp_path, accepted))
    assert ns["_first_supported_std"](requested) == expected


@pytest.mark.skipif(sys.platform == "win32", reason="probe is POSIX-only")
def test_std_probe_caches_per_requested_standard(tmp_path, monkeypatch):
    ns = _extract("_first_supported_std", "_STD_PROBE_CACHE")
    monkeypatch.setenv("CXX", _fake_compiler(tmp_path, ["c++23"]))
    assert ns["_first_supported_std"]("c++26") == "c++23"
    # Second call must come from the cache: break the compiler to prove it.
    monkeypatch.setenv("CXX", "/nonexistent-compiler")
    assert ns["_first_supported_std"]("c++26") == "c++23"
