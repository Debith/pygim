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

# `_config` is the reader. `enact.py` sets two variables to carry the session across an exec, which
# is a process boundary and the one place that is legitimate — the successor's wiring reads them.
# `_docs_serve` is the docs server, which has not been wired this way yet.
MAY_READ = {"_pygim/_config.py", "_pygim/_mcp/enact.py", "_pygim/_cli/_docs_serve.py"}


def sources(root, pattern, least=1):
    """Every file matching *pattern* under *root*, with its text, read through pygim's own walker.

    A PathSet holds the paths as one table and `read_all_files` reads them in the C++ core: about
    six times faster than pathlib for this tree, and the library is the project's own — a test that
    walks a filesystem by hand while the package ships a walker is not eating what it cooks.

    `read_all_files` skips a member that is not a regular file, so its list can be shorter than the
    membership; a glob for files cannot produce that, and the assertion says so rather than
    trusting it."""
    from pygim.pathlike import PathSet, path

    found = PathSet(path(str(root)).rglob(pattern))
    members, texts = found.to_list(), found.read_all_files()
    assert len(members) == len(texts), "a member was not a regular file; the two lists no longer align"
    assert len(members) >= least, (
        f"only {len(members)} files matched {pattern} under {root} — a rule checked over nothing "
        f"passes for the wrong reason, and this suite uses the library it is testing to do the walk")
    return [(pathlib.Path(str(member)), text) for member, text in zip(members, texts)]


def reads_the_environment(text, filename="<source>"):
    """The lines where *text* actually reads the environment, asked of the syntax tree.

    Text would do here and be wrong: a module that *explains* the rule in a docstring is not
    breaking it, and `_config`'s docstring does exactly that."""
    import ast

    lines = []
    for node in ast.walk(ast.parse(text, filename=str(filename))):
        reads = (isinstance(node, ast.Attribute) and node.attr == "environ"
                 and isinstance(node.value, ast.Name) and node.value.id == "os")
        calls = (isinstance(node, ast.Call)
                 and ((isinstance(node.func, ast.Attribute) and node.func.attr in ("getenv", "environ"))
                      or (isinstance(node.func, ast.Name) and node.func.id == "getenv")))
        if reads or calls:
            lines.append(node.lineno)
    return sorted(set(lines))


@pytest.mark.skipif(not PY_ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def test_the_check_finds_the_case_it_is_hardest_on():
    """Before the check below is trusted, it is run against the one file that both reads the
    environment and writes about reading it. It must see the code and not the prose — the lesson of
    the adventure-craft study, B1: a verifier is not trusted until it has met its own hard case."""
    prose = chr(34) * 3
    assert reads_the_environment(f"{prose}A note about os.environ.{prose}\nx = 1  # os.environ\n") == []
    text = (PY_ROOT / "_pygim" / "_config.py").read_text(encoding="utf-8")
    found = reads_the_environment(text, "_config.py")
    assert found, "the check no longer sees the one module that certainly does read the environment"
    mentions = [n for n, line in enumerate(text.splitlines(), 1) if "os.environ" in line]
    assert [n for n in mentions if n not in found], "this file should mention it in prose as well as use it"


@pytest.mark.skipif(not PY_ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def test_only_the_wiring_reads_the_environment():
    """A component that reads the environment cannot be built by a test without arranging the
    machine around it, and then the test is not exercising the code that ships. Configuration is
    read once, at a composition root, and passed down as a value (global memory #9)."""
    offenders = []
    for file, text in sources(PY_ROOT, "*.py", least=20):     # never vacuously
        relative = file.relative_to(PY_ROOT).as_posix()
        if relative in MAY_READ or "third_party" in relative:
            continue
        offenders += [f"{relative}:{line}" for line in reads_the_environment(text, file)]
    assert offenders == [], ("these read the environment outside the wiring — take an Environment "
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
