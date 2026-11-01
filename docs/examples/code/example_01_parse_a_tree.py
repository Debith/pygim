# type: ignore
"""Python source parsed in C++: ``code.parse()`` for a whole tree, ``code.parse_source()`` for one.

``code.parse(root)`` walks ``root``, reads every file of a language it parses (``code.LANGUAGES``:
Python's ``.py`` and ``.pyi``) and parses it as Python 3.14 —
all of it in C++, one thread per core, with the GIL released. What comes back is a ``Corpus``:
the files sorted by path, each a syntax tree or the reason it is not one. A ``Tree`` and its
``Node`` objects are views: no Python object exists per node until you ask for one, and a
field's value is made when you read it.

The node kinds, their fields and their positions are CPython's own: ``tree.dump()`` is exactly
what ``ast.dump(ast.parse(source))`` writes in Python 3.14.

This example demonstrates:
- Parsing a tree, and what the corpus says of the run
- A file that is not Python: listed as its SyntaxError, raised on request
- Reading a tree: kinds, fields, positions, the source a node spans
- A node compared with the node a test expects: ``code.like``
- dump(), and parsing one source
- A file's encoding, as its coding cookie declares it
"""

import tempfile

from pygim import code, path

with tempfile.TemporaryDirectory() as tmp:
    root = path(tmp)
    (root / "pkg").mkdir()
    (root / "pkg" / "shapes.py").write_text(
        "from dataclasses import dataclass\n"
        "\n"
        "@dataclass\n"
        "class Point:\n"
        "    x: float\n"
        "    y: float = 0.0\n"
        "\n"
        "    def scaled(self, k):\n"
        "        return Point(self.x * k, self.y * k)\n"
    )
    (root / "pkg" / "broken.py").write_text("def broken(:\n    pass\n")
    (root / "notes.txt").write_text("not Python, and not a match")

    # ------------------------------------------------------------------------
    # 1. One call parses the tree
    # ------------------------------------------------------------------------
    #                         ┌─ threads: None is one per core; n fixes it
    #                         ▼
    corpus = code.parse(root, threads=None)            # every .py and .pyi beneath root
    assert [p.name for p in corpus.files] == ["broken.py", "shapes.py"]   # a PathSet, by path
    stats = corpus.stats()
    assert stats["files"] == 2
    assert stats["failed"] == 1

    # ------------------------------------------------------------------------
    # 2. A file that is not Python is a result too
    # ------------------------------------------------------------------------
    error, = corpus.errors                             # each file that is not Python
    assert isinstance(error, SyntaxError)              # as an object: the error it raises
    assert path(error.filename).name == "broken.py"
    assert error.lineno == 1
    try:
        corpus[error.filename]                         # asking for its tree raises that error
        raise AssertionError("a file that is not Python has no tree")
    except SyntaxError as e:
        assert (e.filename, e.lineno) == (error.filename, error.lineno)

    # ------------------------------------------------------------------------
    # 3. Reading a tree
    # ------------------------------------------------------------------------
    tree = corpus[root / "pkg" / "shapes.py"]          # by its path, or by its index
    assert tree.path == root / "pkg" / "shapes.py"     # one file is a pygim path
    #               ┌─ ast's own names: the kind, then the fields in _fields order
    #               ▼
    cls = tree.find("ClassDef")[0]
    assert cls.kind == "ClassDef"
    assert cls.field("name") == "Point"                # a field's value, as ast holds it
    assert cls.fields[:2] == ["name", "bases"]
    ret = tree.find("Return")[0]
    assert ret.source == "return Point(self.x * k, self.y * k)"     # the text a node spans
    try:
        tree.find("Nmae")                              # a kind ast does not have
        raise AssertionError("an unknown kind is an error, not an empty list")
    except ValueError:
        pass

    # ------------------------------------------------------------------------
    # 4. A node compared with the node a test expects
    # ------------------------------------------------------------------------
    #                       ┌─ kind: then only the fields and positions to compare
    #                       ▼
    assert cls == code.like("ClassDef", name="Point", lineno=4)   # decorated: it starts at `class`
    assert cls == code.like("ClassDef", decorator_list=[code.like("Name", id="dataclass")])
    annotated = [code.like("AnnAssign", target=code.like("Name", id="x")),
                 code.like("AnnAssign", target=code.like("Name", id="y"))]
    assert cls.field("body")[:2] == annotated          # a list, item by item
    times = code.like("BinOp", op="Mult")              # operators and contexts by name
    assert ret == code.like("Return", value=code.like("Call", args=[times, times]))
    assert ret.parent == code.like("FunctionDef", name="scaled")
    assert cls != code.like("ClassDef", name="Line")

# ----------------------------------------------------------------------------
# 5. dump() is ast.dump; parse_source() parses one text
# ----------------------------------------------------------------------------
one = code.parse_source("x = f'{y!r:>8}'")
assert one.dump() == (
    "Module(body=[Assign(targets=[Name(id='x', ctx=Store())], value=JoinedStr(values=["
    "FormattedValue(value=Name(id='y', ctx=Load()), conversion=114, format_spec=JoinedStr("
    "values=[Constant(value='>8')]))]))])"
)

# ----------------------------------------------------------------------------
# 6. Bytes are read as a file is: in the encoding its cookie declares
# ----------------------------------------------------------------------------
euro = code.parse_source(b"# coding: cp1252\nx = '\x80'\n")    # cp1252's 0x80 is the euro sign
assert euro.root.children[0].field("value").field("value") == "€"
try:
    code.parse_source(b"# coding: no-such-codec\nx = 1\n")
    raise AssertionError("an encoding Python does not know is refused")
except SyntaxError as e:
    assert e.msg == "unknown encoding: no-such-codec"     # as CPython says it

print(f"code example OK: {stats['nodes']} nodes in {stats['files']} files, "
      f"{len(tree)} rows in shapes.py")
