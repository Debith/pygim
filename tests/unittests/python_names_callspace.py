# -*- coding: utf-8 -*-
"""The call space of ``pygim::python_names`` (``src/_pygim_fast/utils/python_names.h``), as data.

Every function there takes ``std::string_view`` arguments, so a domain is chosen by the parameter's
*role* — the name the C++ declaration gives it, which GCC reflection reports — not by its type:
``class_name``, ``from`` and ``to`` take class names; ``name`` takes names; ``qualname`` takes
qualified names. What is manual and what is generated:

- manual: the class-name atoms, the stems of a name, the bound on underscore runs (0..3), the two
  owners a spelt name is made for, the qualified-name templates and the unowned qualified names;
- generated: every name ``leading underscores + stem + trailing underscores``, every name spelt for
  an owner (``_Owner`` + a name of two leading underscores), every owned qualified name, and every
  call — the full product of each function's role domains (no reduction).

The expected results come from Python itself, never from pygim: ``enum._is_dunder`` for a dunder,
and Python's compiler for everything mangling decides — the name compiled inside ``class <C>:`` (as
``self.<name>``, or ``import <name>`` for a dotted one) and the qualified name of a template compiled
there. Where the compiler cannot be asked — a name or a class name that is not an identifier, a
qualified name its class does not own — the written contract in the header's comments answers, and
the case says so (``oracle``).

Three consumers read this one model: ``tests/static/run_python_names_callspace.py`` (GCC 16
reflection: the functions and parameters discovered, the product called natively, gcov, mutants),
the compile-time proof it writes (``tests/static/python_names_callspace_proofs.cpp``, compiled into
every build), and ``docs/design/traits_cases.md``.

Run ``python tests/unittests/python_names_callspace.py`` to rewrite the proof; ``--check`` says
whether it is current.
"""

from __future__ import annotations

import enum
import functools
import keyword
import sys
import types
from pathlib import Path

from callspace import Atoms, Either, Grammar, Model, Parameter, generate

PROOF = Path(__file__).resolve().parents[1] / "static" / "python_names_callspace_proofs.cpp"

# ── the domains ─────────────────────────────────────────────────────────────────────────────────

CLASS_NAMES = Atoms(
    "class names",
    ("Trait", "Target", "_Trait", "__Trait", "Trait__", "__", ""),
    why="a plain name and a second one to move to; one and two leading underscores, which mangling "
        "drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the "
        "empty string, which is no class name at all",
)
UNDERSCORE_RUNS = Atoms("underscore runs", (0, 1, 2, 3),
                        why="the rules look at two underscores at each end, so one more on each side")
STEMS = Atoms("stems", ("", "x", "pkg.mod"),
              why="none, a letter, and a dotted name (only an import of a package has one) longer than "
                  "`_Trait`, so a long name without a leading underscore is asked for an owner's prefix")
SHAPED_NAMES = Grammar(
    "shaped names", (("leading", UNDERSCORE_RUNS), ("stem", STEMS), ("trailing", UNDERSCORE_RUNS)),
    make=lambda leading, stem, trailing: "_" * leading + stem + "_" * trailing,
    why="every underscore shape around every stem",
)
PRIVATE_BODIES = Grammar(
    "bodies of a spelt name", (("stem", STEMS), ("trailing", UNDERSCORE_RUNS)),
    make=lambda stem, trailing: "__" + stem + "_" * trailing,
    why="what follows the owner in `_Owner__x`: two underscores, a stem, a trailing run",
)
SPELT_NAMES = Grammar(
    "names spelt for an owner",
    (("owner", Atoms("owners", ("Trait", "Target"), why="the plain class names")), ("body", PRIVATE_BODIES)),
    make=lambda owner, body: f"_{owner}{body}",
    why="how a private name reads after mangling, for this class and for another",
)
NAMES = Either("names", (SHAPED_NAMES, SPELT_NAMES), why="shaped names, and names already spelt for an owner")

# A qualified-name template: its tail after the owner, and the source that compiles to it.
QUALNAME_TEMPLATES = {
    "f": "def f(self):\n    pass",
    "f.<locals>.<lambda>": "def f(self):\n    return lambda: 0",
    "f.<locals>.g": "def f(self):\n    def g():\n        pass",
    "Inner.f": "class Inner:\n    def f(self):\n        pass",
}
QUALNAME_TAILS = Atoms("qualified-name tails", tuple(QUALNAME_TEMPLATES),
                       why="a method, a lambda and a function inside it, and a nested class's method")
OWNED_QUALNAMES = Grammar(
    "owned qualified names", (("owner", CLASS_NAMES), ("tail", QUALNAME_TAILS)),
    make=lambda owner, tail: f"{owner}.{tail}", why="each template, inside each class",
)
UNOWNED_QUALNAMES = Atoms(
    "unowned qualified names", ("f", "TraitX.f", "Trait", "Trait.", ""),
    why="a module-level name; another class whose name begins with an owner's; a bare owner; an owner "
        "and a dot with nothing after; nothing",
)
QUALNAMES = Either("qualified names", (OWNED_QUALNAMES, UNOWNED_QUALNAMES), why="owned and unowned")

ROLES = {"class_name": CLASS_NAMES, "from": CLASS_NAMES, "to": CLASS_NAMES, "name": NAMES, "qualname": QUALNAMES}

# The functions the model covers: their parameters, by the C++ names, and the kind of their result.
# The reflection probe reports the same from the declarations; a difference is schema drift.
FUNCTIONS = {
    "is_dunder": (("name",), "bool"),
    "owner_of": (("class_name",), "text"),
    "is_private_name": (("name",), "bool"),
    "mangled": (("class_name", "name"), "text"),
    "unmangled": (("class_name", "name"), "text"),
    "respelt": (("from", "to", "name"), "optional text"),
    "reowned": (("from", "to", "name"), "text"),
    "requalified": (("from", "to", "qualname"), "text"),
}

MODELS = {
    function: Model(name=f"pygim::python_names::{function}", call=f"{function}({', '.join(parameters)})",
                    parameters=tuple(Parameter(p, ROLES[p]) for p in parameters))
    for function, (parameters, _) in FUNCTIONS.items()
}

# ── the oracle: Python's compiler and enum; the written contract only where they cannot answer ──


def _identifier(text):
    return text.isidentifier() and not keyword.iskeyword(text)


def _importable(text):
    return bool(text) and all(_identifier(part) for part in text.split("."))


def _codes(code):
    """The code objects nested in `code`, depth-first."""
    for constant in code.co_consts:
        if isinstance(constant, types.CodeType):
            yield constant
            yield from _codes(constant)


@functools.lru_cache(maxsize=None)
def compiler_spelling(class_name, name):
    """How Python's compiler spells `name` inside ``class <class_name>:`` — or None when it cannot be
    asked (either is not an identifier; a dotted name's part is not)."""
    if not _identifier(class_name) or not _importable(name):
        return None
    if "." in name:  # only `import a.b` writes a dotted name, and it writes it whole
        body = next(_codes(compile(f"class {class_name}:\n    import {name}\n", "<oracle>", "exec", dont_inherit=True)))
        return next(n for n in body.co_names if "." in n)
    module = compile(f"class {class_name}:\n    def f(self):\n        return self.{name}\n", "<oracle>", "exec",
                     dont_inherit=True)
    (spelling,) = list(_codes(module))[1].co_names
    return spelling


@functools.lru_cache(maxsize=None)
def compiler_qualname(class_name, tail):
    """The qualified name of the innermost code in template `tail`, compiled in ``class <class_name>:``
    — or None when it cannot be asked (not an identifier, not a template, Python before 3.11)."""
    if not _identifier(class_name) or tail not in QUALNAME_TEMPLATES or sys.version_info < (3, 11):
        return None
    indented = "\n".join("    " + line for line in QUALNAME_TEMPLATES[tail].splitlines())
    codes = list(_codes(compile(f"class {class_name}:\n{indented}\n", "<oracle>", "exec", dont_inherit=True)))
    return codes[-1].co_qualname


def _owner(class_name):
    spelling = compiler_spelling(class_name, "__o")
    if spelling is None:
        return class_name.lstrip("_"), "contract"  # "the name without its leading underscores"
    return ("" if spelling == "__o" else spelling[1:-len("__o")]), "compiler"


def _private(name):
    spelling = compiler_spelling("C", name)
    if spelling is None:  # "starts with two underscores, does not end with two, has no dot"
        return name.startswith("__") and not name.endswith("__") and "." not in name, "contract"
    return spelling != name, "compiler"


def _mangled(class_name, name):
    spelling = compiler_spelling(class_name, name)
    if spelling is not None:
        return spelling, "compiler"
    (owner, a), (private, b) = _owner(class_name), _private(name)
    return (f"_{owner}{name}" if private and owner else name), _joined(a, b, "contract")


def _unmangled(class_name, name):
    """The private name whose spelling in `class_name` is `name`: searched among `name`'s suffixes,
    the compiler spelling each — the inverse of mangling, by its definition."""
    sources = set()
    for start in range(len(name)):
        candidate = name[start:]
        if not candidate.startswith("__"):
            continue
        private, a = _private(candidate)
        sources.add(a)
        if private:
            spelling, b = _mangled(class_name, candidate)
            sources.add(b)
            if spelling == name:
                return candidate, _joined(*sources)
    return "", _joined(*sources) if sources else "compiler"


def _respelt(source, target, name):
    private, a = _unmangled(source, name)
    if not private:
        return None, a
    spelling, b = _mangled(target, private)
    return spelling, _joined(a, b)


def _requalified(source, target, qualname):
    if not (len(qualname) > len(source) and qualname.startswith(source) and qualname[len(source)] == "."):
        return qualname, "contract"  # "a qualified name that does not start with `from` and a dot"
    tail = qualname[len(source) + 1:]
    compiled = compiler_qualname(target, tail)
    if compiled is not None and compiler_qualname(source, tail) == qualname:
        return compiled, "compiler"
    return target + qualname[len(source):], "contract"


def _joined(*sources):
    """The oracles behind a result made from several: "compiler", "contract" or "compiler+contract"."""
    return "+".join(sorted({part for source in sources for part in source.split("+")}))


def expected(function, arguments):
    """(the expected result, the oracle that gave it) for one call."""
    return _expected(function, tuple(sorted(arguments.items())))


@functools.lru_cache(maxsize=None)
def _expected(function, items):
    a = dict(items)
    if function == "is_dunder":
        return enum._is_dunder(a["name"]), "enum"
    if function == "owner_of":
        return _owner(a["class_name"])
    if function == "is_private_name":
        return _private(a["name"])
    if function == "mangled":
        return _mangled(a["class_name"], a["name"])
    if function == "unmangled":
        return _unmangled(a["class_name"], a["name"])
    if function == "respelt":
        return _respelt(a["from"], a["to"], a["name"])
    if function == "reowned":
        moved, source = _respelt(a["from"], a["to"], a["name"])
        return (a["name"] if moved is None else moved), source
    if function == "requalified":
        return _requalified(a["from"], a["to"], a["qualname"])
    raise KeyError(function)


@functools.lru_cache(maxsize=None)
def cases():
    """Every generated call of every function, with its expected result: {function: [record]}; made
    once per process (the records are read, never changed)."""
    return {function: [{"id": case.id, "arguments": case.arguments, "expected": result, "oracle": oracle}
                       for case in generate(model)
                       for result, oracle in [expected(function, case.arguments)]]
            for function, model in MODELS.items()}


# ── the compile-time proof: the same product, expanded by C++ loops, against these expectations ─


def _cpp(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_proof():
    """tests/static/python_names_callspace_proofs.cpp: each function's role domains as arrays, the
    product expanded by nested loops in parameter order, and each call's expected result — from the
    oracle above — at the call's position in that order."""
    all_cases = cases()
    roles = {"class_names": CLASS_NAMES.expand(), "names": NAMES.expand(), "qualnames": QUALNAMES.expand()}
    role_array = {"class_name": "class_names", "from": "class_names", "to": "class_names",
                  "name": "names", "qualname": "qualnames"}
    texts = sorted({r["expected"] for records in all_cases.values() for r in records
                    if isinstance(r["expected"], str)})
    index = {text: i for i, text in enumerate(texts)}
    lines = [
        "// GENERATED by tests/unittests/python_names_callspace.py — do not edit.",
        "//",
        "// The full call space of every function in utils/python_names.h, proven at compile time in every",
        "// build (ext.utils.toml). The domains below are the model's, generated from its declared atoms",
        "// and bounds; the loops expand each function's product in parameter order; each call's expected",
        "// result was computed by Python's compiler (or enum._is_dunder, or the written contract where the",
        "// compiler cannot be asked) and is stored at the call's position. A failing static_assert names",
        "// the position: the first call whose result differs. No runtime code.",
        "",
        '#include "../../src/_pygim_fast/utils/python_names.h"',
        '#include "case_table.h"',
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <optional>",
        "#include <string_view>",
        "",
        "namespace {",
        "",
        "using namespace pygim::python_names;",
        "using pygim::proofs::every_row_holds;",
        "",
    ]
    for array, values in roles.items():
        lines.append(f"constexpr std::array<std::string_view, {len(values)}> {array}{{")
        lines += [f"    {_cpp(v)}," for v in values]
        lines += ["};", ""]
    lines.append(f"// Every expected text result, once; a call's expectation is an index here ({len(texts)} texts).")
    lines.append(f"constexpr std::array<std::string_view, {len(texts)}> texts{{")
    lines += [f"    {_cpp(t)}," for t in texts]
    lines += ["};", "", "constexpr std::int16_t none = -1;  // respelt(): nothing", ""]

    for function, (parameters, kind) in FUNCTIONS.items():
        records = all_cases[function]
        if kind == "bool":
            values = ["true" if r["expected"] else "false" for r in records]
            element = "bool"
        else:
            values = [str(none_or(index, r["expected"])) for r in records]
            element = "std::int16_t"
        lines.append(f"constexpr std::array<{element}, {len(records)}> {function}_expected{{")
        for start in range(0, len(values), 32):
            lines.append("    " + ", ".join(values[start:start + 32]) + ",")
        lines += ["};", ""]
        arrays = [role_array[p] for p in parameters]
        loops = []
        indent = "    "
        lines.append(f"consteval std::size_t first_failing_{function}_call() {{")
        lines.append(f"{indent}std::size_t at = 0;")
        for depth, (p, array) in enumerate(zip(parameters, arrays)):
            loops.append(f"{indent * (depth + 1)}for (const std::string_view {_param(p)} : {array}) {{")
        lines += loops
        inner = indent * (len(parameters) + 1)
        call = f"{function}({', '.join(_param(p) for p in parameters)})"
        if kind == "bool":
            lines.append(f"{inner}if ({call} != {function}_expected[at]) return at;")
        elif kind == "text":
            lines.append(f"{inner}if ({call} != texts[{function}_expected[at]]) return at;")
        else:
            lines.append(f"{inner}const auto result = {call};")
            lines.append(f"{inner}const auto want = {function}_expected[at];")
            lines.append(f"{inner}if (want == none ? result.has_value() : (!result || *result != texts[want])) return at;")
        lines.append(f"{inner}++at;")
        for depth in reversed(range(len(parameters))):
            lines.append(f"{indent * (depth + 1)}}}")
        lines.append(f"{indent}return at == {function}_expected.size() ? every_row_holds : at;")
        lines += ["}", f"static_assert(first_failing_{function}_call() == every_row_holds);", ""]
    lines += ["}  // namespace", ""]
    return "\n".join(lines)


def none_or(index, value):
    return -1 if value is None else index[value]


def _param(name):
    return {"from": "source", "to": "target"}.get(name, name)


def main(argv):
    text = render_proof()
    if "--check" in argv:
        current = PROOF.read_text(encoding="utf-8") if PROOF.exists() else ""
        print("current" if current == text else f"stale: run python {Path(__file__).name}")
        return 0 if current == text else 1
    PROOF.write_text(text, encoding="utf-8")
    print(f"written: {PROOF}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
