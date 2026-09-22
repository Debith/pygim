# -*- coding: utf-8 -*-
"""The layers point one way (docs/definition_of_done.md): a core header is pybind-free.

Everything under src/_pygim_fast/mapping/ and the named core headers must build
without Python — that is what lets their laws be compile-time proofs. Only
adapter headers and bindings may include pybind11."""

import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2] / "src" / "_pygim_fast"
# CI removes src/ before running the suite against the installed package: this
# is a source-tree check, so it skips there rather than fail.
pytestmark = pytest.mark.skipif(not ROOT.is_dir(), reason="source tree not present (testing the installed package)")
CORE = sorted(ROOT.glob("mapping/*.h")) + [
    ROOT / "utils" / "hash.h",
    ROOT / "utils" / "memory.h",
    ROOT / "pathlike" / "core.h",
    ROOT / "pathlike" / "uri.h",
    ROOT / "pathlike" / "path_table.h",
    ROOT / "pathlike" / "engine_list.h",
]
PYBIND = re.compile(r'#\s*include\s*[<"]pybind11/')


def _code(header):
    """The header's text with line comments removed (a comment may quote an include)."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in header.read_text(encoding="utf-8").splitlines())


@pytest.mark.parametrize("header", CORE or [ROOT], ids=lambda p: str(p.relative_to(ROOT.parent)))
def test_core_header_is_pybind_free(header):
    text = _code(header)
    assert not PYBIND.search(text), f"{header.relative_to(ROOT)} includes pybind11: core headers must not"
    assert "Py_" not in text and "PyObject" not in text, f"{header.relative_to(ROOT)} touches the Python C API"


def test_core_headers_include_only_core():
    """A core header may include the standard library and other core headers, nothing from adapter/."""
    for header in CORE:
        for inc in re.findall(r'#\s*include\s*"([^"]+)"', _code(header)):
            assert "adapter/" not in inc and "bindings" not in inc, f"{header.relative_to(ROOT)} includes {inc}"


# ── configuration is read where the program is wired, and nowhere else ────────

PY_ROOT = pathlib.Path(__file__).resolve().parents[2] / "src"
READS_THE_ENVIRONMENT = re.compile(r"os\.environ|os\.getenv|getenv\(")

# `_config` is the reader. `enact.py` sets two variables to carry the session across an exec, which
# is a process boundary and the one place that is legitimate — the successor's wiring reads them.
# `_docs_serve` is the docs server, which has not been wired this way yet.
MAY_READ = {"_pygim/_config.py", "_pygim/_mcp/enact.py", "_pygim/_cli/_docs_serve.py"}


@pytest.mark.skipif(not PY_ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def test_only_the_wiring_reads_the_environment():
    """A component that reads the environment cannot be built by a test without arranging the
    machine around it, and then the test is not exercising the code that ships. Configuration is
    read once, at a composition root, and passed down as a value (global memory #9)."""
    offenders = []
    for file in sorted(PY_ROOT.rglob("*.py")):
        relative = file.relative_to(PY_ROOT).as_posix()
        if relative in MAY_READ or "third_party" in relative:
            continue
        for number, line in enumerate(file.read_text(encoding="utf-8").splitlines(), 1):
            if READS_THE_ENVIRONMENT.search(line) and not line.lstrip().startswith("#"):
                offenders.append(f"{relative}:{number}: {line.strip()}")
    assert offenders == [], ("these read the environment outside the wiring — pass an Environment "
                             "instead:\n" + "\n".join(offenders))


@pytest.mark.skipif(not PY_ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def test_the_servers_constructor_only_sets_values():
    """A constructor sets values: no logic, no loops, no IO. Anything that has to be read is read
    by `build`, the wiring, so the object can be made from plain values in a test exactly as it is
    made in production."""
    import ast

    source = (PY_ROOT / "_pygim" / "_mcp" / "enact.py").read_text(encoding="utf-8")
    server = next(n for n in ast.walk(ast.parse(source))
                  if isinstance(n, ast.ClassDef) and n.name == "EnactServer")
    init = next(n for n in server.body if isinstance(n, ast.FunctionDef) and n.name == "__init__")
    statements = [n for n in init.body if not isinstance(n, (ast.Expr, ast.Pass))]
    odd = [type(n).__name__ for n in statements if not isinstance(n, (ast.Assign, ast.AnnAssign))]
    assert odd == [], "EnactServer.__init__ does more than assign: " + ", ".join(odd)
    def eager(node):
        """Every call the constructor actually makes. A lambda's body is not walked: the dispatch
        table is a value made of deferred calls, and nothing in it runs while constructing."""
        for child in ast.iter_child_nodes(node):
            if isinstance(child, ast.Lambda):
                continue
            if isinstance(child, ast.Call):
                yield child
            yield from eager(child)

    calls = [c for n in statements for c in eager(n)]
    assert calls == [], "EnactServer.__init__ calls " + ", ".join(ast.unparse(c) for c in calls)


@pytest.mark.skipif(not PY_ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def test_no_test_can_reach_this_machines_own_stores(tmp_path):
    """The floor in conftest, proved rather than assumed: a test that arranges nothing at all still
    sees a redirected world, so `oo enact setup --user` in a forgetful test cannot create a store in
    the developer's real user data directory. One did, on 2026-09-22."""
    from _pygim import _config

    where = _config.from_process()
    assert where.home.is_relative_to(tmp_path), where.home
    assert where.user_data.is_relative_to(tmp_path), where.user_data
    assert where.store_root is None and where.session is None
