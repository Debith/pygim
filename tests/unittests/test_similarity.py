# -*- coding: utf-8 -*-
"""pygim.similarity: deterministic all-pairs similarity of functions and classes,
one language per index, never across languages."""

import ast
import errno
import itertools
import os
import pathlib
import signal
import subprocess
import sys
import textwrap
import threading
import time
import warnings

import pyarrow as pa
import pyarrow.compute as pc
import pytest
from click.testing import CliRunner
from tabulate import tabulate

from pygim import similarity
from pygim.__main__ import cli
from stub_parity import Stub

try:
    import polars as pl
except ImportError:   # a sanitizer run makes polars unloadable (tests/static/run_tests_under_sanitizers.py)
    pl = None

PY = textwrap.dedent('''
    import os

    class A(Base):
        """A docstring never counts."""
        X = 1

        def m(self, a):
            return a + 1

        def n(self, q):
            t = q * 2
            return os.path.join(t, "x")

    class B(Base):
        X = 1

        def n(self, r):
            u = r * 2
            return os.path.join(u, "y")

        def m(self, z):
            """Neither does this one."""
            return z + 1

    def f(a):
        def inner(k):
            return k
        return a + 1

    def g(a):
        return a + n
''')

CPP = textwrap.dedent('''
    #include <cstdlib>
    namespace geo {
    struct Point {
        int x = 0;
        int norm1() const { return abs(x) + abs(y); }
    };
    int twice(int v) { return v * 2; }
    int doubled(int w) { return w * 2; }
    long widened(long v) { return v * 2; }
    }
''')


def _index(language, source, origin="m"):
    ix = similarity.Index(language)
    ix.add(source, origin=origin)
    return ix


def _pairs(ix, *args, **kwargs):
    """The pairs table as rows (dicts), for asserting on."""
    return ix.pairs(*args, **kwargs).to_pylist()


def _names(ix, kind=None):
    return ix.units(kind).column("qualname").to_pylist()


def _scores(ix, kind="function"):
    return {(p["a_qualname"], p["b_qualname"]): p for p in _pairs(ix, kind)}


def _in_sweep_order(ix, kind, table):
    """`table`'s pairs in the order the sweep visits them: rows lightest first (ties by id), each
    row's partners after it in that order."""
    units = ix.units(kind).sort_by([("weight", "ascending"), ("id", "ascending")])
    place = [0] * (max(units["id"].to_pylist(), default=0) + 1)   # each unit's row in the sweep
    for row, unit in enumerate(units["id"].to_pylist()):
        place[unit] = row
    at_a, at_b = pc.take(pa.array(place), table["a"]), pc.take(pa.array(place), table["b"])
    keyed = table.append_column("_row", pc.min_element_wise(at_a, at_b)).append_column("_col", pc.max_element_wise(at_a, at_b))
    return keyed.sort_by([("_row", "ascending"), ("_col", "ascending")]).drop_columns(["_row", "_col"])


def _classes(n):
    """n classes of one to twelve methods each, so a class row's cost varies."""
    def cls(i):
        methods = "".join(f"    def m{k}(self, a):\n        return a * {(i + k) % 5} + {k % 3}\n" for k in range(1 + i % 12))
        return f"class C{i}:\n    x = {i % 4}\n{methods}"
    return _index("python", "\n".join(cls(i) for i in range(n)))


class TestLanguages:
    def test_registry_lists_both_languages(self):
        assert set(similarity.LANGUAGES) >= {"python", "cpp"}
        assert ".py" in similarity.LANGUAGES["python"]["exts"]
        assert ".hpp" in similarity.LANGUAGES["cpp"]["exts"]

    def test_no_extension_belongs_to_two_languages(self):
        exts = [e for rec in similarity.LANGUAGES.values() for e in rec["exts"]]
        assert len(exts) == len(set(exts))

    @pytest.mark.parametrize("path, language", [
        ("a.py", "python"), ("pkg/b.pyi", "python"), ("x.CPP", "cpp"), ("y/z.h", "cpp"),
        ("c.cc", "cpp"), ("README.md", None), ("Makefile", None), (".py", None),
    ])
    def test_language_of(self, path, language):
        assert similarity.language_of(path) == language

    def test_language_of_accepts_pathlike(self, tmp_path):
        assert similarity.language_of(tmp_path / "m.py") == "python"

    def test_unknown_language_names_the_known_ones(self):
        with pytest.raises(ValueError, match=r"unknown language 'rust' \(known: .*cpp.*python"):
            similarity.Index("rust")


class TestStub:
    """src/pygim/similarity.pyi is what an editor and a type checker see of the module: every name
    it exports, and each Index method with the binding's parameters — names, order, keyword-only
    marks and defaults. Found 2026-10-29: add_files(), the bulk API the CLI uses, was not in it."""

    STUB = Stub(similarity)

    def test_it_names_everything_the_module_exports(self):
        assert set(similarity.__all__) <= self.STUB.names()

    def test_every_method_of_the_index_is_stubbed(self):
        bound = {n for n in vars(similarity.Index) if not n.startswith("_")} | {"__init__", "__len__", "__repr__"}
        assert sorted(bound - set(self.STUB.methods("Index"))) == []

    @pytest.mark.parametrize("name", sorted({n for n in vars(similarity.Index) if not n.startswith("_") and n != "language"} | {"__init__"}))
    def test_each_method_takes_the_bindings_parameters(self, name):
        assert Stub.params(self.STUB.methods("Index")[name]) == Stub.binding_params(getattr(similarity.Index, name))

    @pytest.mark.parametrize("name", sorted(set(vars(similarity.Index)) & {"__init__", "__len__", "__repr__", "language", "add", "add_file",
                                                                            "add_files", "units", "pairs", "stream_pairs", "stats"}))
    def test_each_docstring_is_the_bindings(self, name):
        """help() shows the binding's text and an editor the stub's: one text. A difference fails
        with the binding's, to paste into the stub."""
        bound = Stub.binding_doc(getattr(similarity.Index, name))
        assert Stub.same_text(ast.get_docstring(self.STUB.methods("Index")[name], clean=True), bound), bound

    def test_the_module_level_docstrings_are_the_bindings(self):
        assert Stub.same_text(ast.get_docstring(self.STUB.cls("Index")), similarity.Index.__doc__)
        assert Stub.same_text(ast.get_docstring(self.STUB.function("language_of")), Stub.binding_doc(similarity.language_of))


class TestAllPairs:
    def test_every_function_is_compared_with_every_other(self):
        ix = _index("python", PY)
        functions = _names(ix, "function")
        assert functions == ["A.m", "A.n", "B.n", "B.m", "f", "f.<locals>.inner", "g"]
        pairs = _pairs(ix, "function")
        assert len(pairs) == len(functions) * (len(functions) - 1) // 2
        order = {q: i for i, q in enumerate(functions)}
        assert {(p["a_qualname"], p["b_qualname"]) for p in pairs} == set(itertools.combinations(functions, 2))
        assert all(order[p["a_qualname"]] < order[p["b_qualname"]] for p in pairs)
        assert all(p["a"] < p["b"] for p in pairs)

    def test_cpp_every_function_is_compared(self):
        ix = _index("cpp", CPP)
        assert _names(ix, "function") == ["geo::Point::norm1", "geo::twice", "geo::doubled", "geo::widened"]
        assert ix.pairs("function").num_rows == 6

    def test_best_first(self):
        scores = _index("python", PY).pairs("function").column("score").to_pylist()
        assert scores == sorted(scores, reverse=True)

    def test_threshold_filters(self):
        ix = _index("python", PY)
        exact = ix.pairs("function", threshold=1.0).select(["a_qualname", "b_qualname"]).to_pylist()
        assert exact == [{"a_qualname": "A.m", "b_qualname": "B.m"}, {"a_qualname": "A.n", "b_qualname": "B.n"}]
        assert all(s >= 0.5 for s in ix.pairs("function", threshold=0.5).column("score").to_pylist())

    @pytest.mark.parametrize("bad", [-0.1, 1.5, float("nan")])
    def test_threshold_out_of_range(self, bad):
        with pytest.raises(ValueError, match="threshold"):
            _index("python", PY).pairs("function", threshold=bad)

    def test_kind_is_validated(self):
        with pytest.raises(ValueError, match="kind must be 'function' or 'class', got 'method'"):
            _index("python", PY).pairs("method")

    def test_score_is_the_exact_ratio(self):
        for p in _pairs(_index("python", PY), "function"):
            assert p["score"] == 2 * p["shared"] / p["total"]
            assert p["exact"] == (2 * p["shared"] == p["total"])
            assert 0.0 <= p["score"] <= 1.0


    def test_min_size_leaves_out_small_units(self):
        ix = _index("python", PY)
        names = set(_names(ix, "function"))
        rows = _pairs(ix, "function", 0.0, 1)
        assert {p["a_qualname"] for p in rows} | {p["b_qualname"] for p in rows} == names
        assert ix.pairs("function", 0.0, 10_000).num_rows == 0
        counts = [ix.pairs("function", 0.0, m).num_rows for m in range(0, 200, 5)]
        assert counts == sorted(counts, reverse=True)              # a larger floor never adds a pair
        assert any(0 < c < counts[0] for c in counts)              # inner (tiny) drops out before A.n

    def test_threaded_sweep_is_deterministic(self):
        """Above 256 units the sweep runs on every core; the table never changes."""
        src = "\n".join(f"def f{i}(a{i % 7}, b):\n    return a{i % 7} * {i % 3} + b{'' if i % 5 else ' + n'}\n"
                        for i in range(400))
        first = _index("python", src).pairs("function", 0.0)
        assert first.num_rows == 400 * 399 // 2
        for _ in range(3):
            assert _index("python", src).pairs("function", 0.0).equals(first)


class TestNormalisation:
    def test_local_names_and_docstrings_never_count(self):
        s = _scores(_index("python", PY))
        assert s[("A.m", "B.m")]["exact"]          # renamed parameter, docstring on one side

    def test_string_values_never_count_only_their_type(self):
        assert _scores(_index("python", PY))[("A.n", "B.n")]["exact"]   # "x" vs "y"

    def test_a_global_counts(self):
        s = _scores(_index("python", PY))
        assert not s[("f", "g")]["exact"]          # f also defines inner
        ix = _index("python", "def p(a):\n    return a + 1\ndef q(a):\n    return a + n\n")
        (pair,) = _pairs(ix, "function")
        assert 0.0 < pair["score"] < 1.0

    @pytest.mark.parametrize("language, source", [
        ("python", "def p(a: int) -> int:\n    return a\ndef q(b):\n    return b\n"),
        ("cpp", "int a(int x) { return x+1; }\n"
                "int b(int y)\n{\n    // add one\n    return y + 1; /* done */\n}\n"),
    ], ids=["python annotations", "cpp comments and formatting"])
    def test_what_never_counts(self, language, source):
        assert _pairs(_index(language, source), "function")[0]["exact"]

    def test_cpp_parameter_name_never_counts_its_type_does(self):
        s = _scores(_index("cpp", CPP))
        assert s[("geo::twice", "geo::doubled")]["exact"]
        assert not s[("geo::twice", "geo::widened")]["exact"]
        assert s[("geo::twice", "geo::widened")]["score"] >= 0.5   # 2 of 4 counted subtrees differ

    # Two C/C++ functions that differ in one name: whether it is a local (renamed, never counts) or
    # a global (counts) — the reader binds a name only where it is declared. Found 2026-10-29: a
    # name after `>`, `&`, `&&` or `not` in a condition was taken as declared, so two functions
    # comparing with different globals were exact copies; a local after `>>` was taken as a global.
    @pytest.mark.parametrize("one, other, exact", [
        ("if (n > limit) return 1; return 0;", "if (n > bound) return 1; return 0;", False),
        ("if (n && done) return 1; return 0;", "if (n && over) return 1; return 0;", False),
        ("return (n & mask) != 0;", "return (n & bits) != 0;", False),
        ("if (not done) return 1; return 0;", "if (not over) return 1; return 0;", False),
        ("return g(n & mask);", "return g(n & bits);", False),
        ("std::map<K, std::vector<V>> m; return h(m);", "std::map<K, std::vector<V>> q; return h(q);", True),
        ("const std::vector<int>& v = get(n); return v.size();", "const std::vector<int>& w = get(n); return w.size();", True),
        ("for (int i = 0; i < n; ++i) s += i; return s;", "for (int k = 0; k < n; ++k) s += k; return s;", True),
    ], ids=["greater than", "and", "bit and", "not", "call argument", "nested template", "reference", "for init"])
    def test_cpp_a_name_binds_only_where_it_is_declared(self, one, other, exact):
        ix = _index("cpp", f"int f(int n) {{ {one} }}\nint g2(int n) {{ {other} }}\n")
        assert _pairs(ix, "function")[0]["exact"] is exact

    def test_cpp_a_contextual_keyword_is_a_name(self):
        """`module`, `import`, `final` and `override` are keywords only in their own places: as a
        parameter (121 C files of CPython call one `module`) it is renamed as any other."""
        ix = _index("cpp", "int f(PyObject *module) { return g(module); }\nint h(PyObject *m) { return g(m); }\n")
        assert _pairs(ix, "function")[0]["exact"]

    NEAR = textwrap.dedent('''
        def load(path, encoding="utf-8"):
            result = {}
            with open(path, encoding=encoding) as f:
                for number, line in enumerate(f, 1):
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    key, sep, value = line.partition("=")
                    if not sep:
                        raise ValueError(f"{path}:{number}: no '=' in {line!r}")
                    result[key.strip()] = value.strip()
            return result
    ''')

    @pytest.mark.parametrize("edit", [
        ("line.startswith", "line.endswith"),                  # rename an attribute
        ("if not sep:", "if sep:"),                            # drop an operator
        ("    return result", "    log(result)\n    return result"),             # add a statement
    ])
    def test_one_edit_stays_a_near_duplicate(self, edit):
        """One small edit costs only the subtrees around it: a near-duplicate scores high."""
        edited = self.NEAR.replace(*edit)
        assert edited != self.NEAR                             # the edit applies
        ix = similarity.Index("python")
        ix.add(self.NEAR)
        ix.add(edited)
        ix.add("def other(xs):\n    return sorted(set(xs), key=len)[:3]\n")
        rows = {(r["a"], r["b"]): r["score"] for r in ix.pairs(columns=["a", "b", "score"]).to_pylist()}
        assert rows[(0, 1)] >= 0.8
        assert rows[(0, 2)] < 0.2 and rows[(1, 2)] < 0.2


class TestClasses:
    def test_methods_are_paired_by_best_assignment(self):
        (pair,) = _pairs(_index("python", PY), "class")
        assert (pair["a_qualname"], pair["b_qualname"], pair["exact"]) == ("A", "B", True)   # methods in the other order

    def test_an_unmatched_method_counts_against(self):
        src = PY + "\nclass C(Base):\n    X = 1\n    def m(self, a):\n        return a + 1\n"
        s = _scores(_index("python", src), "class")
        assert s[("A", "B")]["exact"]
        assert 0.0 < s[("A", "C")]["score"] < 1.0

    def test_a_method_names_its_class_as_owner(self):
        units = _index("python", PY).units().to_pylist()
        ids = {u["qualname"]: u["id"] for u in units}
        owners = {u["qualname"]: u["owner"] for u in units}
        assert owners["A.m"] == owners["A.n"] == ids["A"]
        assert owners["B.n"] == owners["B.m"] == ids["B"]
        assert owners["f"] is None and owners["f.<locals>.inner"] is None and owners["A"] is None

    def test_a_method_under_a_statement_is_counted_once(self):
        """A method defined under `if` in a class body is a method of the class, as one at the top
        of the body is, and is no more in the class's own tree. Found 2026-10-29: it was both
        (subprocess.Popen's _get_handles, under `if _mswindows:`)."""
        ix = _index("python", "class A:\n    if X:\n        def m(self):\n            return 1\n\n"
                              "class B:\n    if X:\n        def m(self):\n            return 2 + 3 * 4 - g(5)\n")
        rows = {u["qualname"]: u for u in ix.units().to_pylist()}
        own = lambda c: rows[c]["nodes"] - rows[c + ".m"]["nodes"]   # the class's own tree: its methods' left out
        assert rows["A.m"]["owner"] == rows["A"]["id"] and own("A") == own("B")

    def test_cpp_class_and_methods(self):
        units = _index("cpp", CPP, origin="geo.h").units().to_pylist()
        cls, method = units[0], units[1]
        assert {k: cls[k] for k in ("id", "qualname", "kind", "origin", "line", "owner")} == {
            "id": 0, "qualname": "geo::Point", "kind": "class", "origin": "geo.h", "line": 4, "owner": None}
        assert (method["qualname"], method["owner"]) == ("geo::Point::norm1", 0)


class TestCppStructure:
    """What the C/C++ reader takes for a unit, a scope and a line, on what the review found it
    misread (2026-10-29)."""

    @pytest.mark.parametrize("source, names", [
        ("struct alignas(16) S { int f() { return 1; } };", ["S", "S::f"]),
        ("namespace a { inline namespace v1 { int f() { return 1; } } }", ["a::v1::f"]),
        ("class LLVM_ABI Foo { int m() { return 1; } };", ["Foo", "Foo::m"]),
        ("class Foo final : public Base { int m() { return 1; } };", ["Foo", "Foo::m"]),
        ("#if A\nint f() { return 1; }\n#elifdef B\nint f() { return 2; }\n#endif\n", ["f"]),
        ("#if A\nint f() { return 1; }\n#elifndef B\nint f() { return 2; }\n#endif\n", ["f"]),
        ("// a note \\\nint g(int x) { return x; }\nint f() { return 1; }\n", ["f"]),
    ], ids=["alignas", "inline namespace", "a macro before the name", "final", "elifdef", "elifndef", "continued comment"])
    def test_units(self, source, names):
        assert _names(_index("cpp", source)) == names

    def test_a_string_continued_by_backslash_crlf(self):
        """A CRLF checkout splices lines as an LF one does: `\\` before `\r\n` continues a string."""
        ix = _index("cpp", 'const char* s = "a\\\r\nb";\r\nint f() { return 1; }\r\n')
        assert [(u["qualname"], u["line"]) for u in ix.units().to_pylist()] == [("f", 3)]


class TestLanguagesNeverMeet:
    def test_add_file_refuses_another_language(self, tmp_path):
        cpp = tmp_path / "a.cpp"
        cpp.write_text(CPP)
        ix = similarity.Index("python")
        with pytest.raises(ValueError, match=r"a\.cpp' is a cpp file; this index holds python .* never compares across"):
            ix.add_file(cpp)
        assert len(ix) == 0

    def test_add_file_refuses_unknown_extension(self, tmp_path):
        txt = tmp_path / "notes.txt"
        txt.write_text("x")
        with pytest.raises(ValueError, match="not a file of a known language"):
            similarity.Index("cpp").add_file(txt)

    def test_two_indexes_have_no_operation_between_them(self):
        public = {n for n in dir(similarity.Index) if not n.startswith("_")}
        assert public == {"language", "add", "add_file", "add_files", "units", "pairs", "stream_pairs", "stats"}
        operators = ("__add__", "__sub__", "__or__", "__and__", "__xor__", "__lt__", "__le__", "__gt__", "__ge__", "__contains__")
        defined = {op for cls in similarity.Index.__mro__ if cls is not object for op in vars(cls)}   # not type's: `Index | None` is typing
        assert sorted(defined & set(operators)) == []



class TestFiles:
    def test_add_file_reads_and_records_origin(self, tmp_path):
        a = tmp_path / "a.py"
        a.write_text("def f(a):\n    return a\n")
        b = tmp_path / "b.py"
        b.write_text("def g(b):\n    return b\n")
        ix = similarity.Index("python")
        ix.add_file(a)
        ix.add_file(str(b))
        (pair,) = _pairs(ix)
        assert (pair["a_origin"], pair["a_line"], pair["b_origin"], pair["b_line"]) == (str(a), 1, str(b), 1)

    def test_coding_cookie_is_honoured(self, tmp_path):
        p = tmp_path / "latin.py"
        p.write_bytes("# -*- coding: latin-1 -*-\ndef f():\n    return 'é'\n".encode("latin-1"))
        ix = similarity.Index("python")
        ix.add_file(p)
        assert _names(ix) == ["f"]

    def test_missing_file(self, tmp_path):
        with pytest.raises(FileNotFoundError):
            similarity.Index("python").add_file(tmp_path / "nope.py")

    def test_a_directory_is_an_is_a_directory_error(self, tmp_path):
        (tmp_path / "pkg.py").mkdir()
        with pytest.raises(IsADirectoryError):
            similarity.Index("python").add_file(tmp_path / "pkg.py")

    @pytest.mark.skipif(not hasattr(os, "mkfifo"), reason="FIFOs are POSIX")
    def test_a_fifo_is_refused_at_once_not_waited_on(self, tmp_path):
        """The file is read in C++ (pathlike's file_reads.h: opened non-blocking, fstat'ed): a FIFO
        nobody writes is an OSError at once, where Python's open() would wait for a writer forever."""
        fifo = tmp_path / "pipe.py"
        os.mkfifo(fifo)
        outcome = []
        def read():
            try:
                similarity.Index("python").add_file(fifo)
                outcome.append("added")
            except OSError as e:
                outcome.append(type(e).__name__)
        worker = threading.Thread(target=read, daemon=True)
        worker.start()
        worker.join(timeout=5)
        assert outcome == ["OSError"]

    def test_a_file_reads_with_another_thread_running_python(self, tmp_path):
        """The read and the parse let the GIL go: a Python thread gets it while add_file() runs,
        which it could not if the call held it from start to end. (Parsed under the GIL, through
        ast, this file stopped every other thread for about 0.3 s.)"""
        big = tmp_path / "big.py"
        big.write_text("".join(f"def f{i}(a, b):\n    return [x * {i} for x in a if x > b]\n" for i in range(20_000)))
        stamps, stop = [], threading.Event()
        def count():
            while not stop.is_set():
                stamps.append(time.perf_counter())
                time.sleep(0.001)   # lets the GIL go, and asks for it back
        counter = threading.Thread(target=count)
        counter.start()
        try:
            ix = similarity.Index("python")
            start = time.perf_counter()
            ix.add_file(big)
            end = time.perf_counter()
        finally:
            stop.set()
            counter.join()
        assert len(ix) == 20_000
        assert any(start < t < end for t in stamps), f"no Python ran in {1000 * (end - start):.0f} ms of add_file"

    # (language, source, origin, the ValueError's message)
    @pytest.mark.parametrize("language, source, origin, message", [
        ("python", "x = 1\nint f(int a) { return a; }\n", "snippet.py", r"^snippet\.py:2: "),
        ("cpp", "int x;\nvoid f() {\n", "broken.cpp", r"^broken\.cpp:2: unclosed '\{'"),
    ], ids=["cpp text in a python index is a python syntax error", "cpp unbalanced braces name file and line"])
    def test_a_source_its_frontend_cannot_read_names_origin_and_line(self, language, source, origin, message):
        with pytest.raises(ValueError, match=message):
            similarity.Index(language).add(source, origin=origin)

    def test_a_file_that_is_not_utf8_names_the_line_of_its_bad_byte(self, tmp_path):
        """CPython names the line of the first byte that is not UTF-8; the source checks found it
        before any line was indexed, and said line 1 (2026-10-27). In code, not a comment: 3.14
        refuses one in a comment too, as pygim does, where 3.12 does not."""
        f = tmp_path / "latin.py"
        f.write_bytes(b"x = 1\r\ny = 2\ncaf\xe9 = 3\n")
        with pytest.raises(SyntaxError) as cpython:
            compile(f.read_bytes(), str(f), "exec")
        with pytest.raises(ValueError, match=rf"latin\.py:{cpython.value.lineno}: "):
            similarity.Index("python").add_file(str(f))

    # Every C++ construct that can span lines, each spanning two line breaks.
    SPANNING = {
        "string spliced": 'const char *s = "a\\\nb\\\nc";\n',
        "raw string": 'const char *s = R"(a\nb\nc)";\n',
        "block comment": "/* a\nb\nc */\n",
        "directive continued": "#define M(x) \\\n  (x) \\\n  + 1\n",
        "directive continued, quote characters": "#define Q(o) o = '\"'; \\\n  o = '\"'; \\\n  o = 0\n",
        "line comment continued": "// a \\\nb \\\nc\n",
    }

    @pytest.mark.parametrize("before", SPANNING.values(), ids=SPANNING.keys())
    def test_cpp_line_after_a_construct_spanning_lines(self, before):
        (unit,) = _index("cpp", before + "int f(void) { return 1; }\n").units().to_pylist()
        assert unit["line"] == 4


class TestAddFiles:
    """add_files(): many files read, parsed and fingerprinted on worker threads, added in the order given."""

    @pytest.fixture()
    def tree(self, tmp_path):
        files = []
        for i in range(60):
            p = tmp_path / f"m{i:02d}.py"
            p.write_text(f"def f{i}(a, b):\n    return a * {i} + b\n\nclass C{i}:\n    def m(self):\n        return self.f{i}()\n")
            files.append(p)
        (tmp_path / "bad.py").write_text("def (:\n")
        return files

    @pytest.mark.parametrize("threads", [None, 1, 4])
    def test_the_units_are_what_add_file_in_a_loop_gives(self, tree, threads):
        one = similarity.Index("python")
        for f in tree:
            one.add_file(f)
        many = similarity.Index("python")
        assert many.add_files(tree, threads=threads) == []
        assert many.units().equals(one.units())
        assert many.pairs(threshold=0.5).equals(one.pairs(threshold=0.5))

    def test_a_file_that_is_not_python_is_left_out_and_named(self, tree, tmp_path):
        ix = similarity.Index("python")
        (rejected,) = ix.add_files([tree[0], tmp_path / "bad.py", tree[1]])
        assert rejected[0] == str(tmp_path / "bad.py") and rejected[1].startswith(f"{tmp_path / 'bad.py'}:1: ")
        assert [u["qualname"] for u in ix.units().to_pylist()] == ["f0", "C0", "C0.m", "f1", "C1", "C1.m"]

    @pytest.mark.parametrize("make, why", [
        (lambda p: None, os.strerror(errno.ENOENT)),
        (lambda p: p.mkdir(), os.strerror(errno.EISDIR)),
        (lambda p: os.mkfifo(p), "not a regular file"),
        (lambda p: (p.write_text("def f(a):\n    return a\n"), p.chmod(0)), os.strerror(errno.EACCES)),
    ], ids=["missing", "a directory", "a fifo", "unreadable"])
    def test_a_path_the_os_will_not_read_is_left_out_and_named_with_why(self, tmp_path, make, why):
        """The message names the file and says why in the OS's words, as `pygim similar` prints it
        (`skipped: <message>`). Found 2026-10-29: a missing file and a directory both said "not a
        regular file", and a refused read only "Permission denied", naming no file."""
        if why == os.strerror(errno.EACCES) and os.geteuid() == 0:
            pytest.skip("root reads every file")
        p = tmp_path / "x.py"
        make(p)
        assert similarity.Index("python").add_files([p]) == [(str(p), f"{p}: {why}")]

    def test_a_file_of_another_language_is_refused_before_anything_is_read(self, tree, tmp_path):
        cpp = tmp_path / "a.cpp"
        cpp.write_text("int f() { return 1; }\n")
        ix = similarity.Index("python")
        with pytest.raises(ValueError, match="is a cpp file"):
            ix.add_files([tree[0], cpp])
        assert len(ix) == 0

    def test_threads_zero_is_a_value_error(self, tree):
        with pytest.raises(ValueError):
            similarity.Index("python").add_files(tree, threads=0)

    CHILD = r"""
import os, pathlib, signal, sys, threading, time
from pygim import similarity
root = pathlib.Path(sys.argv[1])
body = "".join(f"def f{i}(a, b=1, *c, **d):\n    return [x * {i} for x in a if x]\n" for i in range(150))
files = []
for d in range(800):
    p = root / f"m{d}.py"
    p.write_text(body)
    files.append(p)
tasks = lambda: len(os.listdir("/proc/self/task")) if os.path.isdir("/proc/self/task") else 0
t = time.perf_counter()
similarity.Index("python").add_files(files)
full = time.perf_counter() - t
before = tasks()
sys.setswitchinterval(1.0)   # the helper gets the GIL only once add_files has let it go
go = threading.Event()
helper = threading.Thread(target=lambda: (go.wait(), os.kill(os.getpid(), signal.SIGINT)))
helper.start()
go.set()
ix = similarity.Index("python")
t = time.perf_counter()
try:
    ix.add_files(files)
    print("completed")
except KeyboardInterrupt:
    print("KeyboardInterrupt")
took = time.perf_counter() - t
helper.join()
print("stopped early", took < full / 2, f"({took * 1000:.1f} of {full * 1000:.1f} ms)")
print("nothing added", len(ix) == 0)
print("threads left", tasks() - before)
"""

    @pytest.mark.skipif(sys.platform == "win32", reason="sends a POSIX signal to a child process")
    def test_ctrl_c_stops_the_reading_and_adds_nothing(self, tmp_path):
        done = subprocess.run([sys.executable, "-c", self.CHILD, str(tmp_path)], capture_output=True, text=True, timeout=120)
        lines = done.stdout.splitlines()
        assert lines and lines[0] == "KeyboardInterrupt", done.stdout + done.stderr
        assert "stopped early True" in done.stdout and "nothing added True" in done.stdout, done.stdout
        if os.path.isdir("/proc/self/task"):
            assert "threads left 0" in done.stdout, done.stdout

    def test_cpp_files_too(self, tmp_path):
        files = []
        for i in range(10):
            p = tmp_path / f"u{i}.cpp"
            p.write_text(f"int twice{i}(int v) {{ return v * 2 + {i}; }}\n")
            files.append(p)
        ix = similarity.Index("cpp")
        assert ix.add_files(files) == []
        assert len(ix) == 10


class TestDeterminism:
    def test_same_input_same_output(self):
        assert _index("python", PY).pairs().equals(_index("python", PY).pairs())

    def test_across_processes(self):
        """No salted hash anywhere: a fresh interpreter (another PYTHONHASHSEED) agrees."""
        code = ("from pygim import similarity; import sys; ix = similarity.Index('python'); "
                "ix.add(sys.stdin.read()); print([(p['a'], p['b'], p['shared'], p['total']) for p in ix.pairs().to_pylist()])")
        outs = {
            subprocess.run([sys.executable, "-c", code], input=PY, capture_output=True, text=True, check=True,
                           env={**os.environ, "PYTHONHASHSEED": seed}).stdout
            for seed in ("1", "2")
        }
        assert len(outs) == 1
        assert str([(p["a"], p["b"], p["shared"], p["total"]) for p in _pairs(_index("python", PY))]) + "\n" in outs


class TestArrow:
    def test_pairs_schema(self):
        t = _index("python", PY).pairs()
        assert isinstance(t, pa.Table)
        dict_str = pa.dictionary(pa.int32(), pa.string())
        assert t.schema == pa.schema([
            pa.field("a", pa.uint32(), False), pa.field("b", pa.uint32(), False),
            pa.field("a_qualname", dict_str, False), pa.field("a_origin", dict_str, False),
            pa.field("a_line", pa.uint32(), False),
            pa.field("b_qualname", dict_str, False), pa.field("b_origin", dict_str, False),
            pa.field("b_line", pa.uint32(), False),
            pa.field("score", pa.float64(), False), pa.field("shared", pa.uint64(), False),
            pa.field("total", pa.uint64(), False), pa.field("exact", pa.bool_(), False),
        ])

    def test_units_schema(self):
        t = _index("python", PY).units()
        dict_str = pa.dictionary(pa.int32(), pa.string())
        assert t.schema == pa.schema([
            pa.field("id", pa.uint32(), False), pa.field("kind", dict_str, False),
            pa.field("qualname", pa.string(), False), pa.field("origin", dict_str, False),
            pa.field("line", pa.uint32(), False), pa.field("owner", pa.uint32(), True),
            pa.field("nodes", pa.uint32(), False), pa.field("weight", pa.uint64(), False),
        ])
        assert t.column("id").to_pylist() == list(range(t.num_rows))

    def test_pair_ids_join_units(self):
        ix = _index("python", PY)
        joined = ix.pairs().join(ix.units().select(["id", "qualname"]), keys="a", right_keys="id")
        for row in joined.to_pylist():
            assert row["qualname"] == row["a_qualname"]

    def test_origins_are_one_dictionary(self):
        ix = similarity.Index("python")
        for name in ("x.py", "y.py", "x.py"):
            ix.add("def f(a):\n    return a\n", origin=name)
        t = ix.pairs()
        assert t.num_rows == 3
        assert t.column("a_origin").chunk(0).dictionary.to_pylist() == ["x.py", "y.py"]   # each file once
        assert t.column("b_origin").chunk(0).dictionary.equals(t.column("a_origin").chunk(0).dictionary)

    @pytest.mark.skipif(pl is None, reason="polars cannot load here: a sanitizer run")
    def test_polars_reads_it_without_copying_rows(self):
        df = pl.from_arrow(_index("python", PY).pairs())
        assert df.height == 21
        assert df.schema["a_origin"] == pl.Categorical and df.schema["a_qualname"] == pl.Categorical
        assert df.filter(pl.col("exact")).select("a_qualname", "b_qualname").rows() == [("A.m", "B.m"), ("A.n", "B.n")]

    def test_empty_results_keep_their_schema(self):
        ix = similarity.Index("cpp")
        assert ix.pairs().num_rows == 0 and ix.units().num_rows == 0
        assert ix.pairs().schema.names[:3] == ["a", "b", "a_qualname"]


class TestLargeResults:
    """Ranked tables in batches, top-N with pruning, and unranked streams."""

    SRC = "\n".join(f"def f{i}(a{i % 7}, b):\n    return a{i % 7} * {i % 3} + b{'' if i % 5 else ' + n'}\n"
                    for i in range(300))

    def test_pairs_come_in_batches_of_batch_size(self):
        ix = _index("python", self.SRC)
        whole = ix.pairs()
        cut = ix.pairs(batch_size=1000)
        assert [b.num_rows for b in cut.to_batches()] == [1000] * 44 + [850]   # 300 * 299 / 2 = 44,850
        assert cut.equals(whole)

    @pytest.mark.parametrize("limit", [1, 7, 100, 44_850, 50_000])
    def test_limit_is_the_head_of_the_ranking(self, limit):
        ix = _index("python", self.SRC)
        whole = ix.pairs()
        assert ix.pairs(limit=limit).equals(whole.slice(0, limit))
        assert ix.pairs(threshold=0.5, limit=limit).equals(ix.pairs(threshold=0.5).slice(0, limit))

    @pytest.mark.parametrize("limit", [1, 50, 1000, 44_850])
    def test_a_class_limit_is_the_head_of_the_class_ranking(self, limit):
        """Class rows with a limit are counted from indexes, as without one, and claimed lightest
        first on every worker (300 classes, so more than one): still the full ranking's head."""
        ix = _classes(300)
        whole = ix.pairs(kind="class")
        assert ix.pairs(kind="class", limit=limit).equals(whole.slice(0, limit))
        assert ix.pairs(kind="class", threshold=0.6, limit=limit).equals(ix.pairs(kind="class", threshold=0.6).slice(0, limit))
        assert ix.pairs(kind="class", min_size=60, limit=limit).equals(ix.pairs(kind="class", min_size=60).slice(0, limit))

    def test_a_limit_between_the_ladders_rungs_is_the_head_of_the_ranking(self):
        """From 4,096 functions a limit goes down the filter's thresholds (1.0, 0.95, ...): one
        between two of them, 0.85, stops at its own and keeps the ranking's head at 0.85."""
        ix = _index("python", "\n".join(f"def f{i}(a, b):\n    return a * {i % 11} + b{i % 13}" + " - a" * (i % 5) + "\n" for i in range(4200)))
        assert ix.pairs(threshold=0.85, limit=30).equals(ix.pairs(threshold=0.85).slice(0, 30))

    LIMIT_CHILD = """
import sys, threading, time
from pygim import similarity
from pygim.utils import rss_bytes
ix = similarity.Index("python")
ix.add("\\n".join(f"def f{i}(a):\\n    return a + {i % 3}\\n" for i in range(5000)))   # one tree, 12.5M pairs at 1.0
base, top, done = rss_bytes(), [0], threading.Event()
def sample():
    while not done.is_set():
        top[0] = max(top[0], rss_bytes())
        time.sleep(0.001)
sampler = threading.Thread(target=sample)
sampler.start()
t = ix.pairs(limit=1000)
done.set()
sampler.join()
print(t.num_rows, t["score"].to_pylist()[-1], (max(top[0], rss_bytes()) - base) >> 20)
"""

    def test_a_limit_holds_only_its_best_pairs(self):
        """pairs(limit=N) of 4,096 functions or more tries thresholds from 1.0 down until one finds N
        pairs. Every pair found was kept until the end — 12.5M of 5,000 identical functions, 400 MiB
        and their sort, to return 1,000 (3.2 GiB for 12,000; 2026-10-27). Each worker keeps its best N."""
        done = subprocess.run([sys.executable, "-c", self.LIMIT_CHILD], capture_output=True, text=True, timeout=300)
        rows, worst, peak_mib = done.stdout.split()
        assert (rows, worst) == ("1000", "1.0"), done.stdout + done.stderr
        assert int(peak_mib) < 100, f"peak {peak_mib} MiB above the built index"

    def test_stream_yields_every_pair_in_fixed_batches(self):
        ix = _index("python", self.SRC)
        reader = ix.stream_pairs(batch_size=4096)
        assert isinstance(reader, pa.RecordBatchReader)
        assert reader.schema == ix.pairs().schema
        batches = list(reader)
        assert [b.num_rows for b in batches[:-1]] == [4096] * (len(batches) - 1)
        assert sum(b.num_rows for b in batches) == 44_850
        streamed = pa.Table.from_batches(batches)

        def keyed(t):
            return sorted(zip(t["a"].to_pylist(), t["b"].to_pylist(), t["shared"].to_pylist()))
        assert keyed(streamed) == keyed(ix.pairs())                           # the same pairs, unranked

    def test_stream_is_deterministic(self):
        ix = _index("python", self.SRC)
        first = pa.Table.from_batches(list(ix.stream_pairs(threshold=0.3, batch_size=777)))
        for _ in range(3):
            assert pa.Table.from_batches(list(ix.stream_pairs(threshold=0.3, batch_size=777))).equals(first)

    @pytest.mark.parametrize("kind, threshold", [("function", 0.01), ("function", 0.5), ("class", 0.5)])
    def test_a_stream_at_a_threshold_is_the_sweep_in_order(self, kind, threshold):
        """At a threshold a stream sweeps blocks of rows into collectors — function rows counted from
        every token below 0.5 (here in two blocks: more than 2**19 candidates), filtered and sorted
        back into sweep order from 0.5, class rows counted from their indexes — and the reader joins
        blocks where a batch spans them: the sweep's pairs at that threshold, in sweep order, every
        column as pairs() has it, dictionaries included."""
        ix = _index("python", "\n".join(f"def f{i}(a):\n    return a * {i % 7} + b{i % 5}\n" for i in range(1030))) if kind == "function" \
            else _classes(300)
        swept = _in_sweep_order(ix, kind, ix.pairs(kind=kind, threshold=threshold))
        cut = list(ix.stream_pairs(kind=kind, threshold=threshold, batch_size=10_007))
        assert all(b.num_rows == 10_007 for b in cut[:-1]) and len(cut) > 1
        assert pa.Table.from_batches(cut).equals(swept)

    def test_shared_counts_agree_between_merging_and_counting(self):
        """pairs() from 0.5 merges each candidate's subtree sets (the prefix filter offers them;
        utils/sorted_sets.h merges four values a step with AVX2 where the machine has it); a
        threshold-0 stream counts them from an index of every token. Functions of 1 to 150
        statements, many repeated, so the sets run from one value to hundreds on every alignment of
        four: both must give every pair the same counts."""
        src = "\n".join(f"def f{i}(a):\n" + "".join(f"    b{j % (1 + i % 9)} = a * {j}\n" for j in range(1 + i)) + "    return a\n"
                        for i in range(150))
        ix = _index("python", src)
        cols = ["a", "b", "shared", "total"]
        counted = {(r["a"], r["b"]): r for r in ix.stream_pairs(columns=cols).read_all().to_pylist()}
        for threshold in (0.5, 0.7, 0.9):
            merged = ix.pairs(threshold=threshold, columns=cols).to_pylist()
            assert merged and all(counted[(r["a"], r["b"])] == r for r in merged)
        assert len(ix.pairs(threshold=0.5)) > 500

    @pytest.mark.parametrize("batch_size", [1, 7919, 1 << 20])
    def test_stream_is_the_sweep_in_order(self, batch_size):
        """stream_pairs() emits the sweep: the functions lightest first (ties by id), each against
        every later one, (a, b) its lower and higher id. Enough pairs for the workers' tiles to end
        inside rows and for batches to end inside tiles; every column as pairs() has it."""
        src = "\n".join(f"def f{i}(a):\n    return a * {i % 7} + b{i % 5}" + " + a" * (i % 4) + "\n" for i in range(400))   # 79,800 pairs: a dozen tiles
        ix = _index("python", src)
        swept = _in_sweep_order(ix, "function", ix.pairs())
        # a batch of one pair is a hand-over of its own: the first 1,000 run past the first two rows
        streamed = ix.stream_pairs(batch_size=batch_size).read_all() if batch_size > 1 else \
            pa.Table.from_batches(list(itertools.islice(ix.stream_pairs(batch_size=1), 1000)), schema=swept.schema)
        assert streamed.num_rows in (1000, 400 * 399 // 2)
        assert streamed.equals(swept.slice(0, streamed.num_rows))

    @pytest.mark.parametrize("batch_size", [1, 7919, 1 << 20])
    def test_the_class_stream_is_the_class_sweep_in_order(self, batch_size):
        """stream_pairs(kind="class") emits the class sweep the same way: classes lightest first,
        each against every later one, every column as pairs() has it. A class row's cost varies, so
        its tiles end inside rows, batches inside tiles."""
        ix = _classes(300)
        swept = _in_sweep_order(ix, "class", ix.pairs(kind="class"))
        # the first 1,000 one-pair batches run through the first three rows (299, 298, 297 pairs)
        streamed = ix.stream_pairs(kind="class", batch_size=batch_size).read_all() if batch_size > 1 else \
            pa.Table.from_batches(list(itertools.islice(ix.stream_pairs(kind="class", batch_size=1), 1000)), schema=swept.schema)
        assert streamed.num_rows in (1000, 300 * 299 // 2)
        assert streamed.equals(swept.slice(0, streamed.num_rows))

    def test_a_query_after_an_add_sees_the_added_units(self):
        """An index keeps what its corpus state gives every query — the units' names, files and
        lines, each kind's sweep order — and an add() renews it: queries on an index that grew
        equal those on one built with everything at once, names, sizes and streams alike."""
        first = "def f(a):\n    return a + 1\n\nclass K:\n    def m(self):\n        return 1\n"
        later = "def g(b):\n    return b + 1\n\ndef h(c):\n    if c:\n        return c * 2\n    return 0\n\nclass L:\n    def m(self):\n        return 2\n"
        grown = _index("python", first, origin="one")
        asked = {   # every cache filled before the add: the units' columns, both kinds' orders
            "functions": grown.pairs(threshold=0.0), "of size 19": grown.pairs(threshold=0.0, min_size=19),
            "stream": grown.stream_pairs().read_all(), "classes": grown.pairs(kind="class"),
        }
        grown.add(later, origin="two")
        whole = _index("python", first, origin="one")
        whole.add(later, origin="two")
        now = {"functions": grown.pairs(threshold=0.0), "of size 19": grown.pairs(threshold=0.0, min_size=19),
               "stream": grown.stream_pairs().read_all(), "classes": grown.pairs(kind="class")}
        assert {k: (asked[k].num_rows, now[k].num_rows) for k in now} == {
            "functions": (1, 10), "of size 19": (0, 3), "stream": (1, 10), "classes": (0, 1)}   # f, K.m; then g, h, L.m, and L
        for size in (0, 5, 9):
            assert grown.pairs(threshold=0.0, min_size=size).equals(whole.pairs(threshold=0.0, min_size=size))
        assert now["stream"].equals(whole.stream_pairs().read_all())
        assert now["classes"].equals(whole.pairs(kind="class"))
        assert "two" in now["functions"]["b_origin"].to_pylist()

    @pytest.mark.skipif(not hasattr(os, "fork"), reason="no fork on this platform")
    def test_a_forked_child_queries_without_its_parents_threads(self):
        """The threads parallel steps borrow (utils/parallel.h, parked_workers) belong to the
        process that started them: a child forked after a query runs its own query to the end."""
        ix = _index("python", "\n".join(f"def f{i}(a):\n    return a * {i % 7} + {i % 3}\n" for i in range(800)))
        want = ix.pairs(threshold=0.5).num_rows   # the parent's threads are started
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)   # forking a process with threads: deliberate here
            pid = os.fork()
        if pid == 0:
            os._exit(0 if ix.pairs(threshold=0.5).num_rows == want else 1)
        deadline = time.monotonic() + 20
        while (done := os.waitpid(pid, os.WNOHANG))[0] == 0:
            if time.monotonic() > deadline:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
                pytest.fail("the child's query never finished")
            time.sleep(0.005)
        assert os.waitstatus_to_exitcode(done[1]) == 0

    def test_stream_respects_threshold_and_min_size(self):
        ix = _index("python", self.SRC)
        streamed = ix.stream_pairs(threshold=0.9, min_size=5).read_all()
        assert streamed.num_rows == ix.pairs(threshold=0.9, min_size=5).num_rows
        assert pc.min(streamed["score"]).as_py() >= 0.9

    def test_stream_outlives_its_index(self):
        reader = _index("python", self.SRC).stream_pairs()      # the Index is gone at once
        assert reader.read_all().num_rows == 44_850

    def test_an_empty_stream_keeps_its_schema(self):
        t = similarity.Index("cpp").stream_pairs().read_all()
        assert t.num_rows == 0 and t.schema.names[:2] == ["a", "b"]

    def test_add_while_streaming_fails_the_stream(self):
        """Blocks swept after an add() would mix two corpora: the stream fails instead."""
        ix = _index("python", "\n".join(f"def f{i}(a):\n    return a + {i % 3}\n" for i in range(6000)))
        reader = ix.stream_pairs(batch_size=100_000)
        reader.read_next_batch()
        ix.add("def late(q):\n    return q\n")
        with pytest.raises(pa.ArrowInvalid, match="index changed while a stream_pairs"):
            for _ in reader:
                pass

    def test_adds_while_a_class_stream_is_written_never_read_a_moved_unit(self):
        """add()s that grow the corpus while a class stream's tiles are written: the stream ends
        whole or fails as changed, and no tile counts its rows from units an add moved — each
        counts them under the shared lock. Found 2026-10-29: a placed class tile let go of the lock
        after checking the generation, then read the units; AddressSanitizer saw the read of the
        freed array (PYGIM_SANITIZE, tests/static/run_tests_under_sanitizers.py)."""
        classes = "".join(f"class C{i}:\n" + "".join(f"    def m{k}(self, a):\n        return a * {k} + {i % 7}\n" for k in range(3))
                          for i in range(2000))
        ix = _index("python", classes)
        reader = ix.stream_pairs(kind="class", batch_size=1000)
        reader.read_next_batch()
        adder = threading.Thread(target=lambda: [ix.add(classes, origin=f"more{k}") for k in range(3)])
        adder.start()
        rows = 1000
        try:
            for batch in reader:
                rows += batch.num_rows
        except pa.ArrowInvalid as e:
            assert "index changed while a stream_pairs" in str(e)
        else:
            assert rows == 2000 * 1999 // 2
        adder.join()

    def test_counting_an_index_while_files_are_added_reads_one_state(self, tmp_path):
        """len(), stats() and repr() while add_files() grows the corpus on another thread, the
        GIL released: each reads one state, under the shared lock, never a vector being moved.
        Found 2026-10-29: the three read the corpus holding only the GIL (AddressSanitizer)."""
        files = []
        for k in range(30):
            files.append(tmp_path / f"m{k}.py")
            files[-1].write_text("".join(f"def f{i}(a):\n    return a * {i % 5} + {k}\n" for i in range(150)))
        ix = similarity.Index("python")
        adder = threading.Thread(target=lambda: [ix.add_files(files) for _ in range(4)])
        adder.start()
        seen = []   # each read sees a whole add_files() or none of it: 4,500 units each
        while adder.is_alive():
            seen += [len(ix), ix.stats()["units"], int(repr(ix).split("units=")[1].rstrip(")"))]
        adder.join()
        assert seen == sorted(seen) and {n % 4500 for n in seen} <= {0}
        assert len(ix) == 4 * 4500

    def test_stream_writes_to_parquet(self, tmp_path):
        pq = pytest.importorskip("pyarrow.parquet")
        ix = _index("python", self.SRC)
        reader = ix.stream_pairs(threshold=0.5, batch_size=2048)
        with pq.ParquetWriter(tmp_path / "pairs.parquet", reader.schema) as w:
            for batch in reader:
                w.write_batch(batch)
        assert pq.read_table(tmp_path / "pairs.parquet").num_rows == ix.pairs(threshold=0.5).num_rows

    @pytest.mark.parametrize("method", ["pairs", "stream_pairs"])
    def test_columns_select_and_order(self, method):
        ix = _index("python", self.SRC)
        out = getattr(ix, method)(columns=["score", "b", "a"])
        t = out if isinstance(out, pa.Table) else out.read_all()
        assert t.schema.names == ["score", "b", "a"]
        full = ix.pairs() if method == "pairs" else ix.stream_pairs().read_all()
        assert t.equals(full.select(["score", "b", "a"]))

    @pytest.mark.parametrize("columns, message", [
        (["a", "nope"], "unknown pairs column 'nope' \\(known: a, b, a_qualname"),
        (["a", "a"], "pairs column 'a' requested twice"),
        ([], "columns must name at least one column"),
    ])
    @pytest.mark.parametrize("method", ["pairs", "stream_pairs"])
    def test_columns_are_validated(self, method, columns, message):
        with pytest.raises(ValueError, match=message):
            getattr(_index("python", PY), method)(columns=columns)

    def test_slim_columns_join_back_to_units(self):
        ix = _index("python", self.SRC)
        slim = ix.pairs(threshold=0.9, columns=["a", "b", "score"])
        names = ix.units().select(["id", "qualname"])
        joined = slim.join(names, keys="a", right_keys="id").rename_columns(["a", "b", "score", "a_qualname"])
        full = ix.pairs(threshold=0.9).select(["a", "a_qualname"]).to_pylist()
        assert sorted((r["a"], r["a_qualname"]) for r in joined.to_pylist()) == sorted(
            (r["a"], r["a_qualname"]) for r in full)

    @pytest.mark.parametrize("method", ["pairs", "stream_pairs"])
    def test_batch_size_is_validated(self, method):
        with pytest.raises(ValueError, match="batch_size must be at least 1"):
            getattr(_index("python", PY), method)(batch_size=0)

    @pytest.mark.parametrize("method", ["pairs", "stream_pairs"])
    def test_the_largest_batch_size_is_one_batch_of_everything(self, method):
        """Batches are counted as pairs / batch_size rounded up without adding batch_size first,
        which wrapped to no batch at all for a batch_size of 2**64 - 1 (2026-10-29)."""
        got = getattr(_index("python", PY), method)(batch_size=2**64 - 1)
        batches = got.to_batches() if method == "pairs" else list(got)
        assert [b.num_rows for b in batches] == [21]


def _same(*sources, language="python", modules=None):
    """Whether every function in `sources` (one per source) is identical up to names."""
    ix = similarity.Index(language)
    for i, src in enumerate(sources):
        ix.add(textwrap.dedent(src), origin=f"s{i}", module=(modules or {}).get(i))
    t = ix.pairs(columns=["exact"])
    return t.num_rows == len(sources) * (len(sources) - 1) // 2 and all(t["exact"].to_pylist())


class TestImports:
    """The same thing done through different imports is the same tree."""

    SPELLINGS = [
        "import os\ndef f(p):\n    return os.path.join(p, 'x')\n",
        "import os.path\ndef f(p):\n    return os.path.join(p, 'x')\n",
        "from os import path\ndef f(p):\n    return path.join(p, 'x')\n",
        "import os.path as osp\ndef f(p):\n    return osp.join(p, 'x')\n",
        "from os.path import join\ndef f(p):\n    return join(p, 'x')\n",
        "from os.path import join as j\ndef f(p):\n    return j(p, 'x')\n",
        "def f(p):\n    import os\n    return os.path.join(p, 'x')\n",
        "def f(p):\n    from os.path import join as jj\n    return jj(p, 'x')\n",
    ]

    FALLBACK = ("try:\n    import ujson as json\nexcept ImportError:\n    import json\n"
                "def f(x):\n    return json.dumps(x)\n")

    # (sources, their modules by position, whether every one is the same call)
    @pytest.mark.parametrize("sources, modules, same", [
        (SPELLINGS, None, True),
        (("from os.path import join\ndef f(p):\n    return join(p, 'x')\n",
          "from shlex import join\ndef f(p):\n    return join(p, 'x')\n"), None, False),
        (("from os.path import *\ndef f(p):\n    return join(p, 'x')\n",
          "from os.path import join\ndef f(p):\n    return join(p, 'x')\n"), None, False),
        (("def f(x):\n    return len(x)\n",
          "import builtins\ndef f(x):\n    return builtins.len(x)\n"), None, True),
        (("from .util import helper\ndef f(x):\n    return helper(x)\n",
          "from pkg.util import helper\ndef f(x):\n    return helper(x)\n",
          "from . import util\ndef f(x):\n    return util.helper(x)\n"), {0: "pkg.a", 1: "pkg.b", 2: "pkg.c"}, True),
        (("from .util import helper\ndef f(x):\n    return helper(x)\n",
          "from util import helper\ndef f(x):\n    return helper(x)\n"), None, False),
        (("from .util import helper\ndef f(x):\n    return helper(x)\n",
          "from .util import helper as h\ndef f(x):\n    return h(x)\n"), None, True),
        (("import os\ndef f(p):\n    return os.path.join(p, 'x')\n",
          "def f(os, p):\n    return os.path.join(p, 'x')\n"), None, False),
        (("import os\ndef f(p):\n    return os.path.join(p, 'x')\n",
          "import os\ndef f(p):\n    os = p\n    return os.path.join(p, 'x')\n"), None, False),
        ((FALLBACK, "import json\ndef f(x):\n    return json.dumps(x)\n"), None, False),
        ((FALLBACK, FALLBACK.replace("dumps(x)", "dumps(y)").replace("f(x)", "f(y)")), None, True),
        (("list = list\ndef f(x):\n    return list(x)\n",
          "def f(x):\n    return list(x)\n"), {0: "pkg.a", 1: "pkg.b"}, True),
        (("import os\ngetcwd = os.getcwd\ndef f():\n    return getcwd()\n",
          "import os\ndef f():\n    return os.getcwd()\n",
          "from os import getcwd as here\ncwd = here\ndef f():\n    return cwd()\n"), None, True),
        (("import os\ngetcwd = os.getcwd\ngetcwd = None\ndef f():\n    return getcwd()\n",
          "import os\ndef f():\n    return os.getcwd()\n"), None, False),
        (("a = b\nb = a\ndef f():\n    return a()\n", "a = b\nb = a\ndef f():\n    return a()\n"), None, True),
        (("x = 1\ndef f(a, x=x):\n    return a + x\n", "x = 1\ndef f(a, y=x):\n    return a + y\n"), None, True),
        (("x = 1\ndef f(a, x=x):\n    return a + x\n", "z = 1\ndef f(a, z=z):\n    return a + z\n"), None, False),
        (("x = 1\ndef g():\n    return lambda a, x=x: a + x\n", "x = 1\ndef g():\n    return lambda a, y=x: a + y\n"), None, True),
        (("deco = 1\n@deco\ndef f(deco):\n    return deco\n", "deco = 1\n@deco\ndef f(other):\n    return other\n"), None, True),
    ], ids=[
        "every spelling of one call", "a different target", "a star import binds nothing",
        "builtins.len is len", "relative and absolute imports meet",
        "an unknown module keeps relative dots", "an unknown module's relative import meets itself",
        "a parameter shadows an import", "an assignment shadows an import",
        "two targets for one name stay as written", "two targets for one name meet themselves",
        "list = list is the builtin", "a module-level alias is what it aliases",
        "an alias bound twice stays as written", "an alias cycle ends",
        "a default is the enclosing scope's name", "defaults naming two globals differ",
        "a lambda's default is the enclosing scope's", "a decorator is the enclosing scope's",
    ])
    def test_one_call_spelled_many_ways(self, sources, modules, same):
        assert _same(*sources, modules=modules) is same

    # (sources with their modules, the pair that must be exact)
    @pytest.mark.parametrize("sources, pair", [
        ((("def helper(x):\n    return x\ndef f(x):\n    return helper(x) + 1\n", "pkg.util"),
          ("from pkg.util import helper\ndef g(y):\n    return helper(y) + 1\n", "pkg.other")), ("f", "g")),
        ((("import os\nclass C:\n    os = None\n    def m(self, p):\n        return os.sep + p\n", None),
          ("from os import sep\ndef m(self, p):\n    return sep + p\n", None)), ("C.m", "m")),
        ((("def f():\n    @(d := wrap)\n    def inner():\n        return 1\n    return d\n", None),
          ("def g():\n    @(e := wrap)\n    def inner():\n        return 1\n    return e\n", None)), ("f", "g")),
    ], ids=["a module-level definition is its qualified name", "methods do not see the class body",
            "a decorator's walrus binds in the enclosing function"])
    def test_a_name_is_what_its_scope_binds(self, sources, pair):
        ix = similarity.Index("python")
        for text, module in sources:
            ix.add(text, module=module)
        rows = ix.pairs(columns=["a_qualname", "b_qualname", "exact"]).to_pylist()
        assert {"a_qualname": pair[0], "b_qualname": pair[1], "exact": True} in rows

    def test_add_file_names_the_module_from_its_packages(self, tmp_path):
        pkg = tmp_path / "pkg"
        (pkg / "sub").mkdir(parents=True)
        (pkg / "__init__.py").write_text("")
        (pkg / "sub" / "__init__.py").write_text("")
        (pkg / "sub" / "a.py").write_text("from ..util import helper\ndef f(x):\n    return helper(x)\n")
        (pkg / "sub" / "b.py").write_text("from pkg import util\ndef g(x):\n    return util.helper(x)\n")
        ix = similarity.Index("python")
        ix.add_file(pkg / "sub" / "a.py")
        ix.add_file(pkg / "sub" / "b.py")
        assert ix.pairs(columns=["exact"])["exact"].to_pylist() == [True]
        # an explicit module wins: anchored elsewhere, the relative import names something else
        ix2 = similarity.Index("python")
        ix2.add_file(pkg / "sub" / "a.py", module="other.sub.a")
        ix2.add_file(pkg / "sub" / "b.py")
        assert ix2.pairs(columns=["exact"])["exact"].to_pylist() == [False]

    def test_cpp_aliases_and_using_declarations(self):
        ix = similarity.Index("cpp")
        ix.add("namespace fs = std::filesystem;\nint a(int x) { return std::move(fs::path(x)); }\n")
        ix.add("int b(int y) { return ::std::move(std::filesystem::path(y)); }\n")
        ix.add("using std::move;\nint c(int z) { return move(std::filesystem::path(z)); }\n")
        ix.add("int d(int w) { using namespace std; return move(filesystem::path(w)); }\n")
        rows = {(r["a_qualname"], r["b_qualname"]): r["exact"]
                for r in ix.pairs(columns=["a_qualname", "b_qualname", "exact"]).to_pylist()}
        assert rows[("a", "b")] and rows[("a", "c")] and rows[("b", "c")]
        assert not rows[("a", "d")]                    # `using namespace` cannot be followed statically


class TestCli:
    def test_threshold_zero_needs_a_limit(self, tmp_path):
        """-t 0 would rank every pair of every unit, n(n-1)/2 lines held in memory before the first is
        printed: refused before any file is read, unless -n caps it."""
        (tmp_path / "m.py").write_text("def f(a):\n    return a\n\ndef g(b):\n    return b\n")
        refused = CliRunner().invoke(cli, ["similar", str(tmp_path), "-t", "0"])
        assert refused.exit_code == 2 and "-n N" in refused.output
        capped = CliRunner().invoke(cli, ["similar", str(tmp_path), "-t", "0", "-n", "1", "-m", "0"])
        assert capped.exit_code == 0 and "the best 1 function pair(s)" in capped.output

    def test_one_index_per_language(self, tmp_path):
        (tmp_path / "a.py").write_text("def f(a):\n    return a + 1\n\ndef g(b):\n    return b + 1\n")
        (tmp_path / "b.cpp").write_text("int f(int a) { return a + 1; }\nint g(int b) { return b + 1; }\n")
        (tmp_path / "bad.hpp").write_text("void f() {\n")
        hidden = tmp_path / ".hidden"
        hidden.mkdir()
        (hidden / "c.py").write_text("def h(c):\n    return c + 1\n")
        result = CliRunner().invoke(cli, ["similar", str(tmp_path), "-t", "1.0", "-m", "0"])
        assert result.exit_code == 0, result.output
        assert "python: 1 function pair(s)" in result.output     # f ~ g; the hidden h never read
        assert "cpp: 1 function pair(s)" in result.output        # never a Python-C++ pair
        assert "bad.hpp:1: unclosed '{'" in result.output       # reported and skipped

    def test_reported_line_is_where_the_function_starts(self, tmp_path):
        """Seen on CPython: a docstring spliced over lines (bisect), and a macro writing '"' (_json's
        ENCODE_OUTPUT), left every later C function reported too early."""
        (tmp_path / "m.c").write_text('PyDoc_STRVAR(doc, "one\\n\\\ntwo\\n\\\nthree");\n'
                                      "#define ENCODE(o) o = '\"'; \\\n    o = '\"'; \\\n    o = 0\n"
                                      "int f(int a) { return a + 1; }\nint g(int b) { return b + 1; }\n")
        pygim = [sys.executable, "-c", "import sys; from pygim.__main__ import cli; sys.exit(cli())"]   # the console script
        result = subprocess.run([*pygim, "similar", str(tmp_path), "-t", "1.0", "-m", "0"],
                                capture_output=True, text=True, check=True)
        assert "m.c:7" in result.stdout and "m.c:8" in result.stdout, result.stdout

    @pytest.mark.parametrize("relative", [False, True])
    def test_the_printed_table_is_tabulates_layout_of_the_pairs(self, tmp_path, monkeypatch, relative):
        """similar prints each pair as its score (three decimals, ` =` when exact) and each side's
        `qualname  file:line`, in tabulate's plain layout — which it writes itself, a line at a time
        (tabulate took 0.5 s for 23,000 rows). It reads the files in pathlib's order, by their parts
        (two/deep before two/m.py, two-x after both), spelled from the root as given: an absolute
        path or `.`; names past ASCII, ties and several directories included."""
        for d in ("one", "two", "two/deep", "two-x"):
            (tmp_path / d).mkdir(parents=True, exist_ok=True)
            src = "".join(f"def f{i}_{d.replace('/', '_').replace('-', '_')}(a):\n    return a * {i % 3} + {i % 2}\n\n" for i in range(12))
            (tmp_path / d / "m.py").write_text(src + "def ñandú(b):\n    return b * 2 + 1\n", encoding="utf-8")
        root = tmp_path
        if relative:
            monkeypatch.chdir(tmp_path)
            root = pathlib.Path(".")
        result = CliRunner().invoke(cli, ["similar", str(root), "-t", "0.5", "-m", "0"])
        assert result.exit_code == 0, result.output
        ix = similarity.Index("python")
        ix.add_files([str(root / d / "m.py") for d in ("one", "two/deep", "two", "two-x")])
        pairs = ix.pairs(threshold=0.5).to_pylist()
        table = tabulate([(f"{r['score']:.3f}" + (" =" if r["exact"] else ""),
                           f"{r['a_qualname']}  {r['a_origin']}:{r['a_line']}",
                           f"{r['b_qualname']}  {r['b_origin']}:{r['b_line']}") for r in pairs],
                         headers=("score", "a", "b"), disable_numparse=True)
        assert len(pairs) > 100 and result.output == f"\npython: {len(pairs)} function pair(s) scoring >= 0.5\n{table}\n"

    @staticmethod
    def _twins(root):
        root.mkdir(parents=True, exist_ok=True)
        (root / "m.py").write_text("def f(a):\n    return a * 2 + 1\n\ndef g(b):\n    return b * 2 + 1\n")

    def test_one_tree_named_twice_is_read_once(self, tmp_path, monkeypatch):
        """Roots that overlap — one tree as an absolute path and as a relative one — read each file
        once, by its first spelling: no function pairs with its own copy (2026-10-29)."""
        self._twins(tmp_path / "proj")
        monkeypatch.chdir(tmp_path)
        result = CliRunner().invoke(cli, ["similar", str(tmp_path / "proj"), "proj", "-t", "1.0", "-m", "0"])
        assert result.exit_code == 0, result.output
        assert "python: 1 function pair(s) scoring >= 1.0" in result.output

    def test_check_exits_1_when_pairs_are_shown(self, tmp_path):
        """--check makes the command a gate: exit 1 when it shows pairs, 0 when it shows none."""
        self._twins(tmp_path)
        assert CliRunner().invoke(cli, ["similar", str(tmp_path), "-m", "0", "--check"]).exit_code == 1
        assert CliRunner().invoke(cli, ["similar", str(tmp_path), "-m", "0"]).exit_code == 0
        (tmp_path / "m.py").write_text("def f(a):\n    return a\n")
        assert CliRunner().invoke(cli, ["similar", str(tmp_path), "-m", "0", "--check"]).exit_code == 0

    def test_a_limit_says_it_shows_the_best(self, tmp_path):
        """With -n the header says these are the best pairs, not how many there are."""
        self._twins(tmp_path)
        (tmp_path / "n.py").write_text("def h(c):\n    return c * 2 + 1\n")
        result = CliRunner().invoke(cli, ["similar", str(tmp_path), "-m", "0", "-n", "1"])
        assert "python: the best 1 function pair(s) scoring >= 0.9" in result.output

    def test_an_excluded_file_named_on_the_command_line_is_left_out(self, tmp_path):
        self._twins(tmp_path)
        result = CliRunner().invoke(cli, ["similar", str(tmp_path / "m.py"), "-m", "0", "-x", "m.py"])
        assert "No function pairs scoring >= 0.9." in result.output

    @pytest.mark.skipif(not hasattr(os, "mkfifo"), reason="FIFOs are POSIX")
    def test_a_root_that_is_neither_file_nor_directory_is_named_as_such(self, tmp_path):
        os.mkfifo(tmp_path / "pipe")
        result = CliRunner().invoke(cli, ["similar", str(tmp_path / "pipe")])
        assert result.exit_code != 0 and "is neither a file nor a directory" in result.output

    def test_exclude_and_nothing_found(self, tmp_path):
        (tmp_path / "vendor").mkdir()
        (tmp_path / "vendor" / "a.py").write_text("def f(a):\n    return a\ndef g(b):\n    return b\n")
        result = CliRunner().invoke(cli, ["similar", str(tmp_path), "-m", "0", "-x", "vendor"])
        assert result.exit_code == 0, result.output
        assert "No function pairs scoring >= 0.9." in result.output


class TestStats:
    def test_counts_and_bytes(self):
        ix = _index("python", PY)
        st = ix.stats()
        assert (st["units"], st["functions"], st["classes"]) == (9, 7, 2)
        assert st["subtrees"] > 0 and st["bytes"] > 0
        assert len(ix) == 9
        assert repr(ix) == "similarity.Index('python', units=9)"
        assert ix.language == "python"



class TestEdges:
    """What the API refuses, and the paths the scenarios above do not reach."""

    TWO = "def f(a):\n    return a + 1\n\ndef g(b):\n    return b + 1\n"

    @pytest.mark.parametrize("kwargs, match", [({"threshold": 1.5}, "threshold must be within"), ({"kind": "method"}, "kind must be"),
                                               ({"batch_size": 0}, "batch_size must be at least 1")])
    def test_stream_pairs_checks_its_arguments_as_pairs_does(self, kwargs, match):
        ix = _index("python", self.TWO)
        with pytest.raises(ValueError, match=match):
            ix.stream_pairs(**kwargs)
        with pytest.raises(ValueError, match=match):
            ix.pairs(**kwargs)

    def test_units_refuse_an_unknown_kind(self):
        with pytest.raises(ValueError, match="kind must be"):
            similarity.Index("python").units("method")

    @pytest.mark.parametrize("one", ["a.py", b"a.py"])
    def test_add_files_takes_paths_not_one_path(self, one):
        """A str or bytes is iterable: its characters were taken for paths."""
        with pytest.raises(TypeError, match="not one path"):
            similarity.Index("python").add_files(one)

    def test_add_files_refuses_no_threads_even_of_nothing(self):
        with pytest.raises(ValueError, match="threads must be at least 1"):
            similarity.Index("python").add_files([], threads=0)

    def test_add_file_raises_what_open_would(self, tmp_path):
        if os.geteuid() == 0:
            pytest.skip("root reads every file")
        locked = tmp_path / "locked.py"
        locked.write_text("def f(a):\n    return a\n")
        locked.chmod(0)
        with pytest.raises(PermissionError):
            similarity.Index("python").add_file(locked)
        folder = tmp_path / "pkg.py"
        folder.mkdir()
        with pytest.raises(IsADirectoryError):
            similarity.Index("python").add_file(folder)

    def test_an_init_source_is_its_own_package(self):
        """`from . import util` in pkg/__init__.py is pkg.util: an added text's package is its module
        when its origin names an __init__ file, as add_file() takes a file's."""
        init = "from . import util\ndef f(x):\n    return util.helper(x)\n"
        absolute = "from pkg import util\ndef g(y):\n    return util.helper(y)\n"
        assert _same(init, absolute, modules={0: "pkg", 1: "other"}) is False   # origin "s0": pkg's parent is the package
        ix = similarity.Index("python")
        ix.add(init, origin="pkg/__init__.py", module="pkg")
        ix.add(absolute, module="other")
        assert ix.pairs(columns=["exact"])["exact"].to_pylist() == [True]

    def test_a_placed_stream_leaves_out_small_units(self):
        ix = _index("python", "\n".join(f"def f{i}(a):\n    return a * {i % 7}" + " + a" * (i % 6) + "\n" for i in range(300)))
        small = ix.stream_pairs(min_size=25).read_all()   # four of the six sizes: 200 of 300 functions
        assert small.num_rows == 200 * 199 // 2
        assert small.equals(_in_sweep_order(ix, "function", ix.pairs(min_size=25)))

    def test_every_language_says_what_it_reads(self):
        assert all(isinstance(r["doc"], str) and r["doc"] and r["exts"] and isinstance(r["exts"], tuple) for r in similarity.LANGUAGES.values())

    @pytest.mark.parametrize("path, language", [(b"x.py", "python"), (pathlib.Path("x.CPP"), "cpp"), ("x.txt", None)])
    def test_language_of_takes_any_path(self, path, language):
        assert similarity.language_of(path) == language


class TestThreads:
    """adapter.h's claim, held: an add waits for the queries reading the corpus and they wait for
    it, so a query on one thread beside adds on another sees a whole corpus every time. Before
    index_state's commands, len(), stats() and repr() read the units while add_files() grew them."""

    def test_queries_run_beside_adds_on_another_thread(self, tmp_path):
        files = []
        for i in range(40):
            f = tmp_path / f"m{i}.py"
            f.write_text("".join(f"def f{i}_{j}(a):\n    return a * {j % 5} + {i}\n" for j in range(30)))
            files.append(f)
        ix = similarity.Index("python")
        failures, done = [], threading.Event()
        def add():
            try:
                for f in files[:20]:
                    ix.add_file(f)
                ix.add_files(files[20:])
            except BaseException as e:   # noqa: B902 - any failure is the test's
                failures.append(e)
            finally:
                done.set()
        adder = threading.Thread(target=add)
        adder.start()
        seen = []
        while not done.is_set():
            n = len(ix)
            seen.append(n)
            assert ix.stats()["units"] >= n and repr(ix).startswith("similarity.Index('python'")
            assert ix.units().num_rows >= n
            ix.pairs(limit=5)
            try:
                ix.stream_pairs(threshold=0.5).read_all()
            except pa.ArrowInvalid:
                pass   # an add came between: the stream says so
        adder.join()
        assert failures == []
        assert seen == sorted(seen) and len(ix) == 40 * 30


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__)


@pytest.mark.skipif(sys.platform == "win32", reason="a file name that is not UTF-8 is POSIX's")
@pytest.mark.parametrize("as_bytes", [True, False], ids=["bytes", "str"])
def test_a_file_name_the_filesystem_cannot_decode_is_refused_before_any_work(tmp_path, as_bytes):
    """Given as bytes or as the str os.fsdecode makes of it, a name that is no UTF-8 is a
    UnicodeEncodeError up front: the bytes form went through and put invalid UTF-8 in a utf8 column."""
    name = os.path.join(os.fsencode(tmp_path), b"caf\xe9.py")
    with open(name, "wb") as f:
        f.write(b"def f(x):\n    return x\n")
    ix = similarity.Index("python")
    with pytest.raises(UnicodeEncodeError):
        ix.add_files([name if as_bytes else os.fsdecode(name)])
    assert len(ix) == 0
