#pragma once
// similarity/python_source.h — the Python frontend: a source's units, read from the tree
// pygim.code's parser builds in C++ (code/python/), with no Python object anywhere.
//
//     std::vector<parsed_unit> units = python_source::parse_python(bytes, ctx);   // parse_error if not Python
//
// It emits the trees the frontend that walked CPython's `ast` emitted — but for the `default_value`
// field type parameters gained in 3.13, a "None" leaf each, and the scopes of defaults and
// decorators (below): the node kinds and fields are the ast module's own
// (code/python/ast_tables.gen.h), visited in `_fields` order. Every
// field holding a node or a list becomes children (a list as one "<Kind>.<field>" node, so
// `if a: x` and `if a: pass\nelse: x` differ); a missing optional value is a "None" leaf. Labels
// and roles:
//
//     Name             a LOCAL of the unit or an enclosing function: the identifier; bind
//                      under Store/Del, use under Load
//     Ref              any other name, resolved (names.h): "os", "len"
//     Constant         the value's type ("int", "str", "NoneType"), constant
//     a str field      "<Kind>.<field>" leaf; bind for the names a statement introduces (def and
//                      class names, arg, except ... as, match captures), attr otherwise
//     an int field     its value, attr (comprehension.is_async, FormattedValue.conversion)
//     an operator      a node of its kind ("Add", "Lt")
//
// Names are resolved as Python scopes them, statically, but for comprehensions, which are no scope
// here (their names stay the unit's): a name assigned anywhere in a function (and not declared
// global/nonlocal) is local to it and stays a Name; any other is looked up outwards — enclosing
// functions (class bodies skipped), then the module, whose table
// binds every import, every module-level definition ("<module>.helper") and every module-level
// alias (`list = list` is the builtin list, `getcwd = os.getcwd` is os.getcwd). A resolved name is
// emitted as its fully qualified spelling's chain — Ref "os", Attribute "path", "join" — so
// `os.path.join(p)`, `osp.join(p)` and `j(p)` are one tree. `builtins.len` is `len`. A name bound
// two ways in one scope stays as written. A function's defaults and decorators, and a class's
// bases, keywords and decorators, are evaluated where it is defined, and resolved there:
// `def f(a, x=x)` takes the module's x (until 2026-10-29 the parameter's, renamed).
//
// Left out, so they never count: import statements inside a unit (what they bind is in the
// names), docstrings, annotations (`annotation`, `returns`), `type_comment`, `ctx` (in the Name's
// role), a Constant's `kind`.
//
// Units: every function (methods and nested functions; qualnames as __qualname__: `C.m`,
// `f.<locals>.g`) and every class, whose tree is its header — the ClassDef with its bases,
// keywords, decorators and every body statement that is not a function or class.
//
// The grammar is Python 3.14's, whatever Python runs pygim (Q34): scores are reproducible across
// interpreters, where the ast frontend's were only within one.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "../code/python/literals.h"
#include "../code/python/tree.h"
#include "../mapping/intern.h"
#include "../mapping/storage.h"
#include "core.h"
#include "names.h"

namespace pygim::similarity::python_source {

namespace cp = pygim::code::python;
using cp::kind;
using cp::field_info;
using cp::shape;
using cp::storage;

namespace detail {

[[nodiscard]] constexpr bool is_def(kind k) noexcept { return k == kind::FunctionDef || k == kind::AsyncFunctionDef; }
static_assert(is_def(kind::AsyncFunctionDef) && !is_def(kind::Lambda));   // a unit: `async def` is a function too

[[nodiscard]] constexpr bool opens_scope(kind k) noexcept { return is_def(k) || k == kind::Lambda || k == kind::ClassDef; }
static_assert(opens_scope(kind::Lambda) && !opens_scope(kind::ListComp));   // a comprehension's names stay the unit's

// Fields the trees leave out.
[[nodiscard]] constexpr bool skipped(std::string_view field) noexcept {
    return field == "ctx" || field == "type_comment" || field == "annotation" || field == "returns" || field == "kind";
}
static_assert(skipped("annotation") && !skipped("body"));   // `def f(a: int)` is `def f(a)`

// Whether a str field binds the name it holds.
[[nodiscard]] constexpr bool binds(kind k, std::string_view field) noexcept {
    return (field == "name" && (is_def(k) || k == kind::ClassDef || k == kind::ExceptHandler || k == kind::MatchAs || k == kind::MatchStar)) ||
           (k == kind::arg && field == "arg") || (k == kind::MatchMapping && field == "rest");
}
static_assert(binds(kind::arg, "arg") && !binds(kind::Attribute, "attr"));   // a parameter is renamed, an attribute kept

// Whether a field of a node that opens a scope is evaluated where the node is, not in its scope:
// a function's or lambda's parameters (their defaults: the names are bound inside), decorators, a
// class's bases and keywords.
[[nodiscard]] constexpr bool evaluated_outside(kind k, std::string_view field) noexcept {
    if (is_def(k)) return field == "args" || field == "decorator_list";
    if (k == kind::Lambda) return field == "args";
    if (k == kind::ClassDef) return field == "bases" || field == "keywords" || field == "decorator_list";
    return false;
}
static_assert(evaluated_outside(kind::FunctionDef, "args") && !evaluated_outside(kind::FunctionDef, "body") &&
              evaluated_outside(kind::ClassDef, "bases") && !evaluated_outside(kind::Call, "args"));   // `def f(x=x)`: the outer x

// The type name of a Constant's value, as type(value).__name__ says it.
[[nodiscard]] constexpr std::string_view constant_type(cp::constant_kind k) noexcept {
    switch (k) {
        case cp::constant_kind::none: return "NoneType";
        case cp::constant_kind::true_:
        case cp::constant_kind::false_: return "bool";
        case cp::constant_kind::ellipsis: return "ellipsis";
        case cp::constant_kind::integer: return "int";
        case cp::constant_kind::floating: return "float";
        case cp::constant_kind::imaginary: return "complex";
        case cp::constant_kind::string: return "str";
        case cp::constant_kind::bytes: return "bytes";
    }
    return "object";
}
static_assert(constant_type(cp::constant_kind::imaginary) == "complex");

// What a tree needs of one field of one kind: its label "<Kind>.<field>" (a list node's and a
// scalar leaf's kind) as an atom, and what the field is to a tree. Made at compile time.
class field_traits {
public:
    constexpr field_traits() = default;
    constexpr field_traits(kind k, const field_info& f) noexcept
        : m_label(atom::joined(cp::info(k).name(), ".", f.name())),
          m_skipped(detail::skipped(f.name())),
          m_is_name(f.name() == "name"),
          m_is_body(f.name() == "body"),
          m_binds(detail::binds(k, f.name())),
          m_outside(detail::evaluated_outside(k, f.name())) {}

    [[nodiscard]] constexpr atom label() const noexcept { return m_label; }
    [[nodiscard]] constexpr bool skipped() const noexcept { return m_skipped; }
    [[nodiscard]] constexpr bool is_name() const noexcept { return m_is_name; }   // a definition's own name
    [[nodiscard]] constexpr bool is_body() const noexcept { return m_is_body; }
    [[nodiscard]] constexpr role name_role() const noexcept { return m_binds ? role::bind : role::attr; }   // of a str it holds
    [[nodiscard]] constexpr bool outside() const noexcept { return m_outside; }   // evaluated_outside

private:
    atom m_label;
    bool m_skipped = false;
    bool m_is_name = false;
    bool m_is_body = false;
    bool m_binds = false;
    bool m_outside = false;
};
static_assert(field_traits(kind::ExceptHandler, cp::info(kind::ExceptHandler).fields()[cp::slot::ExceptHandler::name]).name_role() == role::bind);   // `except E as e`

inline constexpr std::size_t most_fields = [] {
    std::size_t most = 0;
    for (const cp::kind_info& k : cp::kinds) most = std::max(most, k.fields().size());
    return most;
}();

// Every kind's atom and its fields' traits, by kind and slot.
class kind_traits {
public:
    consteval kind_traits() {
        for (std::size_t k = 0; k < cp::kind_count; ++k) {
            const auto fields = cp::kinds[k].fields();
            m_kinds[k] = atom(cp::kinds[k].name());
            for (std::size_t s = 0; s < fields.size(); ++s) m_fields[k][s] = field_traits(static_cast<kind>(k), fields[s]);
        }
    }
    [[nodiscard]] constexpr atom of(kind k) const noexcept { return m_kinds[static_cast<std::size_t>(k)]; }
    [[nodiscard]] constexpr const field_traits& field(kind k, std::size_t s) const noexcept { return m_fields[static_cast<std::size_t>(k)][s]; }

private:
    std::array<atom, cp::kind_count> m_kinds{};
    std::array<std::array<field_traits, most_fields>, cp::kind_count> m_fields{};
};
static_assert(kind_traits().of(kind::Return) == atom("Return"));
inline constexpr kind_traits traits;
static_assert(traits.of(kind::Name) == atom("Name") && traits.field(kind::Call, cp::slot::Call::args).label() == atom("Call.args") &&
              traits.field(kind::FunctionDef, cp::slot::FunctionDef::body).is_body() &&
              traits.field(kind::arg, cp::slot::arg::arg).name_role() == role::bind && traits.field(kind::Name, cp::slot::Name::ctx).skipped());

// The kinds and labels the reader makes itself.
inline constexpr atom none_kind{"None"};
inline constexpr atom ref_kind{"Ref"};
inline constexpr atom attribute_attr{"Attribute.attr"};
inline constexpr atom true_label{"True"};
inline constexpr atom false_label{"False"};

// A Constant's label: its type's name (constant_type), made an atom once per kind.
inline constexpr auto constant_types = [] {
    std::array<atom, cp::constant_kind_count> out{};
    for (std::size_t k = 0; k < out.size(); ++k) out[k] = atom(constant_type(static_cast<cp::constant_kind>(k)));
    return out;
}();
[[nodiscard]] constexpr atom constant_label(cp::constant_kind k) noexcept { return constant_types[static_cast<std::size_t>(k)]; }
static_assert(constant_label(cp::constant_kind::imaginary) == atom("complex"));   // 1j


// The slots of a scope's parameters and body, by the kind that opens it (a function, async or
// not, a lambda, a class: no parameters).
static_assert(cp::slot::FunctionDef::args == cp::slot::AsyncFunctionDef::args && cp::slot::FunctionDef::body == cp::slot::AsyncFunctionDef::body);
[[nodiscard]] constexpr std::uint8_t args_slot(kind k) noexcept { return k == kind::Lambda ? cp::slot::Lambda::args : cp::slot::FunctionDef::args; }
[[nodiscard]] constexpr std::uint8_t body_slot(kind k) noexcept {
    return k == kind::Lambda ? cp::slot::Lambda::body : k == kind::ClassDef ? cp::slot::ClassDef::body : cp::slot::FunctionDef::body;
}
static_assert(args_slot(kind::Lambda) == cp::slot::Lambda::args && body_slot(kind::ClassDef) == cp::slot::ClassDef::body &&
              body_slot(kind::AsyncFunctionDef) == cp::slot::AsyncFunctionDef::body);

// The atoms of the numbers an int field holds — is_async's 0 and 1, a conversion's 97, 114 and
// 115 (-1 is minus_one) — made once: a number past them is made where it is met.
inline constexpr auto small_numbers = [] {
    std::array<atom, 256> out{};
    for (std::size_t v = 0; v < out.size(); ++v) {
        std::array<char, 3> text{};
        std::size_t n = 0;
        if (v >= 100) text[n++] = static_cast<char>('0' + v / 100);
        if (v >= 10) text[n++] = static_cast<char>('0' + v / 10 % 10);
        text[n++] = static_cast<char>('0' + v % 10);
        out[v] = atom(std::string_view(text.data(), n));
    }
    return out;
}();
inline constexpr atom minus_one{"-1"};
static_assert(small_numbers[0] == atom("0") && small_numbers[115] == atom("115") && small_numbers[255] == atom("255"));

}  // namespace detail

/// One lexical scope: its locals, and the names it binds to qualified names — its imports and, at
/// module level, every definition and alias. The module scope has no locals: everything it binds
/// is in its table. A class body is a scope its methods do not see.
class scope {
public:
    /// `locals` sorted, as scope_names::found::locals() gives them.
    scope(bool is_class, std::vector<std::string> locals, binding_table table)
        : m_is_class(is_class), m_locals(std::move(locals)), m_table(std::move(table)) {}
    [[nodiscard]] static scope module(binding_table table) { return scope(false, {}, std::move(table)); }

    [[nodiscard]] bool is_class() const noexcept { return m_is_class; }
    /// Whether `n` is bound here, as a local or in the table: then it hides the scopes outside.
    [[nodiscard]] bool binds(std::string_view n) const { return local(n) || m_table.binds(n); }
    /// What `n`, bound here, stands for: none for a local or an ambiguous binding.
    [[nodiscard]] std::optional<std::string_view> resolve(std::string_view n) const { return local(n) ? std::nullopt : m_table.resolve(n); }

private:
    [[nodiscard]] bool local(std::string_view n) const { return std::binary_search(m_locals.begin(), m_locals.end(), n); }

    bool m_is_class;
    std::vector<std::string> m_locals;   // sorted
    binding_table m_table;
};

/// What the statements of one scope bind, as a walk over them finds it: the names assigned or
/// defined, the names declared global or nonlocal, and the imports. finish() ends the walk.
class scope_names {
public:
    /// What a walk found, its names sorted.
    class found {
    public:
        found(std::vector<std::string> bound, std::vector<std::string> declared, binding_table::builder imports)
            : m_bound(std::move(bound)), m_declared(std::move(declared)), m_imports(std::move(imports)) {
            std::sort(m_bound.begin(), m_bound.end());
            std::sort(m_declared.begin(), m_declared.end());
        }

        /// Every binding of a name, sorted: a name bound twice is in it twice.
        [[nodiscard]] const std::vector<std::string>& bindings() const noexcept { return m_bound; }
        /// Every name bound, sorted, once each.
        [[nodiscard]] std::vector<std::string> names() const {
            std::vector<std::string> out = m_bound;
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        }
        /// The names bound and not declared global or nonlocal: a function's locals.
        [[nodiscard]] std::vector<std::string> locals() const {
            std::vector<std::string> out = names();
            std::erase_if(out, [&](const std::string& n) { return std::binary_search(m_declared.begin(), m_declared.end(), n); });
            return out;
        }
        /// The imports, moved out to the scope they belong to.
        [[nodiscard]] binding_table::builder take_imports() noexcept { return std::move(m_imports); }

    private:
        std::vector<std::string> m_bound;
        std::vector<std::string> m_declared;
        binding_table::builder m_imports;
    };

    void bind(std::string_view n) { m_bound.emplace_back(n); }
    void declare(std::string_view n) { m_declared.emplace_back(n); }
    [[nodiscard]] binding_table::builder& imports() noexcept { return m_imports; }
    [[nodiscard]] found finish() && { return found(std::move(m_bound), std::move(m_declared), std::move(m_imports)); }

private:
    std::vector<std::string> m_bound;
    std::vector<std::string> m_declared;
    binding_table::builder m_imports;
};

/// Reads one file's tree into units.
template <mapping::interner Names>
class reader {
public:
    reader(const cp::syntax_tree& t, const Names& names, const source_context& ctx, std::vector<parsed_unit>& units)
        : m_t(t), m_names(names), m_ctx(ctx), m_units(units), m_name_atoms(atoms_of(names)) {}

    /// Every unit of the module, in source order.
    void run() {
        scope_chain chain{module_scope(0)};
        discover(0, "", std::nullopt, chain);
    }

private:
    // How a node is emitted: inside a unit's tree; as a unit's own node (its name left out); as a
    // class's header; or as a statement of a class body that opens no scope (an `if`, a `try`),
    // where a def or class is a method or nested class of its own as at the top of the body.
    enum class emit_mode { nested, unit_root, class_header, class_statement };
    using alias_list = std::vector<std::pair<std::string, std::string>>;   // (a name, the dotted name assigned to it)
    using alias_table = mapping::flat_storage<std::string, std::string>;   // a name -> the one dotted name it aliases
    using scope_chain = std::vector<scope>;                                // the module's first, the innermost last

    // ── the tree ────────────────────────────────────────────────────────────
    [[nodiscard]] kind kind_of(std::uint32_t i) const noexcept { return m_t[i].kind(); }
    [[nodiscard]] std::string_view name(std::uint32_t id) const { return m_names[id]; }
    [[nodiscard]] atom name_atom(std::uint32_t id) const noexcept { return m_name_atoms[id]; }
    [[nodiscard]] static std::vector<atom> atoms_of(const Names& names) {
        std::vector<atom> out;
        out.reserve(names.size());
        for (std::uint32_t id = 0; id < names.size(); ++id) out.emplace_back(names[id]);
        return out;
    }
    // A node's identifier kept in its value ("" when it has none).
    [[nodiscard]] std::string_view value_name(std::uint32_t i) const {
        const std::uint32_t v = m_t[i].value();
        return v == cp::no_value ? std::string_view() : name(v);
    }
    [[nodiscard]] bool loads(std::uint32_t i) const noexcept { return m_t[i].small() == 0; }   // ctx Load
    [[nodiscard]] std::uint32_t line_of(std::uint32_t i) const noexcept { return m_t.position_of(m_t[i].start()).line(); }

    // Calls f(c) on each child of `i` in field `s`, in order (None in a list included: kind absent).
    template <class F>
    void each_in(std::uint32_t i, std::size_t s, F&& f) const {
        for (std::uint32_t c : m_t.children(i)) {
            if (m_t[c].field() == s) f(c);
        }
    }
    // Calls f(d) on each decorator of def or class `i`. Children come in source order, so its
    // decorators are its first: the rest is not walked (each_in walked every child of every def,
    // 3% of a Python add_files).
    template <class F>
    void each_decorator(std::uint32_t i, F&& f) const {
        static_assert(cp::slot::FunctionDef::decorator_list == cp::slot::AsyncFunctionDef::decorator_list);
        const std::size_t s = kind_of(i) == kind::ClassDef ? cp::slot::ClassDef::decorator_list : cp::slot::FunctionDef::decorator_list;
        for (std::uint32_t c : m_t.children(i)) {
            if (m_t[c].field() != s) return;
            f(c);
        }
    }
    // The one child of `i` in slot `s` of its kind's fields, or no_value.
    [[nodiscard]] std::uint32_t child(std::uint32_t i, std::size_t s) const {
        for (std::uint32_t c : m_t.children(i)) {
            if (m_t[c].field() == s) return c;
        }
        return cp::no_value;
    }
    // Calls f on every node directly under `i`, fields in `_fields` order (operators, contexts and
    // None hold nothing a scope or a unit needs, and are passed over).
    template <class F>
    void for_each_child(std::uint32_t i, F&& f) const {
        const auto fields = cp::info(kind_of(i)).fields();
        for (std::size_t s = 0; s < fields.size(); ++s) {
            if (fields[s].storage() != storage::child) continue;
            each_in(i, s, [&](std::uint32_t c) {
                if (kind_of(c) != kind::absent) f(c);
            });
        }
    }

    // ── scopes ──────────────────────────────────────────────────────────────
    // What an Import / ImportFrom binds, into `table`.
    void bind_import(std::uint32_t stmt, binding_table::builder& table) const {
        if (kind_of(stmt) == kind::Import) {
            each_in(stmt, cp::slot::Import::names, [&](std::uint32_t alias) {
                const std::string n(value_name(alias));
                const std::string as = asname(alias);
                if (!as.empty()) {
                    table.bind(as, n);
                } else {
                    const std::string top = n.substr(0, n.find('.'));   // `import a.b` binds a
                    table.bind(top, top);
                }
            });
            return;
        }
        const std::string module(value_name(stmt));
        const std::size_t level = m_t[stmt].small();
        const std::optional<std::string> resolved = relative_module(m_ctx.package(), level, module);
        const std::string base = resolved ? *resolved : std::string(level, '.') + module;
        each_in(stmt, cp::slot::ImportFrom::names, [&](std::uint32_t alias) {
            const std::string n(value_name(alias));
            if (n == "*") return;   // unknowable statically: those names stay as written
            const std::string as = asname(alias);
            const std::string target = base.empty() || base.back() == '.' ? base + n : base + "." + n;
            table.bind(as.empty() ? n : as, target);
        });
    }
    [[nodiscard]] std::string asname(std::uint32_t alias) const {
        std::string out;
        each_in(alias, cp::slot::alias::asname, [&](std::uint32_t leaf) { out = std::string(name(m_t[leaf].value())); });
        return out;
    }
    // The names a Global or Nonlocal declares.
    void declared_names(std::uint32_t stmt, scope_names& names) const {
        const std::uint8_t field = kind_of(stmt) == kind::Global ? cp::slot::Global::names : cp::slot::Nonlocal::names;
        each_in(stmt, field, [&](std::uint32_t leaf) { names.declare(name(m_t[leaf].value())); });
    }

    // Collects what the statements under `i` bind in ITS scope: stops at nested functions,
    // lambdas and classes (recording their names).
    void collect(std::uint32_t i, scope_names& names) const {
        for_each_child(i, [&](std::uint32_t c) { collect_node(c, names); });
    }
    void collect_node(std::uint32_t c, scope_names& names) const {
        switch (const kind k = kind_of(c)) {
            case kind::Name:
                if (!loads(c)) names.bind(value_name(c));
                return;
            case kind::Import:
            case kind::ImportFrom: bind_import(c, names.imports()); return;
            case kind::Global:
            case kind::Nonlocal: declared_names(c, names); return;
            case kind::FunctionDef:
            case kind::AsyncFunctionDef:
            case kind::ClassDef:   // its name here; its decorators are evaluated here
                names.bind(value_name(c));
                each_decorator(c, [&](std::uint32_t d) { collect_expr(d, names); });
                return;
            case kind::Lambda: return;   // its own scope; its defaults are evaluated here but bind nothing
            case kind::ExceptHandler:
            case kind::MatchAs:
            case kind::MatchStar:
            case kind::MatchMapping:   // a name, or MatchMapping's rest
                if (const std::string_view n = value_name(c); !n.empty()) names.bind(n);
                collect(c, names);
                return;
            default: collect(c, names); return;
        }
    }
    // An expression's bindings in this scope: a Name stored to, or what collect() finds under it.
    void collect_expr(std::uint32_t e, scope_names& names) const {
        if (kind_of(e) == kind::Name) {
            if (!loads(e)) names.bind(value_name(e));
            return;
        }
        collect(e, names);
    }

    // The module's scope: every import and every module-level binding in the table
    // ("<module>.<name>", or the bare name when the module is unknown). A name both imported and
    // assigned is ambiguous.
    [[nodiscard]] scope module_scope(std::uint32_t root) const {
        scope_names walk;
        collect(root, walk);
        scope_names::found names = std::move(walk).finish();
        const std::vector<std::string>& bound = names.bindings();

        // Aliases: a name whose every binding is `name = <dotted name>` stands for what the dotted
        // name stands for, not for <module>.<name>.
        alias_list aliases;
        collect_aliases(root, aliases);
        std::sort(aliases.begin(), aliases.end());
        alias_list one_target;   // one target, and no other binding
        for (std::size_t i = 0; i < aliases.size();) {
            std::size_t j = i;
            bool one = true;
            while (j < aliases.size() && aliases[j].first == aliases[i].first) {
                one = one && aliases[j].second == aliases[i].second;
                ++j;
            }
            const auto range = std::equal_range(bound.begin(), bound.end(), aliases[i].first);
            if (one && static_cast<std::size_t>(range.second - range.first) == j - i) one_target.push_back(aliases[i]);
            i = j;
        }
        alias_table pure;
        pure.assign_bulk(std::move(one_target));

        const std::string prefix = m_ctx.module().empty() ? std::string() : std::string(m_ctx.module()) + ".";
        binding_table::builder table = names.take_imports();
        for (const std::string& n : names.names()) {
            if (!pure.find(n)) table.bind(n, prefix + n);
        }
        const binding_table heads = table.built();   // what an alias's head resolves through: no alias yet
        for (const auto& [n, dotted] : pure.items()) {
            std::vector<std::string> visiting{n};
            if (const auto q = resolve_alias(dotted, heads, pure, visiting)) table.bind(n, *q);
            else table.bind(n, prefix + n);   // through an ambiguous name: not followed
        }
        return scope::module(std::move(table).build());
    }

    // `name = <dotted>` statements of the module's own scope (not inside functions, lambdas or
    // classes): (name, "a.b.c").
    void collect_aliases(std::uint32_t i, alias_list& out) const {
        for_each_child(i, [&](std::uint32_t c) {
            const kind k = kind_of(c);
            if (detail::is_def(k) || k == kind::ClassDef || k == kind::Lambda) return;
            if (k == kind::Assign) {
                std::uint32_t target = cp::no_value, count = 0;
                each_in(c, cp::slot::Assign::targets, [&](std::uint32_t t) {
                    target = t;
                    ++count;
                });
                if (count == 1 && kind_of(target) == kind::Name) {
                    if (const auto d = dotted(child(c, cp::slot::Assign::value))) out.emplace_back(value_name(target), *d);
                }
            }
            collect_aliases(c, out);
        });
    }

    // A Name or a chain of Attributes on a Name, as "a.b.c"; none otherwise.
    [[nodiscard]] std::optional<std::string> dotted(std::uint32_t e) const {
        if (e == cp::no_value) return std::nullopt;
        const kind k = kind_of(e);
        if (k == kind::Name) return std::string(value_name(e));
        if (k != kind::Attribute) return std::nullopt;
        const auto base = dotted(child(e, cp::slot::Attribute::value));
        if (!base) return std::nullopt;
        return *base + "." + std::string(value_name(e));
    }

    // What alias target `dotted` stands for: its head through the other aliases (cycles and
    // self-reference, `list = list`, end at the name as written: a builtin or unbound global) or
    // the module's table; none when the head is ambiguous.
    static std::optional<std::string> resolve_alias(const std::string& dotted, const binding_table& table, const alias_table& pure,
                                                    std::vector<std::string>& visiting) {
        const std::size_t dot = dotted.find('.');
        const std::string head = dotted.substr(0, dot);
        const std::string rest = dot == std::string::npos ? std::string() : dotted.substr(dot);
        if (std::find(visiting.begin(), visiting.end(), head) != visiting.end()) return dotted;   // itself: as written
        if (const std::string* target = pure.find(head)) {
            visiting.push_back(head);
            const auto q = resolve_alias(*target, table, pure, visiting);
            visiting.pop_back();
            return q ? std::optional<std::string>(*q + rest) : std::nullopt;
        }
        if (table.binds(head)) {
            const auto q = table.resolve(head);
            return q ? std::optional<std::string>(std::string(*q) + rest) : std::nullopt;
        }
        return dotted;   // unbound: a builtin or global, as written
    }

    // The scope a function, lambda or class body opens. A def nested in d units is emitted in each
    // of their trees, so its scope is made d + 1 times. Kept by node instead, the scopes cost more
    // than the walks they saved: Lib/test's add_files took 89-90 ms so, 88-89 made each time
    // (2026-10-29; real code nests shallowly).
    [[nodiscard]] scope inner_scope(std::uint32_t i) const {
        const kind k = kind_of(i);
        scope_names walk;
        if (k != kind::ClassDef) parameters(child(i, detail::args_slot(k)), walk);
        if (k == kind::Lambda) collect_expr(child(i, cp::slot::Lambda::body), walk);
        else each_in(i, detail::body_slot(k), [&](std::uint32_t stmt) { collect_node(stmt, walk); });
        scope_names::found names = std::move(walk).finish();
        return scope(k == kind::ClassDef, names.locals(), names.take_imports().build());
    }
    // The names an `arguments` node binds: every parameter, *args and **kwargs.
    void parameters(std::uint32_t args, scope_names& names) const {
        if (args == cp::no_value) return;
        for (std::uint8_t field : {cp::slot::arguments::posonlyargs, cp::slot::arguments::args, cp::slot::arguments::kwonlyargs,
                                   cp::slot::arguments::vararg, cp::slot::arguments::kwarg}) {
            each_in(args, field, [&](std::uint32_t a) { names.bind(value_name(a)); });
        }
    }
    // The qualified name `n` stands for from the innermost scope of `chain`: none when it is a
    // local of the unit or of an enclosing function, or ambiguous — then it stays a Name, as
    // written. The view is into the chain's tables or the tree's names, which outlive its use.
    [[nodiscard]] static std::optional<std::string_view> resolved(const scope_chain& chain, std::string_view n) {
        for (std::size_t i = chain.size(); i-- > 0;) {
            const scope& s = chain[i];
            if (s.is_class() && i + 1 != chain.size()) continue;   // methods do not see the class body
            if (s.binds(n)) return s.resolve(n);
        }
        return n;   // unbound: a global or builtin, as written
    }

    // ── units ───────────────────────────────────────────────────────────────
    // The units under `i`: a def is a method of `owner`, the class unit it sits in, if any.
    void discover(std::uint32_t i, const std::string& prefix, std::optional<std::uint32_t> owner, scope_chain& chain) {
        for_each_child(i, [&](std::uint32_t c) {
            const kind k = kind_of(c);
            if (detail::is_def(k)) {
                const std::string qual = prefix + std::string(value_name(c));
                chain.push_back(inner_scope(c));
                tree_builder b;
                emit(c, b, emit_mode::unit_root, chain);
                const auto me = static_cast<std::uint32_t>(m_units.size());
                m_units.push_back(parsed_unit{unit_kind::function, qual, line_of(c), std::move(b).finish(), {}});
                if (owner) m_units[*owner].add_method(me);
                discover(c, qual + ".<locals>.", std::nullopt, chain);
                chain.pop_back();
            } else if (k == kind::ClassDef) {
                const std::string qual = prefix + std::string(value_name(c));
                const auto me = static_cast<std::uint32_t>(m_units.size());
                m_units.push_back(parsed_unit{unit_kind::class_, qual, line_of(c), {}, {}});
                chain.push_back(inner_scope(c));
                tree_builder b;
                emit(c, b, emit_mode::class_header, chain);
                m_units[me].set_body(std::move(b).finish());
                discover(c, qual + ".", me, chain);
                chain.pop_back();
            } else {
                discover(c, prefix, owner, chain);
            }
        });
    }

    // ── trees ───────────────────────────────────────────────────────────────
    [[nodiscard]] bool is_docstring(std::uint32_t stmt) const {
        if (kind_of(stmt) != kind::Expr) return false;
        const std::uint32_t v = child(stmt, cp::slot::Expr::value);
        return v != cp::no_value && kind_of(v) == kind::Constant && cp::constant_of(m_t[v].small()) == cp::constant_kind::string;
    }

    // A resolved name as its fully qualified spelling's tree: Ref "os", Attribute "path", "join".
    // The parts are read off the text, not split into a vector: one was made for every Name.
    static void emit_ref(tree_builder& b, std::string_view qualified) {
        const std::string_view q = without_builtins(qualified);
        for (std::size_t dots = static_cast<std::size_t>(std::count(q.begin(), q.end(), '.')); dots > 0; --dots) b.open(detail::traits.of(kind::Attribute));
        std::size_t at = q.find('.');
        b.leaf(detail::ref_kind, atom(q.substr(0, at)), role::attr);
        while (at != std::string_view::npos) {
            const std::size_t from = at + 1;
            at = q.find('.', from);
            b.leaf(detail::attribute_attr, atom(q.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from)), role::attr);
            b.close();
        }
    }

    void emit(std::uint32_t i, tree_builder& b, emit_mode mode, scope_chain& chain) const {
        switch (kind_of(i)) {
            case kind::Name: return emit_name(i, b, chain);
            case kind::Constant: return emit_constant(i, b);
            case kind::Attribute:
                if (is_builtins_attribute(i, chain)) return emit_ref(b, value_name(i));   // builtins.len is len
                break;
            default: break;
        }
        emit_node(i, b, mode, chain);
    }
    // A Name: what it resolves to, or itself (a local, or ambiguous).
    void emit_name(std::uint32_t i, tree_builder& b, const scope_chain& chain) const {
        if (const auto qualified = resolved(chain, value_name(i))) return emit_ref(b, *qualified);
        b.leaf(detail::traits.of(kind::Name), name_atom(m_t[i].value()), loads(i) ? role::use : role::bind);
    }
    // A Constant: its type's name, never its value.
    void emit_constant(std::uint32_t i, tree_builder& b) const {
        const atom type = detail::constant_label(cp::constant_of(m_t[i].small()));
        b.leaf(detail::traits.of(kind::Constant), type, role::constant);
    }
    // Whether Attribute `i` is an attribute of the builtins module itself: no prefix then.
    [[nodiscard]] bool is_builtins_attribute(std::uint32_t i, const scope_chain& chain) const {
        const std::uint32_t base = child(i, cp::slot::Attribute::value);
        if (base == cp::no_value || kind_of(base) != kind::Name) return false;
        const auto qualified = resolved(chain, value_name(base));
        return qualified && *qualified == "builtins";
    }
    // Any other node: its kind, then each field in `_fields` order.
    void emit_node(std::uint32_t i, tree_builder& b, emit_mode mode, scope_chain& chain) const {
        const kind k = kind_of(i);
        const bool nested_scope = mode == emit_mode::nested && detail::opens_scope(k);
        if (nested_scope) chain.push_back(inner_scope(i));
        // its own scope is innermost: pushed above, or by discover for a unit's root
        const bool own_scope = nested_scope || ((mode == emit_mode::unit_root || mode == emit_mode::class_header) && detail::opens_scope(k));
        const bool has_docstring = detail::is_def(k) || k == kind::ClassDef || k == kind::Module;
        b.open(detail::traits.of(k));
        const auto fields = cp::info(k).fields();
        for (std::size_t s = 0; s < fields.size(); ++s) {
            const field_info& f = fields[s];
            const detail::field_traits& ft = detail::traits.field(k, s);
            if (ft.skipped()) continue;
            if ((mode == emit_mode::unit_root || mode == emit_mode::class_header) && ft.is_name()) continue;   // a unit's own name
            std::optional<scope> own;   // set aside while a field evaluated where the node is is emitted
            if (own_scope && ft.outside()) {
                own.emplace(std::move(chain.back()));
                chain.pop_back();
            }
            const bool list = f.shape() == shape::nodes || f.shape() == shape::identifiers || f.shape() == shape::operators;
            if (list) {
                b.open(ft.label());
                bool first = true;
                each_in(i, s, [&](std::uint32_t e) {
                    const bool leading = first;
                    first = false;
                    if (f.shape() == shape::nodes && kind_of(e) != kind::absent) {
                        if (ft.is_body() && has_docstring && leading && is_docstring(e)) return;
                        const kind ek = kind_of(e);
                        if (ek == kind::Import || ek == kind::ImportFrom) return;   // already in the names
                        if (in_class_body(mode) && (detail::is_def(ek) || ek == kind::ClassDef)) return;   // units of their own, at any depth of statements
                    }
                    element(e, b, k, s, f, mode, chain);
                });
                b.close();
            } else {
                single(i, b, k, s, f, mode, chain);
            }
            if (own) chain.push_back(std::move(*own));
        }
        b.close();
        if (nested_scope) chain.pop_back();
    }

    // Whether a node emitted so is part of a class body's statements, where defs are units.
    [[nodiscard]] static bool in_class_body(emit_mode mode) noexcept { return mode == emit_mode::class_header || mode == emit_mode::class_statement; }
    // The mode a child of a node emitted in `mode` is emitted in: a class body's statements pass
    // theirs down to any child that opens no scope (a lambda's body is its own); all else nests.
    [[nodiscard]] emit_mode below(emit_mode mode, std::uint32_t child) const noexcept {
        return in_class_body(mode) && !detail::opens_scope(kind_of(child)) ? emit_mode::class_statement : emit_mode::nested;
    }

    // One element of a list field of a node emitted in `mode`.
    void element(std::uint32_t e, tree_builder& b, kind k, std::size_t s, const field_info& f, emit_mode mode, scope_chain& chain) const {
        switch (f.shape()) {
            case shape::identifiers: {
                const detail::field_traits& ft = detail::traits.field(k, s);
                b.leaf(ft.label(), name_atom(m_t[e].value()), ft.name_role());
                return;
            }
            case shape::operators: b.leaf(detail::traits.of(cp::member_of(f, m_t[e].small()))); return;
            default:
                if (kind_of(e) == kind::absent) b.leaf(detail::none_kind);
                else emit(e, b, below(mode, e), chain);
                return;
        }
    }

    // An int field's label: its value written out (a conversion's -1 too).
    [[nodiscard]] static atom number_label(int v) {
        if (v >= 0 && static_cast<std::size_t>(v) < detail::small_numbers.size()) return detail::small_numbers[static_cast<std::size_t>(v)];
        return v == -1 ? detail::minus_one : atom(std::to_string(v));
    }
    // A field holding one value — a node, a name, a number, an operator, or None — of a node emitted in `mode`.
    void single(std::uint32_t i, tree_builder& b, kind k, std::size_t s, const field_info& f, emit_mode mode, scope_chain& chain) const {
        const cp::node& n = m_t[i];
        const detail::field_traits& ft = detail::traits.field(k, s);
        auto text = [&](atom v) { b.leaf(ft.label(), v, ft.name_role()); };
        switch (f.storage()) {
            case storage::never: b.leaf(detail::none_kind); return;
            case storage::child: {
                std::uint32_t c = cp::no_value;
                each_in(i, s, [&](std::uint32_t x) { c = x; });
                if (c == cp::no_value) b.leaf(detail::none_kind);
                else emit(c, b, below(mode, c), chain);
                return;
            }
            case storage::value:
                if (k == kind::Interpolation) {   // str: the expression's text
                    std::string t;
                    cp::decode_text(m_t, n.value(), t);
                    text(atom(t));
                    return;
                }
                if (n.value() == cp::no_value) b.leaf(detail::none_kind);
                else text(name_atom(n.value()));
                return;
            case storage::small:
                switch (cp::meaning_of(k, f)) {
                    case cp::small_meaning::member: b.leaf(detail::traits.of(cp::member_of(f, n.small()))); return;
                    case cp::small_meaning::singleton: {
                        const cp::constant_kind c = cp::constant_of(n.small());
                        if (c == cp::constant_kind::none) b.leaf(detail::none_kind);
                        else b.leaf(ft.label(), c == cp::constant_kind::true_ ? detail::true_label : detail::false_label, role::attr);
                        return;
                    }
                    case cp::small_meaning::conversion: b.leaf(ft.label(), number_label(cp::conversion_of(n.small())), role::attr); return;
                    case cp::small_meaning::string_prefix:   // a Constant's whole small, its literal's kind with the prefix: the kind counts
                    case cp::small_meaning::number: b.leaf(ft.label(), number_label(n.small()), role::attr); return;
                }
                return;
            case storage::leaf: {   // alias.asname: a name or None
                std::uint32_t c = cp::no_value;
                each_in(i, s, [&](std::uint32_t x) { c = x; });
                if (c == cp::no_value) b.leaf(detail::none_kind);
                else text(name_atom(m_t[c].value()));
                return;
            }
        }
    }

    const cp::syntax_tree& m_t;
    const Names& m_names;
    const source_context& m_ctx;
    std::vector<parsed_unit>& m_units;
    std::vector<atom> m_name_atoms;   // each interned name's, made once per file
};

/// What one worker parses with: the parser's rows, kept between files so their room is made once —
/// up to kept_rows: a parked worker (a thread_local workspace lives as long as its thread) would
/// otherwise hold the rows of the largest file it ever read for the life of the process.
class workspace {
public:
    /// The rows kept between files: 65,536, 1.25 MiB — a 320 KB file's; CPython's Lib has two larger.
    static constexpr std::size_t kept_rows = std::size_t{1} << 16;

    /// The tree of a file's bytes (its coding cookie honoured), its names into `names`. A source
    /// that is not Python is a parse_error naming `origin` and the line. UTF-8 bytes become the
    /// tree's text as they are: no copy.
    [[nodiscard]] cp::syntax_tree parse(arena_string bytes, mapping::hashed_interner& names, std::string_view origin) {
        cp::parse_outcome result = cp::parse_python(std::move(bytes), names, m_scratch);
        m_scratch.keep_room_for(kept_rows);
        if (const auto* e = std::get_if<cp::parse_failure>(&result)) {
            throw e->at() ? make_parse_error(origin, e->at()->line(), e->message()) : parse_error(std::string(origin) + ": " + e->message());
        }
        return std::get<cp::syntax_tree>(std::move(result));
    }

private:
    cp::parse_output m_scratch;
};

/// The units `bytes` defines (a file's bytes: its coding cookie is honoured). A source that is not
/// Python is a parse_error naming the origin and the line. Bytes handed over become the tree's text.
[[nodiscard]] inline std::vector<parsed_unit> parse_python(arena_string bytes, const source_context& ctx, workspace& work) {
    mapping::hashed_interner names;
    const cp::syntax_tree tree = work.parse(std::move(bytes), names, ctx.origin());
    std::vector<parsed_unit> units;
    reader<mapping::hashed_interner>(tree, names, ctx, units).run();
    return units;
}
[[nodiscard]] inline std::vector<parsed_unit> parse_python(std::string_view bytes, const source_context& ctx) {
    workspace work;
    return parse_python(arena_string(bytes), ctx, work);
}

}  // namespace pygim::similarity::python_source
