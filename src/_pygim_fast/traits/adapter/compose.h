// traits/adapter/compose.h — compose(): a trait's members copied into a class, rewritten to belong there;
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
// Composing a class that was itself composed into carries its record along: each member keeps the
// trait it first came from and where that trait defined it; only `composed` names the new call. A
// member the class defined itself is attributed to that class.
//
// Lifetime and threading: a class's record is kept off the class, in a side table keyed by the class's
// identity, and removed when the class is collected (detail::RecordStore); a class composed into gets a
// record of its own, never its base's. compose() holds the GIL throughout, as every pybind11 call does.

#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "../../wiring/registry/core.h"
#include "code_builder.h"
#include "../../utils/aliases.h"
#include "../../utils/python_names.h"

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
    [[nodiscard]] pygim::Strings names() const { return m_core.keys(); }

private:
    core::StaticRegistryCore<std::string, Provenance> m_core;
};

namespace detail {

inline std::string text(const py::handle& value) { return py::str(value).cast<std::string>(); }
inline std::string type_name(const py::handle& value) { return text(py::type::of(value).attr("__name__")); }

/// "file:line" of a code object; "" for None.
inline std::string where(const py::handle& code) {
    if (code.is_none()) return {};
    return std::format("{}:{}", text(code.attr("co_filename")), text(code.attr("co_firstlineno")));
}

/// "file:line" of the Python code that called into C++: the innermost Python frame, which a pybind11
/// call does not add to — what sys._getframe(0) answers, read through the C API. "" with no Python
/// frame running.
inline std::string caller() {
    PyFrameObject* frame = PyEval_GetFrame();  // borrowed
    if (frame == nullptr) return {};
    const auto code = py::reinterpret_steal<py::object>(reinterpret_cast<PyObject*>(PyFrame_GetCode(frame)));
    return std::format("{}:{}", text(code.attr("co_filename")), PyFrame_GetLineNumber(frame));
}

/// One trait's functions moved into one class. Example: Move(Greeter, Person).function(Greeter.greet)
/// is greet rebuilt for Person — reading `_Person__name` where it read `_Greeter__name`, with the
/// qualified name `Person.greet`.
class Move {
public:
    Move(py::type trait, py::type target)
        : m_trait(std::move(trait)),
          m_target(std::move(target)),
          m_trait_qualname(text(m_trait.attr("__qualname__"))),
          m_target_qualname(text(m_target.attr("__qualname__"))) {}

    /// `function`, rebuilt to belong to the target: its code by CodeBuilder (private and qualified
    /// names, nested code too), its __class__ cell rebound so zero-argument super() resolves in the
    /// target, its qualified name moved. Defaults, keyword defaults, annotations, docstring, module
    /// and __dict__ come along unchanged. Built as types.FunctionType builds one, through the C API.
    [[nodiscard]] py::object function(const py::handle& function) const {
        const py::object code = CodeBuilder(function.attr("__code__")).reown(m_trait, m_target).build();
        const py::object globals = function.attr("__globals__");
        const py::str qualname(qualname_in_target(function));
        auto out = py::reinterpret_steal<py::object>(
            PyFunction_NewWithQualName(code.ptr(), globals.ptr(), qualname.ptr()));
        if (!out) throw py::error_already_set();
        const py::object closure = closure_in_target(function, code);
        if (PyFunction_SetClosure(out.ptr(), closure.ptr()) != 0) throw py::error_already_set();
        for (const char* attribute : {"__name__", "__defaults__", "__kwdefaults__", "__doc__", "__module__"})
            out.attr(attribute) = function.attr(attribute);
        out.attr("__dict__").attr("update")(function.attr("__dict__"));
        if (py::hasattr(function, "__annotate__"))  // Python 3.14: annotations are evaluated lazily
            out.attr("__annotate__") = function.attr("__annotate__");
        else
            out.attr("__annotations__") = py::dict(function.attr("__annotations__"));
        if (py::hasattr(function, "__type_params__"))  // Python 3.12 and later
            out.attr("__type_params__") = function.attr("__type_params__");
        return out;
    }

    /// `value` rebuilt when it is a Python function; anything else (None, a builtin) as it is.
    [[nodiscard]] py::object function_or_same(const py::object& value) const {
        return PyFunction_Check(value.ptr()) ? function(value) : value;
    }

private:
    /// The function's qualified name re-rooted from the trait onto the target: `Greeter.greet` is
    /// `Person.greet`. The move keys on the qualified name, so it is each policy's class name too.
    [[nodiscard]] std::string qualname_in_target(const py::handle& function) const {
        const python_names::NamePolicy from{m_trait_qualname, m_trait_qualname};
        const python_names::NamePolicy to{m_target_qualname, m_target_qualname};
        return from.requalified(to, text(function.attr("__qualname__")));
    }

    /// The function's closure with its __class__ cell, if any, replaced by a cell holding the target;
    /// None for a function without one. Checked against the rebuilt code's free variables, which
    /// types.FunctionType would have checked and PyFunction_SetClosure does not.
    [[nodiscard]] py::object closure_in_target(const py::handle& function, const py::object& code) const {
        const py::object closure = function.attr("__closure__");
        const py::tuple freevars = code.attr("co_freevars");
        if (closure.is_none()) {
            if (freevars.empty()) return closure;
            throw py::value_error(std::format("{} needs a closure of {} cells and has none",
                                              text(function.attr("__qualname__")), freevars.size()));
        }
        const py::tuple cells = closure;
        if (cells.size() != freevars.size())
            throw py::value_error(std::format("{} has {} closure cells for {} free variables",
                                              text(function.attr("__qualname__")), cells.size(), freevars.size()));
        py::tuple rebound(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (text(freevars[i]) != "__class__") {
                rebound[i] = cells[i];
                continue;
            }
            PyObject* cell = PyCell_New(m_target.ptr());
            if (cell == nullptr) throw py::error_already_set();
            rebound[i] = py::reinterpret_steal<py::object>(cell);
        }
        return rebound;
    }

    py::type m_trait;
    py::type m_target;
    std::string m_trait_qualname;   // owned here: the name policies above view them
    std::string m_target_qualname;
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

/// The records of the classes composed into, kept off the classes themselves (R1 in
/// docs/design/traits.md, 2026-10-19).
///
/// CPython gives a class no private slot — its layout is its metaclass's, and the only per-class
/// storage it offers is the `__dict__` Python sees — so a record kept on the class is visible and can
/// collide with a member of the same name. Instead, the table functools.singledispatch uses, with one
/// change: keyed by the class's identity rather than its equality, so no metaclass `__hash__` or
/// `__eq__` is ever asked (one that lies cannot merge two classes' records). Each entry holds a weak
/// reference to its class whose callback removes the entry while the class is being destroyed — before
/// its memory is freed, so a new class at the same address (CPython reuses it almost every time) starts
/// with no record. A class made by gim_type will carry its record as a member instead; this table is
/// for every other class.
///
/// A RegistryCore over the hashed engine (#131), keyed by the class's address. Allocated once and never
/// destroyed: its weak references must not be released after Python has finalised. Every call holds
/// the GIL.
class RecordStore {
public:
    /// The record of `cls`, or nullptr when nothing was composed into it.
    [[nodiscard]] const TraitRecord* find(const py::handle& cls) const {
        const Entry* found = m_core.try_get_const(cls.ptr());
        return found == nullptr ? nullptr : &found->record;
    }

    /// The record of `cls`, created empty — with the weak reference that removes it — on first use.
    /// The reference stays valid until `cls` is collected: the hashed engine's entries are nodes,
    /// untouched by other inserts and removals.
    TraitRecord& at(const py::handle& cls) {
        PyObject* key = cls.ptr();
        if (Entry* found = m_core.try_get(key)) return found->record;
        const py::cpp_function evict([this, key](const py::handle&) { m_core.remove(key); });
        PyObject* guard = PyWeakref_NewRef(key, evict.ptr());
        if (guard == nullptr) throw py::error_already_set();
        m_core.register_value(key, Entry{TraitRecord{}, py::reinterpret_steal<py::object>(guard)});
        return m_core.try_get(key)->record;
    }

private:
    struct Entry {
        TraitRecord record;
        py::object guard;  // the weak reference to the class; its callback removes this entry
    };
    core::DynamicRegistryCore<PyObject*, Entry> m_core;
};

inline RecordStore& records() {
    static auto* const store = new RecordStore();
    return *store;
}

/// The attribute name a dict key spells, by its real characters, and that name as a fresh str. The
/// characters come straight from the key, so a str subclass cannot change them through __str__ and
/// an embedded NUL is kept whole; the fresh str is what compose() writes, so the subclass instance
/// is not stored as the attribute name. A key that is not a str is refused: a name must be one.
inline std::pair<std::string, py::object> attribute_name(const py::handle& key) {
    if (!PyUnicode_Check(key.ptr()))
        throw py::type_error(std::format("a trait member's name must be a str, not {}", type_name(key)));
    Py_ssize_t size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(key.ptr(), &size);  // raises on a lone surrogate (not UTF-8)
    if (data == nullptr) throw py::error_already_set();
    py::object canonical = py::reinterpret_steal<py::object>(PyUnicode_FromStringAndSize(data, size));
    if (!canonical) throw py::error_already_set();
    return {std::string(data, static_cast<std::size_t>(size)), std::move(canonical)};
}

/// A member planned for the target: rewritten, under the name it will be written as, and attributed
/// to the trait it came from. Nothing is written until every member of the call is planned.
class PlannedMember {
public:
    PlannedMember(std::string name, py::object name_obj, py::object member, std::string trait, std::string defined)
        : m_name(std::move(name)),
          m_name_obj(std::move(name_obj)),
          m_member(std::move(member)),
          m_trait(std::move(trait)),
          m_defined(std::move(defined)) {}

    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const std::string& trait() const noexcept { return m_trait; }

    /// Set the member on `target`; raises what the write raises (a slot Python refuses, a hook).
    void write_into(const py::type& target) const {
        if (PyObject_SetAttr(target.ptr(), m_name_obj.ptr(), m_member.ptr()) != 0) throw py::error_already_set();
    }

    /// Note the member in `record`, composed at `composed` ("file:line").
    void note_into(TraitRecord& record, const std::string& composed) const {
        record.note(m_name, {m_trait, m_defined, composed});
    }

private:
    std::string m_name;
    py::object m_name_obj;  // the name as a canonical str: what is written, NUL and all
    py::object m_member;
    std::string m_trait;    // the trait it came from: module and qualified name
    std::string m_defined;  // "file:line" where that trait defined it
};

/// One compose() call, in two passes so a refusal writes nothing: plan() each trait — every member
/// classified, checked and rewritten, every refusal raised — then commit() the plan.
class Composition {
public:
    Composition(py::type target, bool override_existing)
        : m_target(std::move(target)),
          m_override(override_existing),
          m_own(m_target.attr("__dict__")),
          m_target_name(text(m_target.attr("__qualname__"))) {}

    /// Plan every member of `candidate`. Refuses a candidate that is not a class, a member no kind
    /// composes, and a name that conflicts (admit()).
    void plan(const py::handle& candidate) {
        if (!PyType_Check(candidate.ptr()))
            throw py::type_error(std::format("compose takes classes as traits, not {}", type_name(candidate)));
        const auto trait = py::reinterpret_borrow<py::type>(candidate);
        const auto trait_qualname = text(trait.attr("__qualname__"));
        const auto trait_name = std::format("{}.{}", text(trait.attr("__module__")), trait_qualname);
        const Move move{trait, m_target};
        // A class that was itself composed into passes its members on with where they came from (R2).
        const TraitRecord* carried = records().find(trait);

        for (const auto& [key, member] : py::dict(trait.attr("__dict__"))) {
            auto [name, name_obj] = attribute_name(key);
            auto done = rewritten(static_cast<member_kinds*>(nullptr), member, move);
            if (!done) {
                if (python_names::is_dunder(name)) continue;  // the trait class's own machinery
                throw py::type_error(std::format(
                    "{}.{} is {}; a trait composes functions, properties, classmethods and staticmethods",
                    trait_qualname, name, type_name(member)));
            }
            const bool passed_on = carried != nullptr && carried->contains(name);
            const Provenance origin = passed_on ? carried->at(name) : Provenance{trait_name, where(done->code), {}};
            admit(PlannedMember{std::move(name), std::move(name_obj), std::move(done->member), origin.trait,
                                origin.defined},
                  trait_name);
        }
    }

    /// Write every planned member into the target, then note them in its record. With nothing planned
    /// the target is left exactly as it was. A write that fails leaves the members before it (no
    /// rollback, decided 2026-10-18) and no record: the record is noted only after every write.
    py::type commit() const {
        if (m_plan.empty()) return m_target;
        const auto composed = caller();
        for (const auto& member : m_plan)  // the member writes first: a write may fire a hook
            member.write_into(m_target);
        auto& record = records().at(m_target);  // in the side table: no hook can reach it
        for (const auto& member : m_plan)
            member.note_into(record, composed);
        return m_target;
    }

private:
    /// Add `member`, from `trait_name`, to the plan: a name an earlier trait of this call planned, or
    /// the target's own namespace holds, is refused unless override, which replaces (the later wins).
    void admit(PlannedMember member, const std::string& trait_name) {
        const auto earlier = std::ranges::find(m_plan, member.name(), &PlannedMember::name);
        if (earlier != m_plan.end()) {
            if (!m_override)
                throw py::value_error(std::format("{} and {} both define {} (pass override=True to let the later win)",
                                                  earlier->trait(), trait_name, member.name()));
            m_plan.erase(earlier);
        } else if (m_own.contains(member.name()) && !m_override) {
            throw py::value_error(std::format("{} already has {}; {} would replace it (pass override=True)",
                                              m_target_name, member.name(), trait_name));
        }
        m_plan.push_back(std::move(member));
    }

    py::type m_target;
    bool m_override;
    py::object m_own;            // the target's own namespace (a live view)
    std::string m_target_name;
    std::vector<PlannedMember> m_plan;
};

}  // namespace detail

/// Copy every member of every trait into `target`, rewritten to belong there, and record where each
/// came from. Refuses — writing nothing — a trait that is not a class, a member no kind composes,
/// a name `target` already has, or a name two traits define; `override=True` replaces instead (the
/// later trait winning). Returns `target`.
/// Cost: per member, one CodeBuilder build per function in it; per call, one frame lookup.
/// Example: compose(Person, Greeter) is Person, and Person("ann").greet() is 'hello ann'.
inline py::type compose(const py::type& target, const py::args& traits, bool override_existing) {
    detail::Composition composition{target, override_existing};
    for (const auto& trait : traits)
        composition.plan(trait);
    return composition.commit();
}

/// The record of what was composed into `cls` itself; empty for a class nothing was composed into.
/// Example: list(traits_of(Person)) is ['greet'].
inline py::object traits_of(const py::type& cls) {
    const TraitRecord* found = detail::records().find(cls);
    return py::cast(found != nullptr ? *found : TraitRecord{});  // a snapshot: the store stays private
}

}  // namespace pygim::traits
