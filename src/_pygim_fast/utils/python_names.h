// utils/python_names.h — Python's naming conventions: what kind of name a name is, and how Python
// spells a private name inside a class. Dependency-free, pybind-free and constexpr: a base header.
//
// One family of rules, each matched to the place Python itself applies it (global #113):
//   is_dunder        enum._is_dunder — the rule Python's enum machinery uses
//   is_private_name  the compiler's _Py_Mangle: which names it mangles inside a class body
//   mangled          _Py_Mangle: `__secret` inside `class Trait:` is `_Trait__secret`
// and the moves built on them: unmangled (back), reowned (to another class), requalified (a
// qualified name to another owner). Users: utils (pygim.utils.is_dunder), each (its dunder guard),
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
//   reowned("Trait", "Target", "_Trait__secret")       -> "_Target__secret"
//   reowned("Trait", "Target", "value")                -> "value"           a public name stays
//   requalified("Trait", "Target", "Trait.f.<locals>.<lambda>") -> "Target.f.<locals>.<lambda>"
//
// Lifetime and threading: free functions over string_views; nothing is stored, nothing is shared.

#pragma once

#include <string>
#include <string_view>

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

/// `name`, taken from code compiled in class `from` into code that belongs to class `to`: a private
/// name of `from` becomes the same private name of `to`; every other name is unchanged.
/// Cost: one unmangle; one allocation of the result.
/// Example: reowned("Trait", "Target", "_Trait__secret") is "_Target__secret".
constexpr std::string reowned(std::string_view from, std::string_view to, std::string_view name) {
    const auto private_name = unmangled(from, name);
    return private_name.empty() ? std::string{name} : mangled(to, private_name);
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

}  // namespace pygim::python_names
