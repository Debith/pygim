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


# ─── Mandatory extensions: missing deps abort the build, never skip ──────────


def _dep_ns(**overrides):
    ns = _extract("_require_dep", "_odbc_include_dirs", "_odbc_library_name",
                  "_DEP_INSTALL_HINTS", "_DEP_CONFIGURATORS")
    ns["Path"] = pathlib.Path
    ns.setdefault("conda_prefix", None)
    ns.update(overrides)
    return ns


def test_missing_unixodbc_is_a_build_error_not_a_skip(tmp_path, monkeypatch):
    monkeypatch.setattr(sys, "platform", "linux")
    ns = _dep_ns(_odbc_include_dirs=lambda: [tmp_path])
    with pytest.raises(SystemExit) as exc:
        ns["_require_dep"]("odbc", "pygim._persistence")
    msg = str(exc.value)
    assert "pygim._persistence" in msg
    assert "sql.h" in msg and str(tmp_path) in msg
    assert "unixodbc" in msg.lower()  # actionable install hint


def test_present_unixodbc_header_passes(tmp_path, monkeypatch):
    monkeypatch.setattr(sys, "platform", "linux")
    (tmp_path / "sql.h").write_text("")
    ns = _dep_ns(_odbc_include_dirs=lambda: [tmp_path])
    assert ns["_require_dep"]("odbc", "pygim._persistence") is None


def test_odbc_on_windows_relies_on_the_sdk(monkeypatch):
    monkeypatch.setattr(sys, "platform", "win32")
    ns = _dep_ns(_odbc_include_dirs=list)  # no unixODBC anywhere
    assert ns["_require_dep"]("odbc", "pygim._persistence") is None


def test_unknown_dep_preset_is_a_build_error():
    ns = _dep_ns()
    with pytest.raises(SystemExit) as exc:
        ns["_require_dep"]("pg", "pygim.future")
    assert "'pg'" in str(exc.value) and "pygim.future" in str(exc.value)


@pytest.mark.parametrize("platform,expected", [
    ("linux", "odbc"), ("darwin", "odbc"), ("win32", "odbc32"),
])
def test_odbc_link_library_per_platform(monkeypatch, platform, expected):
    monkeypatch.setattr(sys, "platform", platform)
    assert _dep_ns()["_odbc_library_name"]() == expected


def test_setup_py_never_skips_an_extension():
    """Guard against the skip pattern creeping back in."""
    src = SETUP_PY.read_text(encoding="utf-8")
    assert "Skipping" not in src
    assert "_dep_available" not in src


def test_missing_compiler_is_a_preflight_build_error(monkeypatch):
    import shutil
    ns = _extract("_require_compiler")
    monkeypatch.setattr(sys, "platform", "linux")
    monkeypatch.delenv("CXX", raising=False)
    monkeypatch.setattr(shutil, "which", lambda _c: None)
    with pytest.raises(SystemExit) as exc:
        ns["_require_compiler"]()
    assert "No C++ compiler" in str(exc.value)


def test_present_compiler_passes(monkeypatch):
    import shutil
    ns = _extract("_require_compiler")
    monkeypatch.setattr(sys, "platform", "linux")
    monkeypatch.delenv("CXX", raising=False)
    monkeypatch.setattr(shutil, "which", lambda c: "/usr/bin/c++" if c == "c++" else None)
    assert ns["_require_compiler"]() is None


def test_no_availability_conditional_compilation():
    """Repository rule: code must never silently vary with the environment.

    Banned in first-party C++: `__has_include` (compiles different code
    depending on what happens to be installed) and the `#ifdef VERSION_INFO`
    fallback pattern (masks a build-system regression as version "dev").
    Platform selection (#if defined(_WIN32)), macro hygiene and explicit
    opt-in flags with environment-independent defaults remain allowed —
    see docs/design/cpp_runtime_linking.md.
    """
    fast = SETUP_PY.parent / "src" / "_pygim_fast"
    offenders = []
    for f in list(fast.rglob("*.h")) + list(fast.rglob("*.cpp")):
        if "third_party" in f.parts:
            continue  # vendored code is exempt
        text = f.read_text(encoding="utf-8", errors="ignore")
        for banned in ("__has_include", "#ifdef VERSION_INFO"):
            if banned in text:
                offenders.append(f"{f.relative_to(fast)}: {banned}")
    assert not offenders, offenders
