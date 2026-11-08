// traits/adapter/member.h — the kinds of member, one class each, held by Member; and TraitMove::planned,
// which classifies a trait's members with them.

#pragma once

#include <format>
#include <optional>
#include <utility>
#include <variant>

#include <pybind11/pybind11.h>

#include "../../utils/python_names.h"
#include "introspection.h"
#include "plan.h"
#include "trait_move.h"

namespace pygim::traits {

namespace py = pybind11;

namespace detail {

/// The code of `value` when it is a Python function; None for anything else (None, a builtin).
inline py::object code_or_none(const py::object& value) {
    return PyFunction_Check(value.ptr()) ? py::object(value.attr("__code__")) : py::object(py::none());
}

// ── the kinds of member, one class each ───────────────────────────────────────────────────────

/// A plain function: rebuilt for the target.
class FunctionMember {
public:
    static bool is(const py::handle& value) { return PyFunction_Check(value.ptr()); }
    explicit FunctionMember(py::object function) : m_function(std::move(function)) {}

    [[nodiscard]] py::object rewritten(const TraitMove& trait) const { return trait.function(m_function); }
    [[nodiscard]] py::object code() const { return m_function.attr("__code__"); }

private:
    py::object m_function;
};

/// A property: each accessor rebuilt; a property subclass stays that subclass.
class PropertyMember {
public:
    static bool is(const py::handle& value) { return PyObject_TypeCheck(value.ptr(), &PyProperty_Type); }
    explicit PropertyMember(py::object property) : m_property(std::move(property)) {}

    [[nodiscard]] py::object rewritten(const TraitMove& trait) const {
        return py::type::of(m_property)(trait.function_or_same(accessor("fget")),
                                        trait.function_or_same(accessor("fset")),
                                        trait.function_or_same(accessor("fdel")), m_property.attr("__doc__"));
    }
    [[nodiscard]] py::object code() const { return code_or_none(accessor("fget")); }

private:
    [[nodiscard]] py::object accessor(const char* name) const { return m_property.attr(name); }
    py::object m_property;
};

/// A function in a wrapper whose type `Wrapper` names — a classmethod or a staticmethod: the function
/// inside rebuilt, and a wrapper of the same type made again around it. Each wrapper is a class of
/// its own (the aliases below); one that needs behaviour of its own gets a specialization.
template <class Wrapper>
class WrappedFunctionMember {
public:
    static bool is(const py::handle& value) { return PyObject_TypeCheck(value.ptr(), Wrapper::type()); }
    explicit WrappedFunctionMember(py::object method) : m_method(std::move(method)) {}

    [[nodiscard]] py::object rewritten(const TraitMove& trait) const {
        return py::type::of(m_method)(trait.function_or_same(function()));
    }
    [[nodiscard]] py::object code() const { return code_or_none(function()); }

private:
    [[nodiscard]] py::object function() const { return m_method.attr("__func__"); }
    py::object m_method;
};

// The wrapper's type is returned by a function, not taken as a template argument: on Windows the
// type objects are imported from the Python DLL, and such an address is no constant.

/// classmethod: the function inside rebuilt, so `cls` is the class it is called on.
class ClassMethodWrapper {
public:
    static PyTypeObject* type() { return &PyClassMethod_Type; }
};
using ClassMethodMember = WrappedFunctionMember<ClassMethodWrapper>;

/// staticmethod: the function inside rebuilt.
class StaticMethodWrapper {
public:
    static PyTypeObject* type() { return &PyStaticMethod_Type; }
};
using StaticMethodMember = WrappedFunctionMember<StaticMethodWrapper>;

/// A member of a trait as its kind — each kind a class with its own behaviour, chosen once, here.
/// Example: Member::of(Greeter.__dict__["greet"])->rewritten(trait) is greet rebuilt for the target.
class Member {
public:
    /// `value` as its kind; nothing when no kind composes it.
    static std::optional<Member> of(const py::object& value) { return classified(static_cast<Kinds*>(nullptr), value); }

    /// The member rewritten for the target `trait` moves into.
    [[nodiscard]] py::object rewritten(const TraitMove& trait) const {
        return std::visit([&](const auto& kind) { return kind.rewritten(trait); }, m_kind);
    }
    /// The code that says where the trait defined it; None for a member with no Python code.
    [[nodiscard]] py::object code() const {
        return std::visit([](const auto& kind) { return kind.code(); }, m_kind);
    }

private:
    using Kinds = std::variant<FunctionMember, PropertyMember, ClassMethodMember, StaticMethodMember>;
    explicit Member(Kinds kind) : m_kind(std::move(kind)) {}

    // The first kind whose is() accepts `value`, folded over the variant's kinds in order.
    template <class... Kind>
    static std::optional<Member> classified(std::variant<Kind...>*, const py::object& value) {
        std::optional<Member> out;
        ((Kind::is(value) && (out = Member{Kinds{std::in_place_type<Kind>, value}}, true)) || ...);
        return out;
    }

    Kinds m_kind;
};

inline std::optional<PlannedMember> TraitMove::planned(const py::handle& key, const py::handle& value) const {
    auto [name, name_obj] = attribute_name(key);
    const auto member = Member::of(py::reinterpret_borrow<py::object>(value));
    if (!member) {
        if (python_names::is_dunder(name)) return std::nullopt;  // the trait class's own machinery
        throw py::type_error(std::format(
            "{}.{} is {}; a trait composes functions, properties, classmethods and staticmethods",
            qualname(), name, type_name(value)));
    }
    py::object rewritten = member->rewritten(*this);
    Provenance origin = attribution(name, value, member->code());
    return PlannedMember{std::move(name), std::move(name_obj), std::move(rewritten), std::move(origin), m_name};
}

}  // namespace detail

}  // namespace pygim::traits
