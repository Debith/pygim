#!/usr/bin/env python3.14
"""Generate ast_tables.gen.h — the node kinds of Python's syntax tree and their fields — from the
running Python's `ast` module, its public classes and their `_fields` / `_field_types` (D54).

The tree pygim parses has CPython's node kinds and field order, so `dump()` reads as `ast.dump`
and every field the grammar fills has a place. Nothing is copied from CPython's sources: the
module's public interface is the reference, and the Python that runs this script is the version
the tables describe. Run it with the Python whose grammar the parser targets:

    python3.14 src/_pygim_fast/code/python/gen_ast_tables.py

tests/unittests/test_code_parse.py asserts the committed header is what python3.14 generates.

Where each field's value lives in a row (rows.h): a node is a child row whose `field` is the
slot; the first identifier (or literal) of a kind is the row's `value`; one operator, context,
flag or small integer is the row's `small`; anything more — a second identifier, a list of
identifiers, the operators of a comparison — is a leaf row of a scalar kind (`identifier`,
`cmpop`), and a None inside a list of nodes (Dict.keys, arguments.kw_defaults) is an `absent` row.
"""

from __future__ import annotations

import ast
import sys
import types
import typing
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "ast_tables.gen.h"

# The abstract classes whose concrete subclasses are singletons stored in a row's `small`.
ENUM_FAMILIES = ["expr_context", "boolop", "operator", "unaryop", "cmpop"]
# Fields that are never filled without type comments; dump shows None as nothing.
ALWAYS_NONE = {"type_comment"}


def concrete_classes():
    """Every concrete node class in definition order, with its abstract family (or None)."""
    out = []

    def walk(cls, family):
        for sub in cls.__subclasses__():
            if sub.__module__ != "ast" or getattr(sub, "_field_types", None) is None:
                if sub.__module__ == "ast" and sub.__subclasses__() and getattr(sub, "_field_types", None) is None:
                    if sub.__name__ in ("mod", "stmt", "expr", "excepthandler", "pattern", "type_ignore",
                                        "type_param", *ENUM_FAMILIES):
                        walk(sub, sub.__name__)
                continue
            out.append((sub, family))
    walk(ast.AST, None)
    return out


def shape_of(tp):
    """(shape, family) of a field's type: node / optional_node / nodes / identifier / ..."""
    origin = typing.get_origin(tp)
    if origin is list:
        (inner,) = typing.get_args(tp)
        if inner is str:
            return "identifiers", None
        if inner.__name__ in ENUM_FAMILIES:
            return "operators", inner.__name__
        return "nodes", inner.__name__
    if origin in (types.UnionType, typing.Union):
        (inner,) = [a for a in typing.get_args(tp) if a is not type(None)]
        if inner is str:
            return "optional_identifier", None
        if inner is int:
            return "integer", None
        return "optional_node", inner.__name__
    if tp is str:
        return "identifier", None
    if tp is int:
        return "integer", None
    if tp is object:
        return "constant", None
    if tp.__name__ in ENUM_FAMILIES:
        return "operator", tp.__name__
    return "node", tp.__name__


def storage_of(kind, field, shape, taken):
    """Where a field's value lives: child, value, small or leaf (see the module docstring)."""
    if field in ALWAYS_NONE:
        return "never"
    if shape in ("node", "optional_node", "nodes"):
        return "child"
    if shape in ("identifiers", "operators"):
        return "leaf"
    if shape == "constant":   # Constant.value, MatchSingleton.value, Interpolation.str
        if kind == "MatchSingleton":
            return "small"
        return "value"
    if shape in ("identifier", "optional_identifier"):
        if "value" in taken:
            return "leaf"
        return "value"
    if shape in ("operator", "integer"):
        assert "small" not in taken or kind == "Constant", (kind, field)
        return "small"
    raise AssertionError((kind, field, shape))


def build():
    kinds = []
    for cls, family in concrete_classes():
        name = cls.__name__
        fields = []
        taken = set()
        for f in cls._fields:
            shape, of = shape_of(cls._field_types[f])
            if name == "Constant" and f == "kind":
                shape, of = "optional_identifier", None
                store = "small"   # the 'u' prefix: one bit beside the constant's kind
            else:
                store = storage_of(name, f, shape, taken)
            if store in ("value", "small"):
                taken.add(store)
            fields.append((f, shape, of, store))
        kinds.append((name, family, bool(cls._attributes), fields))
    return kinds


def cpp_name(name):
    return name + "_" if name in ("And", "Or", "Not", "In", "Is", "Del") else name


def render(kinds):
    lines = [
        "#pragma once",
        "// GENERATED by src/_pygim_fast/code/python/gen_ast_tables.py from Python "
        f"{sys.version_info.major}.{sys.version_info.minor}'s ast module — do not edit.",
        "//",
        "// The node kinds of Python's syntax tree in the order the ast module defines them, then the",
        "// scalar kinds rows.h adds (identifier, cmpop, absent); each kind's fields in `_fields` order,",
        "// with the shape of the value and where a row keeps it (see gen_ast_tables.py).",
        "//",
        "// CORE layer: pybind-free.",
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <span>",
        "#include <string_view>",
        "",
        "namespace pygim::code::python {",
        "",
        "/// What a field holds: one node, an optional one, a list of them; an identifier, an optional",
        "/// one, a list; an integer; a constant; one operator or context, a list of operators.",
        "enum class shape : std::uint8_t { node, optional_node, nodes, identifier, optional_identifier, identifiers, integer, constant, operator_, operators };",
        "/// Where a row keeps a field: its child rows, its value, its small, its leaf rows, nowhere.",
        "enum class storage : std::uint8_t { child, value, small, leaf, never };",
        "",
        "/// One field of a kind: its name, its shape, where it is kept, and for an operator the family",
        "/// whose kinds name its values.",
        "class field_info {",
        "public:",
        "    constexpr field_info(std::string_view name, enum shape s, enum storage st, std::uint8_t family) noexcept",
        "        : m_name(name), m_shape(s), m_storage(st), m_family(family) {}",
        "    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }",
        "    [[nodiscard]] constexpr enum shape shape() const noexcept { return m_shape; }",
        "    [[nodiscard]] constexpr enum storage storage() const noexcept { return m_storage; }",
        "    /// The first kind of an operator field's family (Load for a context, Add for an operator).",
        "    [[nodiscard]] constexpr std::uint8_t family() const noexcept { return m_family; }",
        "",
        "private:",
        "    std::string_view m_name;",
        "    enum shape m_shape;",
        "    enum storage m_storage;",
        "    std::uint8_t m_family;",
        "};",
        "static_assert(field_info(\"op\", shape::operator_, storage::small, 66).family() == 66 && field_info(\"op\", shape::operator_, storage::small, 66).name() == \"op\");",
        "",
    ]
    enum_names = [cpp_name(k[0]) for k in kinds] + ["identifier", "cmpop", "absent"]
    lines.append("/// Every node kind, then the scalar kinds of leaf rows.")
    lines.append("enum class kind : std::uint8_t {")
    for i in range(0, len(enum_names), 8):
        lines.append("    " + ", ".join(enum_names[i:i + 8]) + ",")
    lines.append("};")
    lines.append(f"inline constexpr std::size_t kind_count = {len(enum_names)};")
    lines.append("")
    first_of = {}
    for i, (name, family, *_rest) in enumerate(kinds):
        if family in ENUM_FAMILIES and family not in first_of:
            first_of[family] = i
    lines.append("/// The first kind of each family of singletons; a row's small counts from it.")
    lines.append("namespace family {")
    for fam in ENUM_FAMILIES:
        lines.append(f"inline constexpr std::uint8_t {fam}{'_' if fam == 'operator' else ''} = {first_of[fam]};")
    lines.append("}  // namespace family")
    lines.append("/// A kind's place in its family, which a row's small holds: Store is 1 in expr_context, Add 0 in operator.")
    lines.append("[[nodiscard]] constexpr std::uint8_t member_index(kind k, std::uint8_t first) noexcept { return static_cast<std::uint8_t>(static_cast<std::uint8_t>(k) - first); }")
    lines.append("static_assert(member_index(kind::Store, family::expr_context) == 1 && member_index(kind::Add, family::operator_) == 0);")
    lines.append("")
    lines.append("namespace detail {")
    for name, family, positioned, fields in kinds:
        if not fields:
            continue
        entries = []
        for f, shape, of, store in fields:
            fam = first_of.get(of, 0) if shape in ("operator", "operators") else 0
            entries.append(f'field_info("{f}", shape::{"operator_" if shape == "operator" else shape}, storage::{store}, {fam})')
        lines.append(f"inline constexpr std::array<field_info, {len(fields)}> fields_of_{name}{{")
        for e in entries:
            lines.append(f"    {e},")
        lines.append("};")
    lines.append("}  // namespace detail")
    lines.append("")
    lines.append("/// One kind: its name, whether its nodes have positions, its fields.")
    lines.append("class kind_info {")
    lines.append("public:")
    lines.append("    constexpr kind_info(std::string_view name, bool positioned, std::span<const field_info> fields) noexcept")
    lines.append("        : m_name(name), m_positioned(positioned), m_fields(fields) {}")
    lines.append("    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }")
    lines.append("    [[nodiscard]] constexpr bool positioned() const noexcept { return m_positioned; }")
    lines.append("    [[nodiscard]] constexpr std::span<const field_info> fields() const noexcept { return m_fields; }")
    lines.append("")
    lines.append("private:")
    lines.append("    std::string_view m_name;")
    lines.append("    bool m_positioned;")
    lines.append("    std::span<const field_info> m_fields;")
    lines.append("};")
    lines.append("static_assert(kind_info(\"Name\", true, {}).positioned() && kind_info(\"Name\", true, {}).fields().empty());")
    lines.append("")
    lines.append(f"inline constexpr std::array<kind_info, kind_count> kinds{{")
    for name, _family, positioned, fields in kinds:
        span = f"detail::fields_of_{name}" if fields else "std::span<const field_info>{}"
        lines.append(f'    kind_info("{name}", {"true" if positioned else "false"}, {span}),')
    for scalar in ("identifier", "cmpop", "absent"):
        lines.append(f'    kind_info("{scalar}", false, std::span<const field_info>{{}}),')
    lines.append("};")
    lines.append("")
    lines.append("/// The information of a kind.")
    lines.append("[[nodiscard]] constexpr const kind_info& info(kind k) noexcept { return kinds[static_cast<std::size_t>(k)]; }")
    lines.append("")
    lines.append("/// Each kind's field slots, by name: slot::FunctionDef::body is the `field` of a body statement.")
    lines.append("namespace slot {")
    for name, _family, _positioned, fields in kinds:
        if not fields:
            continue
        members = ", ".join(f"{f} = {i}" for i, (f, *_r) in enumerate(fields))
        lines.append(f"namespace {name} {{ inline constexpr std::uint8_t {members}; }}")
    lines.append("}  // namespace slot")
    lines.append("")
    lines.append("static_assert(info(kind::FunctionDef).name() == \"FunctionDef\" && info(kind::FunctionDef).fields()[slot::FunctionDef::body].shape() == shape::nodes);")
    lines.append("")
    lines.append("}  // namespace pygim::code::python")
    lines.append("")
    return "\n".join(lines)


def main():
    text = render(build())
    if len(sys.argv) > 1 and sys.argv[1] == "--print":
        sys.stdout.write(text)
        return
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
