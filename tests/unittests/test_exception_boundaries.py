# -*- coding: utf-8 -*-
"""Where a C++ exception crosses into Python, and what it arrives as.

pybind11 wraps every bound call in one try/catch and translates what it catches
(`translate_exception` in pybind11/detail/internals.h), so an adapter never needs a try/catch of
its own only to rethrow. These tests cover what that boundary cannot see, and the rule it cannot
apply by itself:

1. A worker thread. An exception that leaves a `std::thread` body calls std::terminate: the
   interpreter aborts before pybind11 is ever reached.
2. A C-API call. pybind11 translates C++ exceptions, not a NULL result. A NULL handed back as a
   py::object becomes pybind11's "Unable to convert function return value" TypeError, and the real
   exception is demoted to its __cause__.
3. Registration. `py::register_exception` installs a translator that every pygim extension shares
   (one pybind11, so one set of internals), and the newest is tried first: one extension's
   translator for `std::runtime_error` would rename every other extension's errors. And
   `py::exception<T>(...)` on its own only makes the class — nothing ever raises it.
4. The type follows the meaning (docs/definition_of_done.md, "What it says when it fails"). A file
   error is the OSError pathlib raises for the same call, a malformed document a ValueError, a
   missing key a KeyError. Today every C++ `std::runtime_error` arrives as a bare RuntimeError, so
   callers catch RuntimeError, and with it RecursionError and NotImplementedError.

Two more were found while probing these, errors that never reach Python at all: reading a
directory, and a JSONL file whose last document is cut short.
"""

import os
import pathlib
import re

import pytest

from pygim import persistence
from pygim.core.explib import GimError
from pygim.factory import Factory
from pygim.ioc import Container
from pygim.pathlike import ext, path
from pygim.persistence import acquire_datastore
from pygim.registry import KeyPolicyKind, Registry

FAST = pathlib.Path(__file__).resolve().parents[2] / "src" / "_pygim_fast"
# CI removes src/ before running the suite against the installed package: the scans of the C++
# sources skip there rather than fail.
needs_source = pytest.mark.skipif(not FAST.is_dir(), reason="source tree not present (testing the installed package)")
NO_DRIVER = "Driver={pygim-no-such-driver};Server=nowhere;"


# ─── Reading the C++ sources ─────────────────────────────────────────────────

# Comments, raw strings, strings and character literals — but not a digit separator (3'600'000).
_NOT_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|R"(?P<d>[^(\s]*)\(.*?\)(?P=d)"|"(?:\\.|[^"\\\n])*"'
                       r"|(?<!\w)'(?:\\.|[^'\\\n])+'", re.S)


def _first_party():
    """Every first-party C++ file as (path, code), walked and read through pygim's own PathSet as
    test_layering's `sources` does. Vendored code is not ours to hold to these rules."""
    found = path(str(FAST)).pathset("**/*", kind="file").filter(ext(".h", ".cpp"))
    members, texts = found.to_list(), found.read_all_files()
    assert len(members) == len(texts), "a member was not a regular file; the two lists no longer align"
    assert len(members) >= 50, f"only {len(members)} C++ files under {FAST}: a rule checked over nothing passes"
    return sorted((pathlib.Path(str(m)), _code(t)) for m, t in zip(members, texts)
                  if "third_party" not in pathlib.Path(str(m)).parts)


def _code(text):
    """*text* with comments and literals blanked to spaces, newlines kept: a brace inside a
    message does not count, and an offset still gives the file's own line number."""
    return _NOT_CODE.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def _where(f, code, offset):
    return f"{f.relative_to(FAST).as_posix()}:{code.count(chr(10), 0, offset) + 1}"


def _statement_end(code, start):
    """The offset of the `;` that ends the statement running through *start*."""
    depth = 0
    for i in range(start, len(code)):
        depth += {"(": 1, ")": -1}.get(code[i], 0)
        if code[i] == ";" and depth == 0:
            return i
    raise AssertionError("unterminated statement")


def _block(code, brace):
    """The text from the `{` at *brace* to its partner."""
    depth = 0
    for i in range(brace, len(code)):
        depth += {"{": 1, "}": -1}.get(code[i], 0)
        if depth == 0:
            return code[brace:i + 1]
    raise AssertionError("unbalanced braces")


# ─── 1. Worker threads ───────────────────────────────────────────────────────

_LAMBDA = r"\[[^\]]*\]\s*(?:\([^)]*\))?\s*(?:mutable\s*)?(?:noexcept\s*)?\{"
_STARTS = r"(?:\.emplace_back|std::j?thread\s*(?:\w+\s*)?)\s*[({]\s*"


def _enclosing_scope(code, at):
    """The innermost class or struct around *at* (its block), or the whole file: where a name a
    thread body calls is that class's member, not a namesake elsewhere in the file."""
    scope = code
    for m in re.finditer(r"\b(?:class|struct)\s+\w+[^;{()]*\{", code):
        if m.start() > at:
            break
        block = _block(code, m.end() - 1)
        if m.end() - 1 + len(block) > at:
            scope = block
    return scope


def _thread_bodies(code):
    """(where, body, scope) of every thread body: a lambda handed to a thread —
    `threads.emplace_back([..](..) {`, `std::thread([..](..) {`, `std::jthread` — or a callable
    handed by name (`own.emplace_back(run, w)`), its lambda found in the scope around it."""
    for m in re.finditer(_STARTS + _LAMBDA, code):
        yield m.start(), _block(code, m.end() - 1), _enclosing_scope(code, m.start())
    for m in re.finditer(_STARTS + r"(\w+)\s*[,)]", code):
        scope = _enclosing_scope(code, m.start())
        named = re.search(rf"\b{m.group(1)}\s*=\s*" + _LAMBDA, scope)
        if named:   # a lambda by name; any other emplace_back(x, ...) is no thread
            yield m.start(), _block(scope, named.end() - 1), scope


def _catches_everything(code, scope, body):
    """True when *body* catches everything itself, or hands its work to a function that does
    (parallel_load.h's workers call `run_worker`, whose whole body is a try): the function of that
    name in *scope*, the class the body is in, or — when the class defines none, as a base class's
    member — in the file."""
    if "catch (...)" in body:
        return True
    for name in set(re.findall(r"(\w+)\s*\(", body)):
        definition = rf"\b{name}\s*\([^;{{}}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{{"
        where = scope if re.search(definition, scope) else code
        for d in re.finditer(definition, where):
            if "catch (...)" in _block(where, d.end() - 1):
                return True
    return False


def test_the_thread_check_finds_a_body_that_lets_go():
    """Run against its own hard cases before it is trusted: a body calling `work()` is judged by
    its own class's `work()`, not a namesake's that catches (task_pool beside ordered_pool), and a
    lambda handed by name (`own.emplace_back(run, w)`) is read through its name."""
    code = _code("""
class keeper { void work() { try { step(); } catch (...) {} } };
class leaker {
    void start() { m_threads.emplace_back([this] { work(); }); }
    void work() { step(); }
};
void loop() {
    auto run = [&](std::size_t w) { step(w); };
    own.emplace_back(run, 1);
    out.emplace_back(value, 2);
}
""")
    found = [code.count(chr(10), 0, at) + 1 for at, body, scope in _thread_bodies(code) if not _catches_everything(code, scope, body)]
    assert found == [4, 9]


@needs_source
def test_every_thread_body_catches_what_it_throws():
    """An exception that leaves a thread body aborts the interpreter (exit 134, seen on
    fetch_benchmark.h with an unknown ODBC driver). Every thread body catches everything — into an
    exception_ptr the caller rethrows after join — itself or through the one function it runs. A
    source scan, because the benchmark extension that aborted is not public API to call."""
    offenders = []
    for f, code in _first_party():
        if "thread" not in code:
            continue
        for at, body, scope in _thread_bodies(code):
            if not _catches_everything(code, scope, body):
                offenders.append(_where(f, code, at))
    assert not offenders, f"a thread body lets an exception escape, which aborts Python: {offenders}"


# ─── 2. C-API results ────────────────────────────────────────────────────────

@pytest.mark.parametrize("error", [ValueError("refused by the subclass"),
                                   KeyError("refused by the subclass"),
                                   PermissionError(13, "refused by the subclass")],
                         ids=["ValueError", "KeyError", "PermissionError"])
def test_an_error_in_a_path_subclass_constructor_keeps_its_type(error):
    """A user's `class Mine(path)` is built through the metaclass's `PyType_Type.tp_call`. When its
    `__init__` raises, tp_call returns NULL; path_bindings.h hands that NULL back as the result, and
    pybind11 replaces the user's exception with a TypeError about converting the return value."""
    class Refusing(path):
        def __init__(self, *args, **kwargs):
            raise error

    with pytest.raises(type(error)) as raised:
        Refusing("a.txt")
    assert raised.value is error


@needs_source
def test_every_stolen_c_api_result_is_checked_before_use():
    """The whole class: a C-API call returns NULL with the Python error set, and only
    `throw py::error_already_set()` hands that error to pybind11. So a stolen result is assigned,
    and the next line is `if (!name) throw py::error_already_set();` — never returned unchecked."""
    stolen = re.compile(r"py::reinterpret_steal\s*<[^>]*>\s*\(\s*(?:Py\w+|[\w.]+\.tp_\w+)\s*\(")
    offenders = []
    for f, code in _first_party():
        lines = code.splitlines()
        for m in stolen.finditer(code):
            named = re.search(r"(\w+)\s*=\s*$", code[code.rfind("\n", 0, m.start()) + 1:m.start()])
            n = code.count("\n", 0, _statement_end(code, m.start()))   # the statement may span lines
            following = next((line for line in lines[n + 1:] if line.strip()), "")
            if not (named and re.search(rf"if\s*\(\s*!\s*{named.group(1)}\s*\)\s*throw\s+py::error_already_set",
                                        following)):
                offenders.append(_where(f, code, m.start()))
    assert not offenders, f"a C-API result that may be NULL is used unchecked: {offenders}"


# ─── 3. Registration ─────────────────────────────────────────────────────────

@needs_source
def test_every_exception_translation_is_registered_local_to_its_module():
    """`py::register_local_exception` is the only registration: a global one reaches every pygim
    extension, and a bare `py::exception<T>(...)` registers nothing."""
    banned = re.compile(r"py::register_exception\s*<|py::exception\s*<[^;]*?>\s*(?:\w+\s*)?[({]")
    offenders = []
    for f, code in _first_party():
        offenders += [f"{_where(f, code, m.start())}: {m.group(0).split('<')[0]}" for m in banned.finditer(code)]
    assert not offenders, f"register exceptions with py::register_local_exception: {offenders}"


def test_a_persistence_failure_is_a_public_persistence_error():
    """A failed persistence operation raises `pygim.persistence.PersistenceError`, a RuntimeError
    (definition_of_done.md: RuntimeError for a failed operation). Read when the test runs, so a
    missing name fails this test only. Before: the module made a GimError class nothing raised."""
    store = acquire_datastore(NO_DRIVER, format="polars")
    with pytest.raises(persistence.PersistenceError, match="SQLDriverConnect") as raised:
        store.load("SELECT 1")
    assert isinstance(raised.value, RuntimeError)


def test_a_persistence_failure_is_one_of_pygims_own_errors():
    """pygim's errors share one root, pygim.core.explib.GimError, whichever language raised them.
    Before: the persistence module made a GimError of its own, so `except GimError` caught nothing
    from C++."""
    store = acquire_datastore(NO_DRIVER, format="polars")
    with pytest.raises(GimError, match="SQLDriverConnect"):
        store.load("SELECT 1")


def test_one_extension_cannot_rename_another_extensions_errors(tmp_path):
    """The guard for the fix above: once persistence translates its errors, a pathlike failure
    must still not be persistence's GimError. A global translator for `std::runtime_error` would
    break it."""
    with pytest.raises(Exception) as pathlike_error:
        path(str(tmp_path / "missing")).read_bytes()
    assert not isinstance(pathlike_error.value, persistence.PersistenceError)


# ─── 4. The type follows the meaning ─────────────────────────────────────────

def test_an_except_clause_naming_the_subclass_catches_it(tmp_path):
    """`except FileNotFoundError` catches pathlike's, worded as os.strerror words it. Set as
    PyErr_SetObject(OSError, args), the error was a FileNotFoundError raised as type OSError: an
    except clause on Python 3.10 and older compares that type, and caught nothing (PR #39's 3.9 and
    3.10 jobs; pytest.raises compares the instance, and passed). And MSVC's words were lowercase."""
    try:
        path(str(tmp_path / "missing")).read_bytes()
    except FileNotFoundError as caught:
        assert caught.strerror == os.strerror(caught.errno) and caught.filename == str(tmp_path / "missing")
    else:
        pytest.fail("nothing raised")


# name: (the call on pathlib, the same call on pygim, the target under the test's folder)
FILE_ERRORS = {
    "read_bytes of a missing file": (lambda p: p.read_bytes(), lambda p: p.read_bytes(), "missing"),
    "read of a missing document": (lambda p: p.read_text(), lambda p: p.read(), "missing.json"),
    "read_bytes of a directory": (lambda p: p.read_bytes(), lambda p: p.read_bytes(), "folder"),
    "write_bytes into a missing folder": (lambda p: p.write_bytes(b"x"), lambda p: p.write_bytes(b"x"), "no/such/file"),
    "write_bytes onto a directory": (lambda p: p.write_bytes(b"x"), lambda p: p.write_bytes(b"x"), "folder"),
    "mkdir of an existing directory": (lambda p: p.mkdir(), lambda p: p.mkdir(), "folder"),
    "mkdir over a file": (lambda p: p.mkdir(), lambda p: p.mkdir(), "file"),
    "mkdir in a missing folder": (lambda p: p.mkdir(), lambda p: p.mkdir(), "no/such/folder"),
}


@pytest.mark.parametrize("case", FILE_ERRORS)
def test_a_file_error_is_the_oserror_pathlib_raises(tmp_path, case):
    """pathlike promises pathlib parity, failures included: the same call on the same file raises
    the same OSError subclass, whatever this platform's pathlib raises (a directory read is
    IsADirectoryError on POSIX and PermissionError on Windows). Today it is a RuntimeError — and a
    directory's read_bytes returns b'' on Linux without any error."""
    on_pathlib, on_pygim, target = FILE_ERRORS[case]
    (tmp_path / "file").write_bytes(b"x")
    (tmp_path / "folder").mkdir()
    with pytest.raises(OSError) as expected:
        on_pathlib(tmp_path / target)
    with pytest.raises(type(expected.value)):
        on_pygim(path(str(tmp_path / target)))


MALFORMED = {"json": ("bad.json", "{nope"),
             "yaml": ("bad.yaml", "a: [1, 2\n"),
             "toml": ("bad.toml", "a = = 1\n"),
             "jsonl": ("bad.jsonl", '{"a": 1}\nnot json\n')}


@pytest.mark.parametrize("engine", MALFORMED)
def test_a_malformed_document_is_a_value_error(tmp_path, engine):
    """A document that does not parse is a bad value, as json, tomllib and Python's own decoders
    say: ValueError, naming the file."""
    name, text = MALFORMED[engine]
    (tmp_path / name).write_text(text, encoding="utf-8")
    with pytest.raises(ValueError, match=re.escape(name)):
        path(str(tmp_path / name)).read()


NOT_UTF8 = {"json": ("latin1.json", b'{"a": "\xe9"}'),
            "yaml": ("latin1.yaml", b"a: \xe9\n"),
            "toml": ("latin1.toml", b'a = "\xe9"\n'),
            "jsonl": ("latin1.jsonl", b'{"a": "\xe9"}\n')}


@pytest.mark.parametrize("engine", NOT_UTF8)
def test_a_document_that_is_not_utf8_is_a_value_error(tmp_path, engine):
    """Undecodable input is what Python's read_text raises UnicodeDecodeError for — a ValueError."""
    name, data = NOT_UTF8[engine]
    (tmp_path / name).write_bytes(data)
    with pytest.raises(ValueError, match=re.escape(name)):
        path(str(tmp_path / name)).read()


@pytest.mark.parametrize("tail", ['{"b": ', '{"b": 2', "[1, 2"], ids=["after a key", "before the brace", "an array"])
def test_a_jsonl_file_whose_last_document_is_cut_short_is_an_error(tmp_path, tail):
    """simdjson's parse_many leaves an incomplete last document out of the stream and reports it
    only through `truncated_bytes()`, which the engine never asks. So a file cut off mid-write reads
    back as its complete rows, silently: today this returns [{"a": 1}]."""
    (tmp_path / "cut.jsonl").write_text('{"a": 1}\n' + tail + "\n", encoding="utf-8")
    with pytest.raises(ValueError, match=r"cut\.jsonl, line 2"):
        path(str(tmp_path / "cut.jsonl")).read()


class _IService:
    pass


MISSING_KEYS = {
    "Container.resolve": lambda: Container().resolve(_IService),
    "Container.describe": lambda: Container().describe(_IService),
    "Registry[key]": lambda: Registry(hooks=False, policy=KeyPolicyKind.qualname)["missing"],
    "Factory[name]": lambda: Factory()["missing"],
    "Factory.create": lambda: Factory().create("missing"),
}


@pytest.mark.parametrize("lookup", MISSING_KEYS)
def test_a_missing_key_is_a_key_error(lookup):
    """A lookup that finds nothing is a KeyError, as a mapping's is — `registry[key]` raising
    RuntimeError breaks every caller written against the mapping protocol."""
    with pytest.raises(KeyError):
        MISSING_KEYS[lookup]()


def test_an_unknown_lifecycle_is_a_value_error():
    """A rejected argument value is a ValueError (definition_of_done.md)."""
    with pytest.raises(ValueError, match="lifecycle"):
        Container().register(_IService, _IService, lifecycle="forever")
