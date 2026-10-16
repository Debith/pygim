// traits/compose.h — compose(): a trait's members copied into a class, rewritten to belong there;
// traits_of(): where each composed member came from.
//
// Worked example:
//
//   class Greeter:                                     # a trait: nothing inherits from it
//       def greet(self): return f"hello {self.__name}" # reads _Greeter__name
//   class Person:
//       def __init__(self, name): self.__name = name   # stores _Person__name
//
//   compose(Person, Greeter)            -> Person, with greet in Person's own namespace
//   Person("ann").greet()               -> 'hello ann'          the private name was respelt
//   Person.greet.__qualname__           -> 'Person.greet'
//   traits_of(Person)["greet"].trait    -> '<module>.Greeter'
//   compose(Person, Greeter)            -> ValueError: Person already has greet; ... (pass override=True)
//
// Two passes, so a refusal writes nothing: every member of every trait is classified, checked and
// rewritten first, and only then is anything set on the target. A name is a conflict when the
// target's OWN namespace holds it, or when two traits of one call both define it; an inherited name
// is not (every class inherits object.__lt__), and composing over it is what defining it would do.
//
// The kinds of member are a pack of descriptors, folded in order (pygim's #134): a kind says whether
// it matches, how it is rewritten and which code object says where it was defined. A member no kind
// matches is refused by name — unless it is a dunder, which is the trait class's own machinery
// (__module__, __doc__, __dict__) and stays behind.
//
// Lifetime and threading: a class's record is stored on the class as __pygim_traits__ and dies with
// it; a class composed into gets a record of its own, never its base's. compose() holds the GIL
// throughout, as every pybind11 call does.

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "../wiring/registry/core.h"
#include "adapter.h"
#include "core.h"

namespace pygim::traits {

namespace py = pybind11;

/// Where a composed member came from. Example, for Greeter.greet composed into Person:
/// {trait: 'tests.Greeter', defined: '/x/tests.py:12', composed: '/x/tests.py:40'}.
struct Provenance {
    std::string trait;     // the trait's module and qualified name
    std::string defined;   // "file:line" of the member's code in the trait ("" for code that is not Python's)
    std::string composed;  // "file:line" of the compose() call
};

/// The composed members of one class, by name, in name order: a RegistryCore over the flat engine.
/// Cost: a binary search to read; a sorted insert to note. Example: names() of Person above is ['greet'].
class TraitRecord {
public:
    /// Record `name`, replacing what was recorded under it (an override composes over it).
    void note(const std::string& name, Provenance provenance) { m_core.upsert_value(name, std::move(provenance)); }

    /// The provenance of `name`; KeyError when nothing was composed under it.
    [[nodiscard]] Provenance at(const std::string& name) const {
        const auto* found = m_core.try_get_const(name);
        if (found == nullptr) throw py::key_error(name);
        return *found;
    }

    [[nodiscard]] bool contains(const std::string& name) const { return m_core.contains(name); }
    [[nodiscard]] std::size_t size() const noexcept { return m_core.size(); }
    [[nodiscard]] std::vector<std::string> names() const { return m_core.keys(); }

private:
    core::StaticRegistryCore<std::string, Provenance> m_core;
};

namespace detail {

inline std::string text(const py::handle& value) { return py::str(value).cast<std::string>(); }
inline std::string type_name(const py::handle& value) { return text(py::type::of(value).attr("__name__")); }

/// "file:line" of a code object; "" for None.
inline std::string where(const py::handle& code) {
    if (code.is_none()) return {};
    return text(code.attr("co_filename")) + ":" + text(code.attr("co_firstlineno"));
}

/// "file:line" of the Python code that called into C++: sys._getframe(0) is that caller's frame.
inline std::string caller() {
    const py::object frame = py::module_::import("sys").attr("_getframe")(0);
    return text(frame.attr("f_code").attr("co_filename")) + ":" + text(frame.attr("f_lineno"));
}

/// One composition: the trait a member comes from and the class it goes to.
struct Move {
    py::type trait;
    py::type target;

    /// `function`, rebuilt to belong to `target`: its code by CodeBuilder (private and qualified
    /// names, nested code too), its __class__ cell rebound so zero-argument super() resolves in
    /// `target`, its qualified name moved. Defaults, keyword defaults, annotations, docstring,
    /// module and __dict__ come along unchanged.
    [[nodiscard]] py::object function(const py::handle& function) const {
        const py::object code = CodeBuilder(function.attr("__code__")).reown(trait, target).build();
        py::object closure = function.attr("__closure__");
        if (!closure.is_none()) {
            const py::tuple freevars = code.attr("co_freevars");
            const py::tuple cells = closure;
            py::list rebound;
            for (std::size_t i = 0; i < cells.size(); ++i) {
                if (text(freevars[i]) != "__class__") {
                    rebound.append(cells[i]);
                    continue;
                }
                PyObject* cell = PyCell_New(target.ptr());
                if (cell == nullptr) throw py::error_already_set();
                rebound.append(py::reinterpret_steal<py::object>(cell));
            }
            closure = py::tuple(rebound);
        }
        const auto function_type = py::module_::import("types").attr("FunctionType");
        py::object out = function_type(code, function.attr("__globals__"), function.attr("__name__"),
                                       function.attr("__defaults__"), closure);
        for (const char* attribute : {"__kwdefaults__", "__doc__", "__module__"})
            out.attr(attribute) = function.attr(attribute);
        out.attr("__dict__").attr("update")(function.attr("__dict__"));
        if (py::hasattr(function, "__annotate__"))  // Python 3.14: annotations are evaluated lazily
            out.attr("__annotate__") = function.attr("__annotate__");
        else
            out.attr("__annotations__") = py::dict(function.attr("__annotations__"));
        if (py::hasattr(function, "__type_params__"))  // Python 3.12 and later
            out.attr("__type_params__") = function.attr("__type_params__");
        out.attr("__qualname__") = py::str(requalified(text(trait.attr("__qualname__")),
                                                       text(target.attr("__qualname__")),
                                                       text(function.attr("__qualname__"))));
        return out;
    }

    /// `value` rebuilt when it is a Python function; anything else (None, a builtin) as it is.
    [[nodiscard]] py::object function_or_same(const py::object& value) const {
        return PyFunction_Check(value.ptr()) ? function(value) : value;
    }
};

inline py::object code_or_none(const py::object& value) {
    return PyFunction_Check(value.ptr()) ? py::object(value.attr("__code__")) : py::object(py::none());
}

// ── the kinds of member, one descriptor each ───────────────────────────────────────────────────

/// A plain function.
struct function_member {
    static bool matches(const py::handle& m) { return PyFunction_Check(m.ptr()); }
    static py::object rewritten(const py::handle& m, const Move& move) { return move.function(m); }
    static py::object code(const py::handle& m) { return m.attr("__code__"); }
};

/// A property: each accessor rebuilt; a property subclass stays that subclass.
struct property_member {
    static bool matches(const py::handle& m) { return PyObject_TypeCheck(m.ptr(), &PyProperty_Type); }
    static py::object rewritten(const py::handle& m, const Move& move) {
        return py::type::of(m)(move.function_or_same(m.attr("fget")), move.function_or_same(m.attr("fset")),
                               move.function_or_same(m.attr("fdel")), m.attr("__doc__"));
    }
    static py::object code(const py::handle& m) { return code_or_none(m.attr("fget")); }
};

/// A classmethod: the function inside rebuilt, so `cls` is the class it is called on.
struct classmethod_member {
    static bool matches(const py::handle& m) { return PyObject_TypeCheck(m.ptr(), &PyClassMethod_Type); }
    static py::object rewritten(const py::handle& m, const Move& move) {
        return py::type::of(m)(move.function_or_same(m.attr("__func__")));
    }
    static py::object code(const py::handle& m) { return code_or_none(m.attr("__func__")); }
};

/// A staticmethod: the function inside rebuilt.
struct staticmethod_member {
    static bool matches(const py::handle& m) { return PyObject_TypeCheck(m.ptr(), &PyStaticMethod_Type); }
    static py::object rewritten(const py::handle& m, const Move& move) {
        return py::type::of(m)(move.function_or_same(m.attr("__func__")));
    }
    static py::object code(const py::handle& m) { return code_or_none(m.attr("__func__")); }
};

using member_kinds = std::tuple<function_member, property_member, classmethod_member, staticmethod_member>;

/// A member rewritten for the target, and the code that says where the trait defined it.
struct Rewritten {
    py::object member;
    py::object code;
};

template <class Kind>
bool rewrite_if(const py::handle& member, const Move& move, std::optional<Rewritten>& out) {
    if (!Kind::matches(member)) return false;
    out = Rewritten{Kind::rewritten(member, move), Kind::code(member)};
    return true;
}

/// The member rewritten by the first kind that matches it; nothing when no kind does.
template <class... Kinds>
std::optional<Rewritten> rewritten(std::tuple<Kinds...>*, const py::handle& member, const Move& move) {
    std::optional<Rewritten> out;
    (rewrite_if<Kinds>(member, move, out) || ...);
    return out;
}

/// The record stored on `target` itself, created when it has none of its own.
inline TraitRecord& own_record(const py::type& target) {
    const py::object own = target.attr("__dict__");
    if (!own.contains("__pygim_traits__")) py::setattr(target, "__pygim_traits__", py::cast(TraitRecord{}));
    const py::object record = own["__pygim_traits__"];
    return record.cast<TraitRecord&>();
}

}  // namespace detail

/// Copy every member of every trait into `target`, rewritten to belong there, and record where each
/// came from. Refuses — writing nothing — a trait that is not a class, a member no kind composes,
/// a name `target` already has, or a name two traits define; `override=True` replaces instead (the
/// later trait winning). Returns `target`.
/// Cost: per member, one CodeBuilder build per function in it; per call, one frame lookup.
/// Example: compose(Person, Greeter) is Person, and Person("ann").greet() is 'hello ann'.
inline py::type compose(const py::type& target, const py::args& traits, bool override_existing) {
    struct Planned {
        std::string name;
        py::object member;
        std::string trait;
        std::string defined;
    };
    std::vector<Planned> plan;
    const py::object own = target.attr("__dict__");
    const auto target_name = detail::text(target.attr("__qualname__"));

    for (const auto& candidate : traits) {
        if (!PyType_Check(candidate.ptr()))
            throw py::type_error("compose takes classes as traits, not " + detail::type_name(candidate));
        const auto trait = py::reinterpret_borrow<py::type>(candidate);
        const auto trait_qualname = detail::text(trait.attr("__qualname__"));
        const auto trait_name = detail::text(trait.attr("__module__")) + "." + trait_qualname;
        const detail::Move move{trait, target};

        for (const auto& [key, member] : py::dict(trait.attr("__dict__"))) {
            const auto name = detail::text(key);
            auto done = detail::rewritten(static_cast<detail::member_kinds*>(nullptr), member, move);
            if (!done) {
                if (is_dunder_name(name)) continue;  // the trait class's own machinery
                throw py::type_error(trait_qualname + "." + name + " is " + detail::type_name(member) +
                                     "; a trait composes functions, properties, classmethods and staticmethods");
            }
            const auto earlier = std::find_if(plan.begin(), plan.end(), [&](const Planned& p) { return p.name == name; });
            if (earlier != plan.end()) {
                if (!override_existing)
                    throw py::value_error(earlier->trait + " and " + trait_name + " both define " + name +
                                          " (pass override=True to let the later win)");
                plan.erase(earlier);
            } else if (own.contains(name) && !override_existing) {
                throw py::value_error(target_name + " already has " + name + "; " + trait_name +
                                      " would replace it (pass override=True)");
            }
            plan.push_back({name, std::move(done->member), trait_name, detail::where(done->code)});
        }
    }

    const auto composed = detail::caller();
    auto& record = detail::own_record(target);
    for (auto& planned : plan) {
        py::setattr(target, planned.name.c_str(), planned.member);
        record.note(planned.name, {planned.trait, planned.defined, composed});
    }
    return target;
}

/// The record of what was composed into `cls` itself; empty for a class nothing was composed into.
/// Example: list(traits_of(Person)) is ['greet'].
inline py::object traits_of(const py::type& cls) {
    const py::object own = cls.attr("__dict__");
    if (own.contains("__pygim_traits__")) return own["__pygim_traits__"];
    return py::cast(TraitRecord{});
}

}  // namespace pygim::traits
