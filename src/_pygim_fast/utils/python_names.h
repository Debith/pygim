// utils/python_names.h — Python's naming conventions: what kind of name a name is, and how Python
// spells a private name inside a class. Dependency-free, pybind-free and constexpr: a base header.
//
// One family of rules, each matched to the place Python itself applies it (global #113):
//   is_dunder        enum._is_dunder — the rule Python's enum machinery uses
//   is_private_name  the compiler's _Py_Mangle: which names it mangles inside a class body
//   mangled          _Py_Mangle: `__secret` inside `class Trait:` is `_Trait__secret`
// and the moves built on them: unmangled (back), respelt and reowned (to another class), requalified
// (a qualified name to another owner). NamePolicy gathers them for one class; Reowning applies a
// chain of moves, class to class. Users: utils (pygim.utils.is_dunder), each (its dunder guard),
// traits (CodeBuilder and compose()). tests/static/python_names_proofs.cpp proves the rules.
//
// Worked example — the names a CodeBuilder.reown(Trait, Target) meets:
//
//   is_dunder("__lt__")                                -> true
//   is_dunder("___x__")                                -> false             a longer run of underscores, as enum says
//   owner_of("_Trait")                                 -> "Trait"           leading underscores dropped
//   is_private_name("__secret")                        -> true
//   is_private_name("__init__")                        -> false             ends with two underscores
//   mangled("Trait", "__secret")                       -> "_Trait__secret"
//   mangled("__", "__secret")                          -> "__secret"        an all-underscore class mangles nothing
//   unmangled("Trait", "_Trait__secret")               -> "__secret"
//   unmangled("Trait", "_Other__secret")               -> ""                not this owner's private name
//   respelt("Trait", "Target", "_Trait__secret")       -> "_Target__secret"
//   respelt("Trait", "Target", "value")                -> nothing           a public name does not move
//   reowned("Trait", "Target", "_Trait__secret")       -> "_Target__secret"
//   reowned("Trait", "Target", "value")                -> "value"           respelt's spelling, or the name
//   requalified("Trait", "Target", "Trait.f.<locals>.<lambda>") -> "Target.f.<locals>.<lambda>"
//
// Lifetime and threading: free functions over string_views, and NamePolicy, a view, store nothing.
// Reowning owns the class names it is given and is a value: then() returns a new one.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pygim::python_names {

/// Whether `name` is a dunder, by Python's own rule (enum._is_dunder): longer than four characters,
/// two underscores at each end, and neither pair part of a longer run of underscores.
/// Cost: four comparisons. Example: "__lt__" and "__module__" are dunders; "____", "___x__",
/// "__x___", "__" and "__x" are not.
constexpr bool is_dunder(std::string_view name) noexcept {
    return name.size() > 4 && name.starts_with("__") && name.ends_with("__") && name[2] != '_' &&
           name[name.size() - 3] != '_';
}

/// The part of a class name that mangling uses: the name without its leading underscores.
/// Cost: one scan over the leading underscores.
/// Example: owner_of("_Trait") is "Trait"; owner_of("__") is "", and such a class mangles nothing.
constexpr std::string_view owner_of(std::string_view class_name) noexcept {
    const auto first = class_name.find_first_not_of('_');
    return first == std::string_view::npos ? std::string_view{} : class_name.substr(first);
}

/// Whether Python mangles `name` inside a class body (_Py_Mangle): it starts with two underscores,
/// does not end with two, and has no dot (a dotted name only occurs in an import of a package).
/// Cost: three comparisons and one scan for a dot.
/// Example: "__secret" and "__x_" are private; "__init__", "__", "_secret" and "__pkg.mod" are not.
constexpr bool is_private_name(std::string_view name) noexcept {
    return name.starts_with("__") && !name.ends_with("__") && name.find('.') == std::string_view::npos;
}

/// `name` as Python spells it inside class `class_name`.
/// Cost: one allocation of the result.
/// Example: mangled("Trait", "__secret") is "_Trait__secret"; mangled("Trait", "value") is "value".
constexpr std::string mangled(std::string_view class_name, std::string_view name) {
    const auto owner = owner_of(class_name);
    if (!is_private_name(name) || owner.empty()) return std::string{name};
    std::string out;
    out.reserve(1 + owner.size() + name.size());
    out += '_';
    out += owner;
    out += name;
    return out;
}

/// The private name that `name` spells inside class `class_name`, or "" when `name` is not one of
/// that class's private names. In an all-underscore class private names are not mangled, so there
/// a private name is its own spelling.
/// Cost: one comparison of the owner, no allocation.
/// Example: unmangled("Trait", "_Trait__secret") is "__secret"; unmangled("Trait", "value") is "".
constexpr std::string_view unmangled(std::string_view class_name, std::string_view name) noexcept {
    const auto owner = owner_of(class_name);
    if (owner.empty()) return is_private_name(name) ? name : std::string_view{};
    if (name.size() <= 1 + owner.size() || name.front() != '_' || name.substr(1, owner.size()) != owner) return {};
    const auto rest = name.substr(1 + owner.size());
    return is_private_name(rest) ? rest : std::string_view{};
}

/// The new spelling of `name` when code compiled in class `from` moves into class `to`: a private
/// name of `from` becomes the same private name of `to`. Nothing for every other name, which does
/// not move — so a caller keeps what it already holds and allocates only for a name that changes.
/// Cost: one unmangle, no allocation for a name that does not move; one allocation for one that does.
/// Example: respelt("Trait", "Target", "_Trait__secret") is "_Target__secret"; respelt("Trait",
/// "Target", "value") is nothing.
constexpr std::optional<std::string> respelt(std::string_view from, std::string_view to, std::string_view name) {
    const auto private_name = unmangled(from, name);
    if (private_name.empty()) return std::nullopt;
    return mangled(to, private_name);
}

/// `name`, taken from code compiled in class `from` into code that belongs to class `to`: respelt()'s
/// new spelling, or `name` itself when it does not move.
/// Cost: as respelt(), plus one allocation for a name that does not move.
/// Example: reowned("Trait", "Target", "_Trait__secret") is "_Target__secret"; reowned("Trait",
/// "Target", "value") is "value".
constexpr std::string reowned(std::string_view from, std::string_view to, std::string_view name) {
    auto moved = respelt(from, to, name);
    return moved ? std::move(*moved) : std::string{name};
}

/// `qualname` with the qualified name of its owner, `from`, replaced by `to`. A qualified name that
/// does not start with `from` followed by a dot is unchanged.
/// Cost: one prefix comparison; one allocation of the result.
/// Example: requalified("Trait", "Target", "Trait.f") is "Target.f"; "TraitX.f" stays as it is.
constexpr std::string requalified(std::string_view from, std::string_view to, std::string_view qualname) {
    if (qualname.size() <= from.size() || !qualname.starts_with(from) || qualname[from.size()] != '.')
        return std::string{qualname};
    std::string out;
    out.reserve(to.size() + qualname.size() - from.size());
    out += to;
    out += qualname.substr(from.size());
    return out;
}

/// The naming rules gathered for one class. Constructed with the class's name — and, where a
/// qualified name or module is in play, those too — it answers the forms that depend on a class:
/// mangling a private attribute, recognising one, and moving a name or a qualified name to another
/// class. The class-independent rules (``is_dunder``, ``is_private_name``) stay free functions,
/// because they ask about a name's shape, not about any class.
///
/// It is a thin value over the free functions above (the ones the compile-time proofs check), so it
/// adds cohesion, not logic: a class's identity travels as one object instead of parallel strings,
/// and a move reads as ``source.reowned(target, name)``. It holds ``string_view``s and owns nothing,
/// so the text it is built from must outlive it.
///
/// Example: ``NamePolicy("daa").mangled("__attr")`` is ``"_daa__attr"``;
/// ``NamePolicy("Inner", "Outer.Inner", "pkg.mod").qualified_name()`` is ``"pkg.mod.Outer.Inner"``.
class NamePolicy {
public:
    constexpr explicit NamePolicy(std::string_view class_name, std::string_view qualname = {},
                                  std::string_view module = {}) noexcept
        : m_class(class_name), m_qualname(qualname.empty() ? class_name : qualname), m_module(module) {}

    constexpr std::string_view class_name() const noexcept { return m_class; }
    constexpr std::string_view qualname() const noexcept { return m_qualname; }
    constexpr std::string_view owner() const noexcept { return owner_of(m_class); }

    /// `attr` as Python spells it inside this class: ``mangled("__x")`` is ``_Class__x``.
    [[nodiscard]] constexpr std::string mangled(std::string_view attr) const {
        return python_names::mangled(m_class, attr);
    }
    /// The private name whose spelling in this class is `name`, or "" when `name` is not one of this
    /// class's private names.
    [[nodiscard]] constexpr std::string_view unmangled(std::string_view name) const noexcept {
        return python_names::unmangled(m_class, name);
    }
    /// `name` moved from this class into `into`: its new spelling, or nothing when it does not move.
    [[nodiscard]] constexpr std::optional<std::string> respelt(const NamePolicy& into, std::string_view name) const {
        return python_names::respelt(m_class, into.m_class, name);
    }
    /// `name` moved from this class into `into`: the new spelling, or `name` itself when it does not move.
    [[nodiscard]] constexpr std::string reowned(const NamePolicy& into, std::string_view name) const {
        return python_names::reowned(m_class, into.m_class, name);
    }
    /// `qualname` re-rooted from this class's qualified name onto `into`'s.
    [[nodiscard]] constexpr std::string requalified(const NamePolicy& into, std::string_view qualname) const {
        return python_names::requalified(m_qualname, into.m_qualname, qualname);
    }
    /// This class's fully-qualified name, ``module.qualname`` (empty module: just the qualified name).
    [[nodiscard]] constexpr std::string qualified_name() const {
        if (m_module.empty()) return std::string{m_qualname};
        std::string out;
        out.reserve(m_module.size() + 1 + m_qualname.size());
        out += m_module;
        out += '.';
        out += m_qualname;
        return out;
    }

private:
    std::string_view m_class;
    std::string_view m_qualname;
    std::string_view m_module;
};

/// Code moved from class to class, in order — reown(A, B), then reown(B, C) — and what that does to
/// each name the code spells. Each move is NamePolicy's rule; this applies them in order. Unlike
/// NamePolicy it owns the classes' names, so it can be kept: CodeBuilder holds one while it builds,
/// and code nested at any depth reads the same one.
///
/// Example: Reowning{}.then(NamePolicy{"Trait"}, NamePolicy{"Target"}) respells "_Trait__secret" as
/// "_Target__secret", requalifies "Trait.f" as "Target.f", and leaves "value" alone (nothing).
class Reowning {
public:
    constexpr Reowning() = default;  // no moves: every name stays as it is

    /// This reowning followed by a move from class `from` to class `to`, whose names are copied.
    /// A value: this one is left as it was.
    [[nodiscard]] constexpr Reowning then(const NamePolicy& from, const NamePolicy& to) const {
        Reowning out = *this;
        out.m_moves.push_back(Move{from, to});
        return out;
    }

    /// `name` after every move, in order: its new spelling, or nothing when no move changes it. Each
    /// move reads the spelling the ones before it left, so code in A that names B's private attribute
    /// is moved on by a following B -> C (the exception python_names_proofs.cpp states).
    [[nodiscard]] constexpr std::optional<std::string> respelt(std::string_view name) const {
        std::optional<std::string> spelling;
        for (const Move& move : m_moves)
            if (auto next = move.respelt(spelling ? std::string_view{*spelling} : name)) spelling = std::move(next);
        return spelling;
    }

    /// `qualname` re-rooted by every move, in order: "Trait.f.<locals>.g" moved to Target is
    /// "Target.f.<locals>.g".
    [[nodiscard]] constexpr std::string requalified(std::string_view qualname) const {
        std::string out{qualname};
        for (const Move& move : m_moves) out = move.requalified(out);
        return out;
    }

private:
    /// One move, owning both classes' names; the NamePolicy views over them are made per call.
    class Move {
    public:
        constexpr Move(const NamePolicy& from, const NamePolicy& to)
            : m_from_name(from.class_name()),
              m_from_qualname(from.qualname()),
              m_to_name(to.class_name()),
              m_to_qualname(to.qualname()) {}

        [[nodiscard]] constexpr std::optional<std::string> respelt(std::string_view name) const {
            return from().respelt(to(), name);
        }
        [[nodiscard]] constexpr std::string requalified(std::string_view qualname) const {
            return from().requalified(to(), qualname);
        }

    private:
        [[nodiscard]] constexpr NamePolicy from() const { return NamePolicy{m_from_name, m_from_qualname}; }
        [[nodiscard]] constexpr NamePolicy to() const { return NamePolicy{m_to_name, m_to_qualname}; }

        std::string m_from_name;
        std::string m_from_qualname;
        std::string m_to_name;
        std::string m_to_qualname;
    };

    std::vector<Move> m_moves;
};

}  // namespace pygim::python_names
