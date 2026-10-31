# -*- coding: utf-8 -*-
"""pygim.code: Python 3.14 parsed in C++ (code/python/*.h, code/corpus.h). The oracle is CPython's
own parser: a tree's dump must equal ast.dump of ast.parse, field for field and position for
position; a source CPython refuses must be refused."""

import ast
import os
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

from pygim import code
from stub_parity import Stub

SRC = Path(__file__).resolve().parents[2] / "src" / "_pygim_fast" / "code" / "python"


# The nodes whose `value` the dump keeps when it is None: MatchSingleton is Python 3.10's.
KEEPS_NONE = tuple(getattr(ast, name) for name in ("Constant", "MatchSingleton") if hasattr(ast, name))


def dump314(node, attributes=False):
    """ast.dump as Python 3.14 writes it (None and [] left out but for Constant's and MatchSingleton's
    value), from the running Python's tree — 3.12's ast.dump prints empty lists."""
    if isinstance(node, ast.AST):
        parts = []
        for name in node._fields:
            value = getattr(node, name, None)
            keep = isinstance(node, KEEPS_NONE) and name == "value"
            if (value is None or value == []) and not keep:
                continue
            parts.append(f"{name}={dump314(value, attributes)}")
        if attributes:
            parts += [f"{name}={getattr(node, name)!r}" for name in node._attributes if getattr(node, name, None) is not None]
        return f"{type(node).__name__}({', '.join(parts)})"
    if isinstance(node, list):
        return "[" + ", ".join(dump314(x, attributes) for x in node) + "]"
    return repr(node)


def nested_blocks(levels):
    """`levels` if-blocks, each inside the one before."""
    return "".join("    " * i + "if a:\n" for i in range(levels)) + "    " * levels + "b\n"


def nested_fstrings(levels):
    """`levels` f-strings, each the replacement field of the one around it."""
    s = "x"
    for _ in range(levels):
        s = 'f"{' + s + '}"'
    return s


# Each a construct of the grammar, valid in the running Python as in 3.14: the dump must be
# CPython's, positions included.
SAME_AS_CPYTHON = {
    "99 levels of indentation": nested_blocks(99),
    "149 nested f-strings": nested_fstrings(149),
    "70,000 spaces of indentation": "if a:\n" + " " * 70_000 + "b\n" + " " * 70_000 + "c\n",
    "match on a lambda": "match lambda: 1:\n    case _: pass\n",
    "an f-string as a value pattern": 'match x:\n    case f"y": pass\n',
    "an f-string as a mapping key": 'match x:\n    case {f"y": 1}: pass\n',
    "4,300 digits": "x = " + "1" * 4300,
    "200 nested brackets": "x = " + "(" * 200 + "1" + ")" * 200,
    "crlf in a string inside f-string debug text": 'f"""{ """a\r\nb""" = }"""',
    "assignment": "x = 1",
    "chained assignment": "a = b = c",
    "tuple target": "a, *b = c",
    "list target in parentheses": "[a, (b, c)] = d",
    "annotated": "x: int = 1",
    "annotated, parenthesized": "(x): int",
    "annotated attribute": "a.b: List[int]",
    "augmented": "x //= 2",
    "expression statement": "f(x)",
    "semicolons": "a; b; c;",
    "if elif else": "if a:\n    b\nelif c:\n    d\nelse:\n    e",
    "while else": "while x:\n    break\nelse:\n    pass",
    "for tuple target": "for i, (a, b) in x:\n    continue",
    "async for and with": "async def f():\n    async for x in y:\n        async with a as b, c:\n            await d",
    "with parenthesized items": "with (open(a) as f, open(b) as g):\n    pass",
    "with a tuple as context": "with (a, b) as c:\n    pass",
    "try except else finally": "try:\n    a\nexcept E as e:\n    b\nexcept:\n    c\nelse:\n    d\nfinally:\n    e",
    "try except star": "try:\n    a\nexcept* (E, F) as g:\n    b",
    "def with every kind of parameter": "def f(a, b=1, /, c=2, *d, e, f=3, **g) -> int:\n    return a",
    "def with annotations": "def f(a: int, *args: str, **kw: bool): pass",
    "lambda with parameters": "lambda a, b=1, *c, d, **e: a",
    "decorated function and class": "@d1\n@d2(x)\nclass C(B, metaclass=M):\n    @property\n    def p(self): return 1",
    "class with keywords": "class C(*bases, **kw): pass",
    "type parameters": "def f[T: int, *Ts, **P](x: T) -> T: pass",
    "type alias": "type Point[T] = tuple[T, T]",
    "soft keywords as names": "match = case = type = 1\nprint(match, case.type)",
    "import": "import a.b.c as d, e",
    "from import": "from ..a import (b as c, d)",
    "from import star": "from . import *",
    "global nonlocal": "def f():\n    global a, b\n    def g():\n        nonlocal c",
    "del": "del a, b[1], (c.d, e)",
    "raise from": "raise E from c",
    "assert with message": "assert x, 'message'",
    "return yield": "def f():\n    x = yield\n    y = yield from z\n    return (yield a, b)",
    "boolean operators": "a and b and c or not d",
    "comparisons chained": "a < b <= c != d is not e not in f",
    "arithmetic precedence": "-a ** -b * c + d // e % f @ g << h & i ^ j | k",
    "conditional expression": "a if b else c if d else e",
    "walrus": "if (n := len(a)) > 10: pass",
    "calls": "f(a, *b, c=1, **d)",
    "generator as the only argument": "f(x for x in y)",
    "comprehensions": "[x for x in y if x], {k: v for k, v in z}, {s async for s in t}",
    "nested comprehension": "[[y for y in x] for x in z if x if not y]",
    "displays": "[1, *a], {1, *b}, {1: 2, **c}, (), (1,), {}",
    "subscripts and slices": "a[1], a[1:2], a[::3], a[:, 1], a[1:2, ...], a[x:=1]",
    "attributes": "a.b.c(d).e[f]",
    "numbers": "0, 0x_ff, 0o17, 0b1, 1_000, 1.5, .5, 5., 1e10, 1E-5, 3j, 1.5J, 00",
    "strings": "'a', \"b\", '''c''', r'\\d', b'\\x00', u'\\u20ac'",
    "string escapes": "'\\n\\t\\x41\\101\\u00e9\\U0001F600\\\\\\'\\q'",
    "implicit concatenation": "'a' 'b' \"c\"",
    "bytes concatenation": "b'a' b'b'",
    "u prefix kind": "u'a' 'b'",
    "f-strings": "f'a{b}c{d!r}e{f:>10}g{h=}i{j = !s:{k}}'",
    "f-string nested quotes": "f\"{'a' + \"b\"}\"",
    "f-string merged with strings": "'a' f'b{c}d' 'e'",
    "f-string doubled braces": "f'{{}}{x}{{'",
    "f-string over lines": "f'''{\n1 +\n2\n}'''",
    "f-string format spec nested": "f'{x:{y}.{z}f}'",
    "starred statement": "*a, b",
    "parenthesized expression positions": "(a) + (b)",
    "match literals and captures": "match x:\n    case 1 | -2 | 3.5 | 1 + 2j | 'a' | None | True:\n        pass\n    case y:\n        pass",
    "match sequences": "match x:\n    case [a, *rest]:\n        pass\n    case (b, c) | ():\n        pass\n    case d, *_:\n        pass",
    "match mappings and classes": "match x:\n    case {'k': v, **rest}:\n        pass\n    case Point(1, y=2) as p if p:\n        pass\n    case a.b.C():\n        pass",
    "match wildcard and group": "match x:\n    case (_):\n        pass",
    "match as a call": "match(x)\nmatch[1] = 2\nmatch -x",
    "ellipsis and constants": "..., None, True, False",
    "await": "async def f():\n    return await a.b()",
    "decorator expression": "@a.b[c](d)\ndef f(): pass",
    "backslash continuation": "x = 1 + \\\n    2",
    "tabs in indentation": "if a:\n\tb\n\tif c:\n\t\td",
    "comments and blank lines": "# c\n\nx = 1  # d\n\n\n# e\n",
    "no newline at the end": "if x:\n    pass",
    "crlf line ends": "if a:\r\n    b = '''x\r\ny'''\r\n",
    "non-ascii names and strings": "ä = 'é' + \"€\"",
    "docstring and future import": "'''doc'''\nfrom __future__ import annotations",
}


@pytest.mark.filterwarnings("ignore::SyntaxWarning")   # '\\q' is an invalid escape: CPython warns, and keeps it
@pytest.mark.skipif(sys.version_info < (3, 12), reason="the oracle is this Python's ast, which reads f-strings and an import's names as 3.14 does from 3.12")
@pytest.mark.parametrize("source", list(SAME_AS_CPYTHON.values()), ids=list(SAME_AS_CPYTHON))
def test_the_tree_is_cpythons(source):
    assert code.parse_source(source).dump(include_attributes=True) == dump314(cpython_tree(source), True)


def cpython_tree(source):
    """The running Python's tree of `source`; a skip where it is older than 3.14 and its ast cannot
    read syntax that is 3.14's own."""
    try:
        return ast.parse(source)
    except SyntaxError as refused:
        if sys.version_info >= (3, 14):
            raise
        pytest.skip(f"Python {sys.version_info[0]}.{sys.version_info[1]}'s ast cannot read it: {refused.msg}")


# Syntax the running Python may not have: what python3.14's ast.dump gives, pinned 2026-10-25.
PINNED_314 = {
    "t-string": ('t"a{b!r:>{c}}"', "Module(body=[Expr(value=TemplateStr(values=[Constant(value='a'), "
                 "Interpolation(value=Name(id='b', ctx=Load()), str='b', conversion=114, format_spec=JoinedStr(values="
                 "[Constant(value='>'), FormattedValue(value=Name(id='c', ctx=Load()), conversion=-1)]))]))])"),
    "except without parentheses (PEP 758)": ("try:\n    a\nexcept E, F:\n    b", "Module(body=[Try(body=[Expr(value="
                                              "Name(id='a', ctx=Load()))], handlers=[ExceptHandler(type=Tuple(elts=["
                                              "Name(id='E', ctx=Load()), Name(id='F', ctx=Load())], ctx=Load()), body="
                                              "[Expr(value=Name(id='b', ctx=Load()))])])])"),
    "type parameter defaults (PEP 696)": ("class A[T = int, *Ts = *tuple[int]]: pass", "Module(body=[ClassDef(name='A', "
                                          "body=[Pass()], type_params=[TypeVar(name='T', default_value=Name(id='int', "
                                          "ctx=Load())), TypeVarTuple(name='Ts', default_value=Starred(value=Subscript("
                                          "value=Name(id='tuple', ctx=Load()), slice=Name(id='int', ctx=Load()), "
                                          "ctx=Load()), ctx=Load()))])])"),
    "f-string debug text merged": ('f"a{x=}"', "Module(body=[Expr(value=JoinedStr(values=[Constant(value='ax='), "
                                   "FormattedValue(value=Name(id='x', ctx=Load()), conversion=114)]))])"),
    "named character escape": ('"\\N{SNOWMAN}"', "Module(body=[Expr(value=Constant(value='☃'))])"),
}


@pytest.mark.parametrize("source, expected", list(PINNED_314.values()), ids=list(PINNED_314))
def test_syntax_of_3_14_reads_as_python_3_14_has_it(source, expected):
    assert code.parse_source(source).dump() == expected


# Each refused by CPython's parser: the message is ours, the refusal and its line are CPython's (None
# where CPython names no line), checked against the running CPython too.
NOT_PYTHON = {
    "unclosed bracket": ("f(a, b", 1),
    "unexpected indent": ("x = 1\n    y = 2", 2),
    "dedent to no level": ("if a:\n        b\n    c", 3),
    "tabs and spaces mixed": ("if a:\n        b\n\tc", 3),
    "assignment to a literal": ("1 = x", 1),
    "assignment to a call": ("f() = 1", 1),
    "keyword as a name": ("class = 1", 1),
    "positional after keyword": ("f(a=1, b)", 1),
    "default before non-default": ("def f(a=1, b): pass", 1),
    "bare star without names": ("def f(*): pass", 1),
    "generator not alone": ("f(x for x in y, 1)", 1),
    "bytes mixed with str": ("b'a' 'b'", 1),
    "t-string mixed with str": ("t'a' 'b'", 1),
    "unterminated string": ("x = 'abc", 1),
    "unterminated triple quote": ('x = """abc\n\n', 1),
    "bad escape": ("'\\x4'", 1),
    "single closing brace in f-string": ("f'}'", 1),
    "f-string specs nested too deep": ("f'{a:{b:{c:{d}}}}'", 1),
    "leading zero": ("x = 012", 1),
    "invalid identifier character": ("a€ = 1", 1),
    "null byte": ("x = 1\0", None),            # refused before there is text: no line
    "null byte on line 3": ("x = 1\n\ny = 2\0", None),
    "100 levels of indentation": (nested_blocks(100), 101),
    "150 nested f-strings": (nested_fstrings(150), 1),
    "a walrus as a slice bound": ("x[a:=1:2]", 1),
    "a walrus as a dict key": ("{a := 1: 2}", 1),
    "a walrus as a dict comprehension key": ("{a := 1: 2 for a in b}", 1),
    "4,301 digits": ("x = " + "1" * 4301, 1),
    "201 nested brackets": ("x = " + "(" * 201 + "1" + ")" * 201, 1),
    "del of a star": ("del *a", 1),
    "star pattern alone in a group": ("match x:\n    case (*y):\n        pass", 2),
    "except and except star mixed": ("try:\n    a\nexcept E:\n    b\nexcept* F:\n    c", 5),
}


def test_the_buffer_scans_agree_with_cpython_at_every_offset_around_their_steps():
    """utils/byte_scan.h checks UTF-8 and finds where lines start 64 bytes a step (16 or 32 bytes at
    a time, AVX2 chosen at run time; the scalar loop for the tail): a non-ASCII character, each kind
    of line end and a \\r\\n split by a step's edge, at every offset from 0 to 140, give CPython's
    positions, and invalid UTF-8 there CPython's error line. This is the path the dispatch takes
    here; tests/static/run_byte_scan_paths.py checks every path against the scalar definition."""
    for k in range(141):
        pad = "#" + "x" * k
        for src in (pad + "é\nb = 'ü'\n", pad + "\r\nb = 1\rc = 2\n", pad + "\rb = 1\n", pad + "\nb = 1\r\n",
                    "a = 1\n" + pad + "\r\nb\n" + "é" * (k % 7)):
            assert code.parse_source(src).dump(include_attributes=True) == dump314(ast.parse(src), True), (k, src)
        bad = (pad + "\nb = 1\n").encode() + b"\xffc = 2\n"
        lines = []
        for parse in (ast.parse, code.parse_source):
            with pytest.raises(SyntaxError) as raised:
                parse(bad)
            lines.append(raised.value.lineno)
        assert lines[0] == lines[1], (k, lines)


@pytest.mark.parametrize("source, line", list(NOT_PYTHON.values()), ids=list(NOT_PYTHON))
def test_what_cpython_refuses_is_refused(source, line):
    with pytest.raises(SyntaxError) as raised:
        code.parse_source(source)
    assert raised.value.lineno == line
    if sys.version_info >= (3, 12):   # older Pythons accept some (4,301 digits) or refuse them elsewhere
        with pytest.raises(SyntaxError) as theirs:
            ast.parse(source)
        assert theirs.value.lineno == line


@pytest.mark.parametrize("source, missing", [("def (x): pass", "a name"), ("import\n", "a name"),
                                             ("for x in y\n", "':'"), ("from a impor b\n", "'import'")])
def test_what_is_missing_is_named_by_its_text_or_by_what_stands_there(source, missing):
    """An operator or keyword is named by its text, quoted; a name, a number or a new line by what
    it is, unquoted (every one read quoted before: "expected 'a name'")."""
    with pytest.raises(SyntaxError) as raised:
        code.parse_source(source)
    assert raised.value.msg == f"invalid syntax: expected {missing}"


def test_a_relative_import_past_65535_packages_up_is_refused_where_cpython_reads_it():
    """A row holds an import's level in 16 bits, so `from` with more than 65,535 dots is refused —
    CPython reads 70,000 (code_parser.md, "Known differences"); it wrapped to 4,464 before."""
    with pytest.raises(SyntaxError, match="more than 65,535 packages up"):
        code.parse_source("from " + "." * 70_000 + " import x")
    assert code.parse_source("from " + "." * 65_535 + " import x").find("ImportFrom")[0].field("level") == 65_535


# Refused by Python 3.14's ast.parse, though not by every earlier one's.
@pytest.mark.parametrize("source", ["from __future__ import braces", "'''doc'''\nfrom __future__ import nope"])
def test_an_unknown_future_feature_is_refused_as_3_14_refuses_it(source):
    with pytest.raises(SyntaxError):
        code.parse_source(source)


@pytest.mark.parametrize("encoding, text", [("cp1252", "x = '€ – ‰'"), ("koi8-r", "x = 'привет'"), ("Windows-1251", "x = 'Ж'"),
                                            ("iso-8859-15", "x = '€'"), ("mac_roman", "x = 'ƒ'")])
def test_a_single_byte_encoding_is_read_by_its_name_or_alias(encoding, text):
    src = f"# -*- coding: {encoding} -*-\n{text}\n".encode(encoding)
    assert code.parse_source(src).dump() == dump314(ast.parse(src))


def test_a_multi_byte_encoding_is_refused_as_the_files_error():
    with pytest.raises(SyntaxError):
        code.parse_source("# coding: shift_jis\nx = 'テスト'\n".encode("shift_jis"))


# A file python3.14 cannot decode: its SyntaxError's (msg, lineno, offset), pinned 2026-10-30 (the
# running Python's differ: 3.12 says "(unicode error) ..." of a file that is not UTF-8).
NOT_DECODED_314 = {
    'utf8 alias, a sequence cut short': (b'# coding: utf8\nx = 1 \xe2\x82', ("'utf-8' codec can't decode bytes in position 21-22: invalid continuation byte", 0, -1)),
    'utf8 alias, a bad continuation': (b"# coding: utf8\nx = '\xc3'\n", ("'utf-8' codec can't decode byte 0xc3 in position 20: invalid continuation byte", 0, -1)),
    'utf8 alias, a surrogate': (b"# coding: utf8\nx = '\xed\xa0\x80'\n", ("'utf-8' codec can't decode byte 0xed in position 20: invalid continuation byte", 0, -1)),
    'utf8 alias, a bad start': (b"# coding: UTF8\nx = '\x80'\n", ("'utf-8' codec can't decode byte 0x80 in position 20: invalid start byte", 0, -1)),
    'ascii, a byte past 127': (b"# coding: us-ascii\nx='\xe9'\n", ("'ascii' codec can't decode byte 0xe9 in position 22: ordinal not in range(128)", 0, -1)),
    'single-byte, an undefined byte': (b"# coding: cp1253\nx='\xaa'\n", ("'charmap' codec can't decode byte 0xaa in position 20: character maps to <undefined>", 0, -1)),
    'no cookie, not UTF-8': (b"x = 1\ny = '\xc3'\n", ("Non-UTF-8 code starting with '\\xc3' on line 2, but no encoding declared; see https://peps.python.org/pep-0263/ for details", 2, 6)),
    'utf-8 cookie, not UTF-8': (b"# coding: utf-8\nx = '\xc3'\n", ("Non-UTF-8 code starting with '\\xc3' on line 2, but no encoding declared; see https://peps.python.org/pep-0263/ for details", 2, 6)),
    'a BOM with latin-1': (b'\xef\xbb\xbf# coding: latin-1\nx=1\n', ('encoding problem: iso-8859-1 with BOM', 1, 0)),
    'a BOM with an alias of UTF-8': (b'\xef\xbb\xbf# coding: UTF8\nx=1\n', ('encoding problem: UTF8 with BOM', 1, 0)),
    'a cookie after a line of code': (b"x = 1\n# coding: latin-1\ny = '\xe9'\n", ("Non-UTF-8 code starting with '\\xe9' on line 3, but no encoding declared; see https://peps.python.org/pep-0263/ for details", 3, 6)),
    'a cookie on line 3': (b"#!/x\n# hi\n# coding: latin-1\ny = '\xe9'\n", ("Non-UTF-8 code starting with '\\xe9' on line 4, but no encoding declared; see https://peps.python.org/pep-0263/ for details", 4, 6)),
    'an unknown encoding': (b'# coding: nope\nx=1\n', ('unknown encoding: nope', 0, -1)),
    'no text encoding': (b'# coding: hex\nx=1\n', ("'hex' is not a text encoding; use codecs.decode() to handle arbitrary codecs", 0, -1)),
}


@pytest.mark.parametrize("source, expected", list(NOT_DECODED_314.values()), ids=list(NOT_DECODED_314))
def test_a_file_that_cannot_be_decoded_is_refused_as_3_14_refuses_it(source, expected):
    with pytest.raises(SyntaxError) as raised:
        code.parse_source(source)
    assert (raised.value.msg, raised.value.lineno, raised.value.offset) == expected


@pytest.mark.skipif(shutil.which("python3.14") is None, reason="the pins are python3.14's")
def test_the_pinned_decoding_refusals_are_python_3_14s():
    probe = "import sys\nfor s in eval(sys.stdin.read()):\n    try: compile(s, 'f', 'exec'); print(None)\n    except SyntaxError as e: print((e.msg, e.lineno, e.offset))\n"
    sources = [s for s, _ in NOT_DECODED_314.values()]
    seen = subprocess.run(["python3.14", "-c", probe], input=repr(sources), capture_output=True, text=True, check=True).stdout.splitlines()
    assert seen == [repr(e) for _, e in NOT_DECODED_314.values()]


@pytest.mark.parametrize("head", [b"#!/usr/bin/env python\n", b"# a comment\n", b"\n"], ids=["a shebang", "a comment", "a blank line"])
def test_a_cookie_on_line_2_is_read_after_a_line_with_no_code(head):
    src = head + b"# -*- coding: latin-1 -*-\nx = '\xe9'\n"
    assert code.parse_source(src).find("Constant")[0].field("value") == "\xe9" and code.parse_source(src).dump() == dump314(ast.parse(src))


def test_a_str_source_is_text_already_and_its_cookie_is_not_read():
    assert code.parse_source("# coding: latin-1\nx = '\u20ac'\n").find("Constant")[0].field("value") == "\u20ac"   # not in latin-1


def test_the_views_name_what_they_are():
    tree = code.parse_source("x = 1\n", "m.py")
    assert (repr(tree), repr(tree.root), repr(tree.root.children[0])) == ("<Tree m.py: 4 nodes>", "<Module>", "<Assign at 1:0>")   # a position where ast gives one
    assert repr(code.parse(".", "nothing/*.py")) == "<Corpus: 0 files, 0 not Python, 0 nodes>"


def test_bytes_are_read_as_a_file_is_read():
    src = "# -*- coding: latin-1 -*-\nx = 'é'\n".encode("latin-1")
    assert code.parse_source(src).root.children[0].field("value").field("value") == "é"
    assert code.parse_source(b"\xef\xbb\xbfx = 1").dump() == "Module(body=[Assign(targets=[Name(id='x', ctx=Store())], value=Constant(value=1))])"


@pytest.mark.parametrize("source", ["a" + ".b" * 50_000, "+".join(["x"] * 50_000), "if a: pass\n" + "elif a: pass\n" * 20_000],
                         ids=["attribute chain", "operator chain", "elif chain"])
def test_a_tree_as_deep_as_its_source_chains_parses_and_dumps(source):
    """CPython parses these too (its own ast.dump then runs out of recursion); no stack is used per level here."""
    tree = code.parse_source(source)
    assert tree.dump().count("(") > 50_000


def test_nesting_past_a_thousand_levels_is_refused_not_a_crash():
    """CPython 3.14 reads this one (5,000 nested unary minuses): refusing past 1,000 is D44's
    difference, chosen so the recursive parser's stack stays bounded (code_parser.md)."""
    with pytest.raises(SyntaxError):
        code.parse_source("-" * 5_000 + "1")


# Where pygim's parse is not CPython 3.14's (code_parser.md, "Known differences"): each kept as it is,
# so a change to one is a decision, not a drift. A dump, or SyntaxError for a refusal.
NOT_CPYTHONS = {
    "an identifier is not NFKC-normalised": ("\ufb01 = 1", "Module(body=[Assign(targets=[Name(id='\ufb01', ctx=Store())], value=Constant(value=1))])"),
    "an unknown \\N{NAME} is kept as written": (r"x = '\N{NOT A NAME}'", r"Module(body=[Assign(targets=[Name(id='x', ctx=Store())], value=Constant(value='\\N{NOT A NAME}'))])"),
    "barry_as_FLUFL's <> is refused": ("from __future__ import barry_as_FLUFL\n1 <> 2", SyntaxError),
}


@pytest.mark.parametrize("source, expected", list(NOT_CPYTHONS.values()), ids=list(NOT_CPYTHONS))
def test_where_the_parse_is_not_cpythons_it_stays_as_documented(source, expected):
    if expected is SyntaxError:
        with pytest.raises(SyntaxError):
            code.parse_source(source)
    else:
        assert code.parse_source(source).dump() == expected


def test_a_decimal_past_4_300_digits_is_refused_as_cpythons_default_refuses_it():
    with pytest.raises(SyntaxError, match="4300 digits"):
        code.parse_source("x = " + "1" * 4_301)


# ── views ───────────────────────────────────────────────────────────────────
@pytest.fixture()
def tree():
    return code.parse_source("def f(a, b=2):\n    return a + b\n\nx: int = f(1)\n")


def test_a_node_has_its_kind_fields_and_position(tree):
    f = tree.root.children[0]
    assert (f.kind, f.fields[:3], f.lineno, f.col_offset, f.end_lineno, f.end_col_offset) == \
        ("FunctionDef", ["name", "args", "body"], 1, 0, 2, 16)


def test_a_field_holds_what_ast_holds(tree):
    f = tree.root.children[0]
    assert f.field("name") == "f"
    assert [a.field("arg") for a in f.field("args").field("args")] == ["a", "b"]
    assert f.field("args").field("defaults")[0].field("value") == 2
    assert f.field("returns") is None
    assert f.field("body")[0].field("value").field("op") == "Add"


def test_a_scalar_list_is_a_list_of_values():
    compare = code.parse_source("a < b == c").root.children[0].field("value")
    assert compare.field("ops") == ["Lt", "Eq"]
    assert code.parse_source("global a, b").root.children[0].field("names") == ["a", "b"]


def test_a_missing_dict_key_is_none():
    d = code.parse_source("{**a, 'k': v}").root.children[0].field("value")
    assert d.field("keys")[0] is None and d.field("keys")[1].field("value") == "k"


def test_parent_and_children_walk_the_tree(tree):
    ret = tree.root.children[0].field("body")[0]
    assert ret.parent.kind == "FunctionDef" and ret.parent.parent == tree.root and tree.root.parent is None
    assert [c.kind for c in ret.field("value").children] == ["Name", "Name"]


@pytest.mark.parametrize("source", ["{**a, 'k': v}", "def f(*, a, b=1): pass", "a < b"], ids=["dict", "kw defaults", "compare"])
def test_children_are_ast_iter_child_nodes(source):
    """A node's children are the nodes ast.iter_child_nodes gives — but for operators and contexts,
    which are field values here (`field("op") == "Add"`), and in source order where ast goes field by
    field: no placeholder of a missing dict key or keyword default. Found 2026-10-29: the None of
    `{**a}` came out as a child of kind "absent"."""
    values = (ast.expr_context, ast.boolop, ast.operator, ast.unaryop, ast.cmpop)
    kinds = lambda n: sorted(type(c).__name__ for c in ast.iter_child_nodes(n) if not isinstance(c, values))
    ours, theirs = code.parse_source(source).root.children[0], ast.parse(source).body[0]
    assert sorted(c.kind for c in ours.children) == kinds(theirs)
    for mine, cpythons in zip(sorted(ours.children, key=lambda c: c.kind), sorted(ast.iter_child_nodes(theirs), key=lambda c: type(c).__name__)):
        if not isinstance(cpythons, values):
            assert sorted(c.kind for c in mine.children) == kinds(cpythons)


def test_a_dicts_children_come_in_source_order():
    assert [c.kind for c in code.parse_source("{**a, 'k': v}").root.children[0].field("value").children] == ["Name", "Constant", "Name"]


def test_a_node_compared_with_anything_else_is_not_equal(tree):
    """== with a non-node is False, as for any Python object, so `node in [None, node]` works.
    Found 2026-10-29: it raised TypeError."""
    root = tree.root
    assert (root == None) is False and (root != 1) is True and root in [None, root]   # noqa: E711


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX names are bytes")
def test_a_file_named_past_utf8_is_named_as_os_fsdecode_names_it(temp_dir, write_named_past_utf8):
    """A POSIX file name need not be UTF-8: the corpus names it as os.fsdecode does (surrogate
    escapes), its files, its errors and its trees, and finds it by that name. Found 2026-10-29: the
    path was handed back as strict UTF-8, and reading it raised UnicodeDecodeError."""
    root = os.fsencode(temp_dir)
    good = write_named_past_utf8(os.path.join(root, b"\xff.py"), b"x = 1\n")
    bad = write_named_past_utf8(os.path.join(root, b"\xfe.py"), b"x = (\n")
    corpus = code.parse(os.fsdecode(root))
    assert sorted(corpus.files) == sorted([os.fsdecode(good), os.fsdecode(bad)])
    assert [e[0] for e in corpus.errors] == [os.fsdecode(bad)]
    assert corpus[os.fsdecode(good)].path == os.fsdecode(good)


def test_find_lists_a_kind_in_source_order(tree):
    assert [n.field("id") for n in tree.find("Name")] == ["a", "b", "x", "int", "f"]
    assert tree.kind_counts()["Name"] == 5


@pytest.mark.parametrize("name", ["Nmae", "name", "identifier", "cmpop", ""])
def test_find_of_a_name_no_node_kind_has_is_a_value_error(tree, name):
    with pytest.raises(ValueError, match="no node kind is named"):
        tree.find(name)


def test_parse_source_lets_other_threads_run_while_it_parses():
    """The parse runs with the GIL released: a thread woken as it starts runs before it ends."""
    source = "x = [a + b for a in c if a]\n" * 100_000
    woke = []
    go = threading.Event()
    helper = threading.Thread(target=lambda: (go.wait(), woke.append(time.perf_counter())))
    helper.start()
    interval = sys.getswitchinterval()
    sys.setswitchinterval(1.0)   # the helper gets the GIL only when the parse lets it go
    try:
        go.set()
        code.parse_source(source)
        ended = time.perf_counter()
    finally:
        sys.setswitchinterval(interval)
        helper.join()
    assert woke[0] < ended


def test_a_tree_counts_its_nodes_and_keeps_operators_and_contexts_as_fields(tree):
    # Module, FunctionDef, arguments, arg a, arg b, 2, Return, BinOp, a, b, AnnAssign, x, int, Call, f, 1:
    # ast.walk would add Load, Store and Add, which a tree keeps as fields of their nodes.
    assert len(tree) == 16


@pytest.mark.parametrize("source, rows", [("a < b", ["Module", "Expr", "Compare", "Name", "cmpop", "Name"]),
                                          ("global x, y", ["Module", "Global", "identifier", "identifier"]),
                                          ("{**d}", ["Module", "Expr", "Dict", "absent", "Name"])])
def test_a_field_value_kept_as_a_row_is_counted_with_the_nodes(source, rows):
    """A comparison's operators, a list of names and a missing dict key are rows of their own, as
    node() reads them: len counts rows, kind_counts() only nodes."""
    t = code.parse_source(source)
    assert [t.node(i).kind for i in range(len(t))] == rows


def test_a_node_shows_its_source(tree):
    assert tree.find("AnnAssign")[0].source == "x: int = f(1)"


def test_an_unknown_field_is_an_attribute_error(tree):
    with pytest.raises(AttributeError):
        tree.root.field("nope")


def test_constants_are_their_python_values():
    values = code.parse_source("1, 1.5, 2j, 'a', b'b', None, True, ..., 0xff").root.children[0].field("value").field("elts")
    assert [v.field("value") for v in values] == [1, 1.5, 2j, "a", b"b", None, True, ..., 255]


# ── the corpus ──────────────────────────────────────────────────────────────
@pytest.fixture()
def project(temp_dir):
    for name, text in {"a.py": "import os\n", "pkg/b.py": "def f(): pass\n", "pkg/c.py": "def (:\n",
                       "pkg/sub/d.py": "x = 1\n", "notes.txt": "not code", "pkg/e.py/f.py": "y = 2\n"}.items():
        p = temp_dir / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    return temp_dir


def test_a_corpus_holds_every_match_sorted(project):
    corpus = code.parse(project)
    assert [Path(os.path.relpath(p, project)).as_posix() for p in corpus.files] == ["a.py", "pkg/b.py", "pkg/c.py", "pkg/e.py/f.py", "pkg/sub/d.py"]


def test_a_file_that_is_not_python_is_listed_with_its_place(project):
    (path, message, line, column), = code.parse(project).errors
    assert (Path(os.path.relpath(path, project)).as_posix(), line) == ("pkg/c.py", 1)


def test_a_failed_file_raises_its_syntax_error_on_request(project):
    corpus = code.parse(project)
    with pytest.raises(SyntaxError) as raised:
        corpus[str(project / "pkg" / "c.py")]
    assert raised.value.filename == str(project / "pkg" / "c.py")


def test_iteration_gives_the_trees_that_parsed(project):
    assert [t.root.children[0].kind for t in code.parse(project)] == ["Import", "FunctionDef", "Assign", "Assign"]


@pytest.mark.parametrize("threads", [None, 1, 4])
def test_the_trees_are_the_same_at_every_width(project, threads):
    corpus = code.parse(project, threads=threads)
    assert [t.dump() for t in corpus] == [dump314(ast.parse(Path(t.path).read_bytes())) for t in corpus]


def test_the_stats_count_the_run(project):
    stats = code.parse(project, "**/*.py").stats()
    assert (stats["files"], stats["failed"], stats["nodes"]) == (5, 1, 3 + 4 + 4 + 4)   # a, b, d, f; c failed


# A file of each shape a read lands differently: the text inside the string object or past it, a
# quarter chunk and past a chunk (utils/chunk_arena.h), decoded to more bytes than were read, line
# ends of every kind, and each way a file is refused.
FILE_SHAPES = {
    "empty": b"",
    "fifteen bytes": b"x = " + b"1" * 10 + b"\n",
    "sixteen bytes": b"x = " + b"1" * 11 + b"\n",
    "a byte-order mark": b"\xef\xbb\xbfx = 1\n",
    "latin-1, an \u00e9 two bytes": "# -*- coding: latin-1 -*-\nx = '\u00e9'\n".encode("latin-1"),
    "cp1252, a \u20ac three bytes": "# coding: cp1252\nx = '\u20ac\u20ac\u20ac'\n".encode("cp1252"),
    "latin-1 decoded past a quarter chunk": ("# coding: latin-1\nx = '" + "\u00e9" * 600_000 + "'\n").encode("latin-1"),
    "\\r\\n and a lone \\r": b"a = 1\r\nb = 2\rc = 3\r\n",
    "a quarter chunk": b'x = "' + b"a" * (1 << 20) + b'"\n',
    "past a chunk": b'x = "' + b"a" * (4 << 20) + b'"\n',
    "not Python": b"x = = 1\n",
    "not decodable": b"# coding: ascii\nx = '\xe9'\n",
    "a NUL": b"x = 1\0\n",
}


def outcome(parse):
    """What a parse gives: the tree's dump with its positions, or the error's message and place."""
    try:
        return parse().dump(include_attributes=True)
    except SyntaxError as e:
        return e.msg, e.lineno, e.offset


@pytest.mark.parametrize("shape", FILE_SHAPES)
def test_a_corpus_reads_a_file_as_parse_source_reads_its_bytes(temp_dir, shape):
    (temp_dir / "m.py").write_bytes(FILE_SHAPES[shape])
    corpus = code.parse(temp_dir)
    assert outcome(lambda: corpus[str(temp_dir / "m.py")]) == outcome(lambda: code.parse_source(FILE_SHAPES[shape]))


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="/proc is Linux's")
def test_a_corpus_reads_a_file_that_reports_no_size_to_its_end():
    """fstat says 0 for a /proc file: the read goes on until the file ends."""
    corpus = code.parse("/proc/self", "comm")
    assert corpus.stats()["bytes"] == len(Path("/proc/self/comm").read_bytes())
    assert outcome(lambda: corpus["/proc/self/comm"]) == outcome(lambda: code.parse_source(Path("/proc/self/comm").read_bytes()))


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="/proc is Linux's")
def test_a_corpus_reads_a_file_that_reports_no_size_past_its_first_page():
    """A Python process's maps is longer than the page the read starts with. It changes as memory
    is mapped; its first line, where the parse stops, does not."""
    corpus = code.parse("/proc/self", "maps")
    assert corpus.stats()["bytes"] > 4096
    assert outcome(lambda: corpus["/proc/self/maps"]) == outcome(lambda: code.parse_source(Path("/proc/self/maps").read_bytes()))


def test_names_are_shared_across_files(project):
    corpus = code.parse(project)
    assert corpus.stats()["names"] == len({"os", "f", "x", "y"})


@pytest.fixture()
def many_files(temp_dir):
    """120 files of 40 functions: enough that four workers each parse some (four, every run measured)."""
    for i in range(120):
        (temp_dir / f"m{i}.py").write_text("".join(f"def f{i}_{j}(shared, own_{i}_{j}):\n    return shared.attr + own_{i}_{j}\n" for j in range(40)))
    return temp_dir


def test_names_merged_from_many_workers_are_each_name_once(many_files):
    """Each worker's names are merged into one table on as many cores (corpus.h, merged_names)."""
    assert code.parse(many_files, threads=4).stats()["names"] == 120 * 40 * 2 + len({"shared", "attr"})


def test_names_merged_from_many_workers_read_as_one_worker_reads_them(many_files):
    assert [t.dump() for t in code.parse(many_files, threads=4)] == [t.dump() for t in code.parse(many_files, threads=1)]


def test_threads_zero_is_a_value_error(project):
    with pytest.raises(ValueError, match="None for one per core"):   # parsing computes: None is every core (Q39)
        code.parse(project, threads=0)


def test_an_absolute_pattern_is_a_value_error(project):
    with pytest.raises(ValueError):
        code.parse(project, "/etc/*.py")


CHILD = r"""
import os, pathlib, signal, sys, threading, time
from pygim import code

root = pathlib.Path(sys.argv[1])
body = "".join(f"def f{i}(a, b=1, *c, **d):\n    return [x * {i} for x in a if x]\n" for i in range(200))
for d in range(800):
    p = root / f"p{d % 20}" / f"m{d}.py"
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(body)
tasks = lambda: len(os.listdir("/proc/self/task")) if os.path.isdir("/proc/self/task") else 0
t = time.perf_counter()
code.parse(root)
full = time.perf_counter() - t
before = tasks()
sys.setswitchinterval(1.0)   # the helper gets the GIL only once parse has let it go
go = threading.Event()
helper = threading.Thread(target=lambda: (go.wait(), os.kill(os.getpid(), signal.SIGINT)))
helper.start()
go.set()
t = time.perf_counter()
try:
    code.parse(root)
    print("completed")
except KeyboardInterrupt:
    print("KeyboardInterrupt")
took = time.perf_counter() - t
helper.join()
print("stopped early", took < full / 2, f"({took * 1000:.1f} of {full * 1000:.1f} ms)")
print("threads left", tasks() - before)
"""


@pytest.mark.skipif(sys.platform == "win32", reason="sends a POSIX signal to a child process")
def test_ctrl_c_stops_a_parse(temp_dir):
    done = subprocess.run([sys.executable, "-c", CHILD, str(temp_dir)], capture_output=True, text=True, timeout=120)
    lines = done.stdout.splitlines()
    assert lines and lines[0] == "KeyboardInterrupt", done.stdout + done.stderr
    if "thread" not in os.environ.get("PYGIM_SANITIZED", ""):   # ThreadSanitizer holds the signal back (run_tests_under_sanitizers.py)
        assert "stopped early True" in done.stdout, done.stdout
    if os.path.isdir("/proc/self/task"):
        assert "threads left 0" in done.stdout, done.stdout


# ── the generated tables ────────────────────────────────────────────────────
@pytest.mark.skipif(shutil.which("python3.14") is None, reason="the tables describe Python 3.14's ast module")
@pytest.mark.skipif(not SRC.is_dir(), reason="source tree not present (testing the installed package)")
@pytest.mark.parametrize("generator, header", [("gen_ast_tables.py", "ast_tables.gen.h"), ("gen_unicode_tables.py", "unicode_tables.gen.h"),
                                               ("gen_codec_tables.py", "codec_tables.gen.h")])
def test_the_generated_tables_are_python_3_14s(generator, header):
    fresh = subprocess.run(["python3.14", str(SRC / generator), "--print"], capture_output=True, text=True, check=True).stdout
    assert fresh == (SRC / header).read_text(encoding="utf-8"), f"stale: run python3.14 {SRC / generator}"


# ── the stub ────────────────────────────────────────────────────────────────
STUBBED_CLASSES = ("Corpus", "Tree", "Node")
# What pybind11 gives every class, or what the stub leaves to object's (repr) and to `...` (hash).
NOT_STUBBED = {"__init__", "__new__", "__repr__", "__module__", "__doc__", "__dict__", "__weakref__", "_pybind11_conduit_v1_"}


class TestStub:
    """src/pygim/code.pyi is what an editor and a type checker see of the module: every class and
    function, each member with the binding's parameters, and the binding's docstring."""

    STUB = Stub(code)

    def test_it_names_every_class_and_function_of_the_module(self):
        assert {n for n in vars(code) if not n.startswith("_")} <= self.STUB.names()

    @pytest.mark.parametrize("cls", STUBBED_CLASSES)
    def test_every_member_is_stubbed(self, cls):
        assert sorted(set(vars(getattr(code, cls))) - NOT_STUBBED - set(self.STUB.methods(cls))) == []

    @pytest.mark.parametrize("name", ["parse", "parse_source"])
    def test_each_function_takes_the_bindings_parameters_and_docstring(self, name):
        stubbed, bound = self.STUB.function(name), getattr(code, name)
        assert Stub.params(stubbed, method=False) == Stub.binding_params(bound)
        assert Stub.same_text(ast.get_docstring(stubbed), Stub.binding_doc(bound)), Stub.binding_doc(bound)

    @pytest.mark.parametrize("cls, name", [(c, n) for c in STUBBED_CLASSES for n in vars(getattr(code, c)) if n not in NOT_STUBBED and n != "__hash__"])
    def test_each_member_takes_the_bindings_parameters_and_docstring(self, cls, name):
        stubbed, bound = self.STUB.methods(cls)[name], vars(getattr(code, cls))[name]
        if not isinstance(bound, property):
            assert Stub.params(stubbed) == Stub.binding_params(bound)
        assert Stub.same_text(ast.get_docstring(stubbed), Stub.binding_doc(bound)), Stub.binding_doc(bound)

    @pytest.mark.parametrize("cls", STUBBED_CLASSES)
    def test_each_class_docstring_is_the_bindings(self, cls):
        assert Stub.same_text(ast.get_docstring(self.STUB.cls(cls)), getattr(code, cls).__doc__)

