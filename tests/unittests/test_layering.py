# -*- coding: utf-8 -*-
"""The layers point one way (docs/definition_of_done.md): a core header is pybind-free.

Everything under src/_pygim_fast/mapping/ and the named core headers must build
without Python — that is what lets their laws be compile-time proofs. Only
adapter headers and bindings may include pybind11."""

import ast
import pathlib
import re

import pytest

from _pygim import _config
from pygim import code
from pygim.pathlike import PathSet, path

ROOT = pathlib.Path(__file__).resolve().parents[2] / "src" / "_pygim_fast"
# CI removes src/ before running the suite against the installed package: this
# is a source-tree check, so it skips there rather than fail.
pytestmark = pytest.mark.skipif(not ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def _headers(pattern):
    """The headers matching *pattern*, through pygim's own walker — see `sources` below on why the
    project's components are used by the project even where the milliseconds do not matter."""
    return sorted(pathlib.Path(str(p)) for p in path(str(ROOT)).glob(pattern))


def _declares_core(header):
    """Whether *header*'s opening comment (the text before its first #include) says it is in the CORE layer."""
    text = header.read_text(encoding="utf-8", errors="replace")
    first = re.search(r"^\s*#\s*include", text, re.M)
    return "CORE layer" in text[:first.start() if first else len(text)]


# Every header that says it is a core one: the marker is the list, so a core header is never left
# off it by hand (pathlike/scalars.h and utils/pieces.h were, until 2026-10-30).
CORE = [h for h in _headers("**/*.h") if "third_party" not in h.parts and _declares_core(h)]
# Where the source tree is (CI tests the installed package without it, and skips this module).
assert not ROOT.is_dir() or len(CORE) >= 54, f"only {len(CORE)} headers say they are CORE: the marker is the list"


PYBIND = re.compile(r'#\s*include\s*[<"]pybind11/')


def _code(header):
    """The header's text with line comments removed (a comment may quote an include)."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in _text(header).splitlines())


def _text(header):
    """One header, read through PathSet — the same path every other read in this file takes."""
    read = PathSet([path(str(header))]).read_all_files()
    assert read, f"{header} is not a readable file"
    return read[0]


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


# ── a query is built and rendered from plain values ───────────────────────────

QUERY_HEADERS = [ROOT / "persistence" / "core" / "query.h", ROOT / "persistence" / "core" / "dialect.h",
                 *_headers("persistence/strategy/*/dialect.h")]
QUERY_MAY_INCLUDE = {ROOT / "persistence" / "core" / "query.h", ROOT / "utils" / "logging.h"}


def _beyond_plain_values(header, text):
    """The includes in *text* that reach past the standard library, Query and logging. A standard
    header is `<name>` with no dot and no slash, so a C library's `<sql.h>` counts against it."""
    found = []
    for quoted, angled in re.findall(r'#\s*include\s*(?:"([^"]+)"|<([^>]+)>)', text):
        if angled and not re.search(r"[./]", angled):
            continue
        if quoted and (header.parent / quoted).resolve() in QUERY_MAY_INCLUDE:
            continue
        found.append(quoted or f"<{angled}>")
    return found


def test_the_include_check_finds_what_would_reach_a_session():
    """Run against its own hard case before it is trusted: an ODBC header, a sibling strategy header
    and pybind11 are caught; the standard library and Query itself are not."""
    dialect = ROOT / "persistence" / "strategy" / "mssql" / "dialect.h"
    text = '#include <format>\n#include "../../core/query.h"\n#include <sql.h>\n#include "load_impl.h"\n' \
           '#include <pybind11/pybind11.h>\n'
    assert _beyond_plain_values(dialect, text) == ["<sql.h>", "load_impl.h", "<pybind11/pybind11.h>"]


def test_a_query_and_its_dialects_need_no_session_or_database():
    """Building and rendering a query takes plain values — no connection string, store or session
    (memory #144). Query and every dialect include only the standard library, Query and logging,
    so nothing a connection needs can reach them."""
    assert len(QUERY_HEADERS) >= 3, QUERY_HEADERS
    offenders = {str(h.relative_to(ROOT)): found for h in QUERY_HEADERS if (found := _beyond_plain_values(h, _code(h)))}
    assert not offenders, offenders


# ── configuration is read where the program is wired, and nowhere else ────────

PY_ROOT = pathlib.Path(__file__).resolve().parents[2] / "src"

# `_config` is the reader. `_docs_serve` is the docs server, which has not been wired this way yet.
MAY_READ = {"_pygim/_config.py", "_pygim/_cli/_docs_serve.py"}


def sources(root, pattern, least=1):
    """Every file matching *pattern* under *root*, with its text, read through pygim's own walker.

    A PathSet holds the paths as one table and `read_all_files` reads them in the C++ core: about
    six times faster than pathlib for this tree, and the library is the project's own — a test that
    walks a filesystem by hand while the package ships a walker is not eating what it cooks.

    `read_all_files` skips a member that is not a regular file, so its list can be shorter than the
    membership; a glob for files cannot produce that, and the assertion says so rather than
    trusting it."""
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
def test_no_test_can_reach_this_machines_own_home(tmp_path):
    """The floor in conftest, proved rather than assumed: a test that arranges nothing at all still
    sees a redirected world, so a forgetful test cannot write into the developer's real home."""
    assert _config.from_process().home.is_relative_to(tmp_path)


# ── the project uses what the project ships ──────────────────────────────────

BY_HAND = {"rglob", "iterdir", "glob"}          # pathlib's walkers
OS_WALKS = {"walk", "listdir", "scandir"}       # and os's
# `test_pathlike` and `test_path_store` walk with pathlib on purpose: it is the oracle they hold
# PathSet to. Benchmarks compare the two by definition.
ORACLE = ("test_pathlike.py", "test_path_store.py", "benchmarks/")

# What is still walked by hand, counted on 2026-09-22 (19), 2026-09-23 (17), 2026-10-29 (14:
# `pygim clean` walks with pygim's paths) and 2026-10-31 (4). It may fall; it may not rise. A
# ratchet rather than a ban, because banning it today would mean converting nineteen call sites in
# one commit, and a rule that has to be obeyed all at once is a rule that gets turned off.
WALKS_BY_HAND = 4


OURS = {"path", "PathSet", "PathStore"}         # pygim's own, which is the point of the rule


def _hand_walks(text):
    """Where *text* walks a filesystem itself. `ast.walk` is not a filesystem walk, and neither is
    `path(x).rglob(...)` — that is the walker this rule exists to promote, and a check that counted
    obeying it as breaking it would be read once and then turned off.

    Receivers are followed through the idioms that keep a path ours: an assignment from `path(…)`
    or from `ours / "part"`, and a loop over what one of ours yields (`for child in here.iterdir()`).
    That is as far as a syntax tree can honestly go. Anything less direct is still counted, so the
    number can only be too high, never too low."""
    if not any(word in text for word in BY_HAND | OS_WALKS):
        return []                                       # a walk is called by its name: none named, none made
    nodes = list(ast.walk(ast.parse(text)))             # walked once; read four times below

    def is_ours(expr, ours):
        if isinstance(expr, ast.Call):
            if getattr(expr.func, "id", None) in OURS:
                return True
            if isinstance(expr.func, ast.Attribute) and expr.func.attr in OURS and getattr(expr.func.value, "id", None) == "pygim":
                return True                             # pygim.path(...), pygim.PathSet(...)
            if isinstance(expr.func, ast.Attribute):
                return is_ours(expr.func.value, ours)
        if isinstance(expr, ast.Name):
            return expr.id in ours
        if isinstance(expr, ast.BinOp):
            return is_ours(expr.left, ours)
        return False

    ours = set()
    binding = [node for node in nodes if isinstance(node, (ast.Assign, ast.For, ast.comprehension))]
    for _ in range(3):                                  # a name made from one made from one
        for node in binding:
            if isinstance(node, ast.Assign) and is_ours(node.value, ours):
                ours |= {t.id for t in node.targets if isinstance(t, ast.Name)}
            elif isinstance(node, (ast.For, ast.comprehension)):
                source = node.iter
                if isinstance(source, ast.Call) and getattr(source.func, "id", None) == "sorted" and source.args:
                    source = source.args[0]
                if is_ours(source, ours) and isinstance(node.target, ast.Name):
                    ours.add(node.target.id)

    out = []
    for node in nodes:
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)):
            continue
        receiver = getattr(node.func.value, "id", None)
        on_ours = is_ours(node.func.value, ours)
        if node.func.attr in BY_HAND and receiver != "ast" and not on_ours:
            out.append((node.lineno, node.func.attr))
        elif node.func.attr in OS_WALKS and receiver == "os":
            out.append((node.lineno, "os." + node.func.attr))
    return out


def test_the_walk_checker_knows_our_walker_from_the_hand_written_one():
    """Run against its own hard case before it is trusted, as the environment check is: the whole
    rule is "use ours", so mistaking ours for theirs would make obedience look like a violation."""
    assert _hand_walks("from pathlib import Path\nPath('.').rglob('*.py')\n")
    assert _hand_walks("import os\nos.walk('.')\n") == [(2, "os.walk")]
    assert _hand_walks("from pygim.pathlike import path\npath('.').rglob('*.py')\n") == []
    assert _hand_walks("import pygim\npygim.path('.').glob('**/*.py')\n") == []
    assert _hand_walks("from pygim.pathlike import path\nhere = path('.')\nhere.iterdir()\n") == []
    assert _hand_walks("here = path('.') / 'src'\nfor child in sorted(here.iterdir(), key=str):\n"
                       "    child.iterdir()\n") == []
    assert _hand_walks("from pathlib import Path\nhere = Path('.') / 'src'\nhere.iterdir()\n")


@pytest.mark.skipif(not PY_ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def test_the_project_walks_with_its_own_walker():
    """pygim ships PathSet, so pygim uses PathSet. The argument is not consistency and not speed —
    it is that using a component is the only cheap way to find out what is wrong with it. Converting
    one test on 2026-09-22 found a latent bug in the test and an API sharpness in `read_all_files`
    within the hour; neither was going to be found by admiring it from outside (global memory #20).

    This counts what is left and refuses to let it grow. When the count falls, lower the number —
    the second assertion makes that non-optional, so the budget cannot quietly become a licence."""
    repo = PY_ROOT.parent
    found = []
    for area in ("src", "tests"):
        for file, text in sources(repo / area, "*.py", least=5):
            relative = file.relative_to(repo).as_posix()
            if "third_party" in relative or any(o in relative for o in ORACLE):
                continue
            found += [f"{relative}:{line}: {how}()" for line, how in _hand_walks(text)]
    assert len(found) <= WALKS_BY_HAND, (
        f"{len(found)} hand-written walks, up from {WALKS_BY_HAND}. Use PathSet:\n"
        + "\n".join(found))
    assert len(found) >= WALKS_BY_HAND - 2, (
        f"only {len(found)} hand-written walks are left, and WALKS_BY_HAND still says "
        f"{WALKS_BY_HAND} — lower it, or the budget stops meaning anything")


# ── a test reaches the code through its public names ─────────────────────────

# A test that imports an internal — `_pygim.*`, or a `_name` of `pygim` — has found missing API or a
# missing seam: the name a user would need is not public, or the test checks a part it should reach
# through the whole. Benchmarks and runnable examples are tests too. What is left is counted, and
# the count may only fall. Not counted: a dunder module such as `pygim.__main__`, the command itself.
INTERNAL_IMPORTS = 30


def _internal_imports(text):
    """(line, dotted name) for every import in *text* that reaches a pygim internal."""
    def internal(module, names):
        parts = module.split(".")
        if parts[0] not in ("pygim", "_pygim"):
            return []
        if any(p.startswith("_") and not p.startswith("__") for p in parts):
            return [module]
        return [f"{module}.{n}" for n in names if n.startswith("_") and not n.startswith("__")]

    out = []
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            out += [(node.lineno, name) for name in internal(node.module, [a.name for a in node.names])]
        elif isinstance(node, ast.Import):
            out += [(node.lineno, name) for a in node.names for name in internal(a.name, [])]
    return out


def test_the_import_checker_knows_internal_from_public():
    """Run against its own hard case before it is trusted: the rule is "public names only", so
    calling a public import internal would make obedience look like a violation."""
    assert _internal_imports("from _pygim._cli import _style\n") == [(1, "_pygim._cli")]
    assert _internal_imports("from pygim import _persistence\n") == [(1, "pygim._persistence")]
    assert _internal_imports("import pygim._stubs as s\n") == [(1, "pygim._stubs")]
    assert _internal_imports("from pygim.persistence import Query, acquire_datastore\n") == []
    assert _internal_imports("from pygim.__main__ import cli_oo\nfrom __future__ import annotations\n") == []
    assert _internal_imports("from os import _exit\nimport json\n") == []


@pytest.mark.skip(reason="until the CLI app has a public name: the application is public by "
                         "definition (Debith, 2026-10-05), but today it lives in the private _pygim package")
@pytest.mark.skipif(not PY_ROOT.is_dir(), reason="source tree not present (testing the installed package)")
def test_tests_reach_the_code_through_its_public_names():
    """Counts what is left and refuses to let it grow; when the count falls, lower the number —
    the second assertion makes that non-optional, so the budget cannot quietly become a licence."""
    repo = PY_ROOT.parent
    found = []
    for area in ("tests", "benchmarks", "docs/examples"):
        for file, text in sources(repo / area, "*.py", least=5):
            found += [f"{file.relative_to(repo).as_posix()}:{line}: {name}" for line, name in _internal_imports(text)]
    assert len(found) <= INTERNAL_IMPORTS, (
        f"{len(found)} imports of internals, up from {INTERNAL_IMPORTS}. Reach the code through its "
        f"public names — or make the name public:\n" + "\n".join(found))
    assert len(found) >= INTERNAL_IMPORTS - 2, (
        f"only {len(found)} imports of internals are left, and INTERNAL_IMPORTS still says "
        f"{INTERNAL_IMPORTS} — lower it, or the budget stops meaning anything")


def test_time_is_read_in_one_header():
    """Timing code takes its clock (#264): only utils/timestamp.h reads a std::chrono clock, in
    wall_clock, monotonic_clock and thread_cpu_clock. Found 2026-10-30: ScopeLog, the BCP
    profiler, the fetch benchmark and the sanitizer hunt each read steady_clock themselves."""
    readers = re.compile(r"std::chrono::(steady_clock|system_clock|high_resolution_clock)\b")
    found = []
    for area, pattern in ((ROOT, "**/*.h"), (ROOT, "**/*.cpp"), (ROOT.parents[1] / "tests" / "static", "*.cpp")):
        for file, text in sources(area, pattern):
            if file.name == "timestamp.h" or "third_party" in file.as_posix():
                continue
            if readers.search(_code(file)):
                found.append(file.name)
    assert found == []



def test_each_test_module_has_a_name_of_its_own():
    """pytest imports a test module by its file name, with no package around it, so two of one name
    stop the whole run at collection — while each folder run alone passes. tests/examples' and
    tests/unittests' test_examples.py did, 2026-10-30."""
    repo = ROOT.parents[1]
    names = [pathlib.Path(str(p)).name for top in ("tests", "src") for p in path(str(repo / top)).glob("**/test_*.py")]
    assert sorted({n for n in names if names.count(n) > 1}) == []


# Every conditional on the operating system outside src/_pygim_fast/platform/, the boundary the
# system's rules and calls belong behind (docs/design/platform_boundary.md, D77): counted on
# 2026-10-31 (59), 56 once the path rules moved (path_rules.h), 47 once the listing did, 45 once the
# errno translation did, 39 once the file system's writes did (platform/files.h). It may fall, as
# each part moves behind the boundary; it may not rise. A compiler's own checks (_MSC_VER,
# __clang__) are build portability, not the system's, and are not counted.
OS_CONDITIONALS = 39
OS_MACRO = re.compile(r"^[ \t]*#[ \t]*(?:if|ifdef|ifndef|elif)\b.*\b(?:_WIN32|_WIN64|__APPLE__|__linux__|__unix__|__FreeBSD__)\b", re.M)


def test_the_operating_system_is_asked_only_behind_the_platform_boundary():
    found = []
    for pattern in ("*.h", "*.cpp"):
        for file, text in sources(ROOT, pattern):
            relative = file.relative_to(ROOT).as_posix()
            if "third_party" in file.parts or relative.startswith("platform/"):
                continue
            found += [f"{relative}:{text.count(chr(10), 0, m.start()) + 1}" for m in OS_MACRO.finditer(text)]
    assert len(found) <= OS_CONDITIONALS, (
        f"{len(found)} conditionals on the operating system outside platform/, up from {OS_CONDITIONALS}: put the "
        "system's rules and calls behind the boundary:\n" + "\n".join(found))
    assert len(found) >= OS_CONDITIONALS - 2, (
        f"only {len(found)} are left, and OS_CONDITIONALS still says {OS_CONDITIONALS}: lower it")


# Every place the C++ imports a Python module, and why. #248: reach Python through the C API, never
# import while running; where no C API answers, say so and ask. A new import fails here until it is
# listed with its reason.
IMPORTS = {
    "pathlike/adapter/bindings.cpp": 1,          # sys, while the module loads
    "pathlike/adapter/adapter.h": 1,             # collections.namedtuple for ENGINES, while the module loads
    "utils/bindings.cpp": 1,                     # collections.abc (MutableMapping, Mapping), while the module loads
    "persistence/adapter/bindings.cpp": 1,       # pygim.core.explib's GimError, while the module loads
    "utils/arrow_stream.h": 2,                   # pyarrow's table and reader, looked up once at first use (D53)
    "persistence/adapter/arrow_export.h": 2,     # pyarrow's reader import and polars.from_arrow, once at first use (D53)
    "wiring/ioc/adapter.h": 2,                   # inspect and typing, for a constructor its code object cannot say (D67)
    "wiring/factory/adapter.h": 1,               # Factory.use_module(name): importing is what it is asked
}


def test_the_cpp_imports_a_module_only_where_listed():
    """pathlike's TOML engine imported datetime on its first date, kept in static py::objects that were
    destroyed after the interpreter at exit; it takes datetime's C API while the module loads
    (2026-10-30)."""
    found = {}
    for pattern in ("*.h", "*.cpp"):
        for file, _ in sources(ROOT, pattern):
            if "third_party" in file.parts:
                continue
            n = _code(file).count("py::module_::import(")
            if n:
                found[file.relative_to(ROOT).as_posix()] = n
    assert found == IMPORTS


def test_no_assert_compares_a_value_with_itself():
    """`assert default_store().stats()["rows"] == default_store().stats()["rows"]` backed the claim
    that a walk leaves the default table untouched, and could never fail: both sides were read after
    the walk (pathlike's example_12, until 2026-10-30). `x is x` stays: evaluated twice, it asks
    whether a property or a container hands back the one object. Found by pygim's own parser."""
    repo, found = ROOT.parents[1], []
    for top in ("tests", "docs/examples", "src", "benchmarks"):
        for tree in code.parse(str(repo / top), "**/*.py"):
            for node in tree.find("Assert"):
                test = node.field("test")
                if test.kind != "Compare" or test.field("ops") != ["Eq"]:
                    continue
                if test.field("left").source == test.field("comparators")[0].source:
                    found.append(f"{tree.path}:{node.lineno}")
    assert found == []


def test_a_module_a_sanitizer_run_cannot_load_is_imported_where_its_absence_is_handled():
    """A sanitizer run makes some modules unloadable (UNLOADABLE, tests/static/run_tests_under_sanitizers.py)
    and a test needing one skips. `import polars` in test_similarity's header stopped that run at
    collection, every test of the module with it (2026-10-31): such an import is made in a try, or
    by a test's pytest.importorskip — never by a module's own top-level statement."""
    repo = ROOT.parents[1]
    runner = code.parse_source((repo / "tests" / "static" / "run_tests_under_sanitizers.py").read_bytes())
    listed = next(n for n in runner.find("Assign") if n.field("targets")[0].source == "UNLOADABLE")
    unloadable = {name.field("value") for name in listed.field("value").field("elts")}
    found = []
    for tree in code.parse(str(repo / "tests"), "**/*.py"):
        for statement in tree.root.children:   # the module's own statements: a try's are inside it
            if statement.kind == "Import":
                names = [alias.field("name") for alias in statement.field("names")]
            elif statement.kind == "ImportFrom" and statement.field("module"):
                names = [statement.field("module")]
            else:
                continue
            found += [f"{tree.path}:{statement.lineno}" for name in names if name.split(".")[0] in unloadable]
    assert unloadable and found == []
