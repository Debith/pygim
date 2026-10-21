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
// The kinds of member are classes, each with its own behaviour, held by Member (a std::variant,
// pygim's #134): a kind says whether it is one, how it is rewritten and which code object says where
// it was defined. A member no kind takes is refused by name — unless it is a dunder, which is the
// trait class's own machinery (__module__, __doc__, __dict__) and stays behind.
//
// Composing a class that was itself composed into carries its record along: each member keeps the
// trait it first came from and where that trait defined it; only `composed` names the new call. A
// member the class defined itself — or replaced since it was composed — is attributed to that class.
// The record is accurate: it lists only the members the class still holds as compose() left them.
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
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/pybind11.h>

#include "../../wiring/registry/core.h"
#include "code_builder.h"
#include "../../utils/aliases.h"
#include "../../utils/adapter_utils.h"
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
/// Each entry keeps the object compose() left in the class, so the record can tell a member still
/// composed from one replaced or deleted since (D15: traits_of reports only the former).
/// Cost: a binary search to read; a sorted insert to note. Example: names() of Person above is ['greet'].
class TraitRecord {
public:
    /// Record `name` as composed, holding `member` — what the class's own namespace held right after
    /// the write — and replacing what was recorded under it (an override composes over it).
    void note(const std::string& name, Provenance provenance, py::object member) {
        m_core.upsert_value(name, Composed{std::move(provenance), std::move(member)});
    }

    /// The provenance of `name`; KeyError when nothing was composed under it.
    [[nodiscard]] Provenance at(const std::string& name) const {
        const auto* found = m_core.try_get_const(name);
        if (found == nullptr) throw py::key_error(name);
        return found->provenance;
    }

    [[nodiscard]] bool contains(const std::string& name) const { return m_core.contains(name); }
    [[nodiscard]] std::size_t size() const noexcept { return m_core.size(); }
    [[nodiscard]] pygim::Strings names() const { return m_core.keys(); }

    /// Whether `member` is still what compose() left under `name`, compared by identity.
    [[nodiscard]] bool holds(const std::string& name, const py::handle& member) const {
        const auto* found = m_core.try_get_const(name);
        return found != nullptr && found->member.is(member);
    }

    /// This record as it stands in a class whose own namespace is `own`: only the members still there
    /// as compose() left them. Example: after `Person.greet = other`, greet is not in it.
    [[nodiscard]] TraitRecord standing_in(const py::dict& own) const {
        TraitRecord out;
        for (const auto& [name, composed] : m_core.storage().items()) {
            const py::str key(name);
            PyObject* current = PyDict_GetItemWithError(own.ptr(), key.ptr());  // borrowed; no hook is asked
            if (current == nullptr && PyErr_Occurred()) throw py::error_already_set();
            if (current == composed.member.ptr()) out.m_core.upsert_value(name, composed);
        }
        return out;
    }

private:
    struct Composed {
        Provenance provenance;
        py::object member;  // the object compose() left in the class's own namespace
    };
    core::StaticRegistryCore<std::string, Composed> m_core;
};

namespace detail {

inline std::string text(const py::handle& value) { return py::str(value).cast<std::string>(); }
inline std::string type_name(const py::handle& value) { return text(py::type::of(value).attr("__name__")); }

/// A class's own namespace, through the C API: the dict itself, so no metaclass attribute named
/// `__dict__` and no descriptor is asked. compose() reads every class namespace this way — the
/// conflict check, the members of a trait, the record — so a metaclass cannot show it one thing and
/// the writes another. Example: own_namespace(Person)["greet"] is Person.greet's function.
inline py::dict own_namespace(const py::handle& cls) {
#if PY_VERSION_HEX >= 0x030C0000
    PyObject* dict = PyType_GetDict(reinterpret_cast<PyTypeObject*>(cls.ptr()));  // new reference
    return dict != nullptr ? py::reinterpret_steal<py::dict>(dict) : py::dict();
#else
    PyObject* dict = reinterpret_cast<PyTypeObject*>(cls.ptr())->tp_dict;           // borrowed
    return dict != nullptr ? py::reinterpret_borrow<py::dict>(dict) : py::dict();
#endif
}

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

/// A member planned for the target: rewritten, under the name it will be written as, attributed to
/// where it came from, and naming the trait of this call that brings it (for a refusal's message).
/// Nothing is written until every member of the call is planned.
class PlannedMember {
public:
    PlannedMember(std::string name, py::object name_obj, py::object member, Provenance origin, std::string from)
        : m_name(std::move(name)),
          m_name_obj(std::move(name_obj)),
          m_member(std::move(member)),
          m_origin(std::move(origin)),
          m_from(std::move(from)) {}

    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const py::object& name_object() const noexcept { return m_name_obj; }
    /// The trait it first came from (R2): the trait of this call, or one that trait was composed from.
    [[nodiscard]] const std::string& trait() const noexcept { return m_origin.trait; }
    /// The trait of this call that brings it.
    [[nodiscard]] const std::string& from() const noexcept { return m_from; }

    /// Set the member on `target`; raises what the write raises (a slot Python refuses, a hook).
    void write_into(const py::type& target) const {
        if (PyObject_SetAttr(target.ptr(), m_name_obj.ptr(), m_member.ptr()) != 0) throw py::error_already_set();
    }

    /// Note the member in `record`, composed at `composed` ("file:line"), holding what landed under its
    /// name in `own`, the class's namespace after the writes — a metaclass hook may have stored
    /// something else, or nothing, in which case there is nothing to note.
    void note_into(TraitRecord& record, const std::string& composed, const py::dict& own) const {
        PyObject* landed = PyDict_GetItemWithError(own.ptr(), m_name_obj.ptr());  // borrowed
        if (landed == nullptr) {
            if (PyErr_Occurred()) throw py::error_already_set();
            return;
        }
        record.note(m_name, {m_origin.trait, m_origin.defined, composed}, py::reinterpret_borrow<py::object>(landed));
    }

private:
    std::string m_name;
    py::object m_name_obj;  // the name as a canonical str: what is written, NUL and all
    py::object m_member;
    Provenance m_origin;    // the trait it first came from and where that trait defined it
    std::string m_from;     // the trait of this call that brings it: module and qualified name
};

/// A Python function made again with a new code object and qualified name, as types.FunctionType
/// makes one, through the C API. Everything else it carries comes along as it is: defaults, keyword
/// defaults, docstring, module, __dict__, annotations, type parameters. With a class cell, the
/// closure's __class__ cell holds that class.
/// Example: FunctionCopy(f).with_code(code).with_qualname("Person.greet").with_class_cell(Person).make().
class FunctionCopy {
public:
    explicit FunctionCopy(const py::handle& function) : m_function(py::reinterpret_borrow<py::object>(function)) {}

    FunctionCopy& with_code(py::object code) {
        m_code = std::move(code);
        return *this;
    }
    FunctionCopy& with_qualname(std::string qualname) {
        m_qualname = std::move(qualname);
        return *this;
    }
    FunctionCopy& with_class_cell(py::type cls) {
        m_class = std::move(cls);
        return *this;
    }

    [[nodiscard]] py::object make() const {
        py::object copy = made();
        set_closure(copy);
        carry_attributes(copy);
        return copy;
    }

private:
    /// The new function: the code, the original's globals, the qualified name.
    [[nodiscard]] py::object made() const {
        const py::object globals = m_function.attr("__globals__");
        const py::str qualname(m_qualname);
        auto copy = py::reinterpret_steal<py::object>(
            PyFunction_NewWithQualName(m_code.ptr(), globals.ptr(), qualname.ptr()));
        if (!copy) throw py::error_already_set();
        return copy;
    }

    /// The original's closure, its __class__ cell replaced by one holding the class; checked against
    /// the new code's free variables, as types.FunctionType checks and PyFunction_SetClosure does not.
    void set_closure(const py::object& copy) const {
        const py::object closure = m_function.attr("__closure__");
        const py::tuple freevars = m_code.attr("co_freevars");
        if (closure.is_none()) {
            if (!freevars.empty())
                throw py::value_error(std::format("{} needs a closure of {} cells and has none",
                                                  text(m_function.attr("__qualname__")), freevars.size()));
            return;
        }
        const py::tuple cells = closure;
        if (cells.size() != freevars.size())
            throw py::value_error(std::format("{} has {} closure cells for {} free variables",
                                              text(m_function.attr("__qualname__")), cells.size(), freevars.size()));
        py::tuple rebound(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i)
            rebound[i] = m_class && text(freevars[i]) == "__class__" ? class_cell() : py::object(cells[i]);
        if (PyFunction_SetClosure(copy.ptr(), rebound.ptr()) != 0) throw py::error_already_set();
    }

    [[nodiscard]] py::object class_cell() const {
        PyObject* cell = PyCell_New(m_class.ptr());
        if (cell == nullptr) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(cell);
    }

    /// Everything else the original carries, as it is.
    void carry_attributes(const py::object& copy) const {
        for (const char* attribute : {"__name__", "__defaults__", "__kwdefaults__", "__doc__", "__module__"})
            copy.attr(attribute) = m_function.attr(attribute);
        copy.attr("__dict__").attr("update")(m_function.attr("__dict__"));
        if (py::hasattr(m_function, "__annotate__"))  // Python 3.14: annotations are evaluated lazily
            copy.attr("__annotate__") = m_function.attr("__annotate__");
        else
            copy.attr("__annotations__") = py::dict(m_function.attr("__annotations__"));
        if (py::hasattr(m_function, "__type_params__"))  // Python 3.12 and later
            copy.attr("__type_params__") = m_function.attr("__type_params__");
    }

    py::object m_function;
    py::object m_code;
    std::string m_qualname;
    py::object m_class;   // none: the closure's cells are kept as they are
};

/// One trait moved into one class: the two classes, the rules that move code between them, and the
/// record the trait carries when it was itself composed into (R2). Built once, when compose() reaches
/// the trait, so each class's names are read once per trait.
/// Example: TraitMove(Greeter, Person).function(Greeter.greet) is greet rebuilt for Person — reading
/// `_Person__name` where it read `_Greeter__name`, qualified as `Person.greet`.
class TraitMove {
public:
    /// Refuses a candidate that is not a class.
    TraitMove(const py::handle& candidate, py::type target)
        : m_trait(trait_of(candidate)),
          m_target(std::move(target)),
          m_trait_names(::pygim::adapter::name_policy_of(m_trait)),
          m_name(std::format("{}.{}", text(m_trait.attr("__module__")), m_trait_names.qualname())),
          m_rules(python_names::Reowning{}.then(m_trait_names, ::pygim::adapter::name_policy_of(m_target))),
          m_carried(records().find(m_trait)) {}

    /// The trait's qualified name: "Greeter".
    [[nodiscard]] std::string_view qualname() const noexcept { return m_trait_names.qualname(); }
    /// The trait's module and qualified name, what Provenance.trait says: "tests.Greeter".
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

    /// The trait's own namespace, copied: planning runs Python code (a property subclass's
    /// constructor) that could edit it.
    [[nodiscard]] py::dict members() const {
        auto copy = py::reinterpret_steal<py::dict>(PyDict_Copy(own_namespace(m_trait).ptr()));
        if (!copy) throw py::error_already_set();
        return copy;
    }

    /// The member `value` under `key`, planned for the target; nothing for the trait class's own
    /// machinery (a dunder no kind composes). Refuses a key that is not a str and a member no kind
    /// composes. Defined after the kinds of member, which it uses.
    [[nodiscard]] std::optional<PlannedMember> planned(const py::handle& key, const py::handle& value) const;

    /// Where `name` came from: the trait it first came from, when this trait was itself composed from
    /// it and still holds `value` as compose() left it (R2, R4); otherwise this trait, where `code`
    /// defines it.
    [[nodiscard]] Provenance attribution(const std::string& name, const py::handle& value,
                                         const py::object& code) const {
        if (m_carried != nullptr && m_carried->holds(name, value)) return m_carried->at(name);
        return Provenance{m_name, where(code), {}};
    }

    /// `function`, rebuilt to belong to the target: its code by CodeBuilder with this move's rules
    /// (private and qualified names, nested code too), its qualified name moved, and its __class__ cell
    /// holding the target, so zero-argument super() resolves there.
    [[nodiscard]] py::object function(const py::handle& function) const {
        return FunctionCopy(function)
            .with_code(CodeBuilder(function.attr("__code__"), m_rules).build())
            .with_qualname(m_rules.requalified(text(function.attr("__qualname__"))))
            .with_class_cell(m_target)
            .make();
    }

    /// `value` rebuilt when it is a Python function; anything else (None, a builtin) as it is.
    [[nodiscard]] py::object function_or_same(const py::object& value) const {
        return PyFunction_Check(value.ptr()) ? function(value) : value;
    }

private:
    static py::type trait_of(const py::handle& candidate) {
        if (!PyType_Check(candidate.ptr()))
            throw py::type_error(std::format("compose takes classes as traits, not {}", type_name(candidate)));
        return py::reinterpret_borrow<py::type>(candidate);
    }

    py::type m_trait;
    py::type m_target;
    python_names::NamePolicy m_trait_names;
    std::string m_name;               // module and qualified name: "tests.Greeter"
    python_names::Reowning m_rules;   // trait -> target, for private and qualified names
    const TraitRecord* m_carried;     // the trait's own record when it was composed into; nullptr otherwise
};

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

/// A classmethod: the function inside rebuilt, so `cls` is the class it is called on.
class ClassMethodMember {
public:
    static bool is(const py::handle& value) { return PyObject_TypeCheck(value.ptr(), &PyClassMethod_Type); }
    explicit ClassMethodMember(py::object method) : m_method(std::move(method)) {}

    [[nodiscard]] py::object rewritten(const TraitMove& trait) const {
        return py::type::of(m_method)(trait.function_or_same(m_method.attr("__func__")));
    }
    [[nodiscard]] py::object code() const { return code_or_none(m_method.attr("__func__")); }

private:
    py::object m_method;
};

/// A staticmethod: the function inside rebuilt.
class StaticMethodMember {
public:
    static bool is(const py::handle& value) { return PyObject_TypeCheck(value.ptr(), &PyStaticMethod_Type); }
    explicit StaticMethodMember(py::object method) : m_method(std::move(method)) {}

    [[nodiscard]] py::object rewritten(const TraitMove& trait) const {
        return py::type::of(m_method)(trait.function_or_same(m_method.attr("__func__")));
    }
    [[nodiscard]] py::object code() const { return code_or_none(m_method.attr("__func__")); }

private:
    py::object m_method;
};

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

/// Why a member cannot join the plan without override: another trait of this call planned the same
/// name, or the target's own namespace already has it. The two conflict rules, and their messages.
/// Example: Clash::with_target("Person", greet).message() is
/// "Person already has greet; tests.Greeter would replace it (pass override=True)".
class Clash {
public:
    static Clash between_traits(const PlannedMember& earlier, const PlannedMember& later) {
        return Clash{std::format("{} and {} both define {} (pass override=True to let the later win)",
                                 earlier.trait(), later.from(), later.name())};
    }
    static Clash with_target(const std::string& target_name, const PlannedMember& member) {
        return Clash{std::format("{} already has {}; {} would replace it (pass override=True)",
                                 target_name, member.name(), member.from())};
    }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }

private:
    explicit Clash(std::string message) : m_message(std::move(message)) {}
    std::string m_message;
};

/// The members one compose() call will write, in the order they will be written, and the rule for
/// joining it: a member that clashes is refused unless override, and with override the later trait
/// wins. Every refusal happens before anything is written.
class Plan {
public:
    Plan(py::type target, bool override_existing)
        : m_target(std::move(target)),
          m_override(override_existing),
          m_own(own_namespace(m_target)),
          m_target_name(text(m_target.attr("__qualname__"))) {}

    void admit(PlannedMember member) {
        if (const auto clash = clash_of(member); clash && !m_override)
            throw py::value_error(clash->message());
        replace_or_append(std::move(member));
    }

    [[nodiscard]] bool empty() const noexcept { return m_members.empty(); }

    /// Write every member into the target, in order. A write that fails leaves the members before it
    /// (no rollback, decided 2026-10-18).
    void write() const {
        for (const auto& member : m_members) member.write_into(m_target);
    }

    /// Note every member in the target's record, composed at `composed`, holding what landed. The
    /// record is in the side table: no hook can reach it.
    void record(const std::string& composed) const {
        const py::dict own = own_namespace(m_target);
        auto& record = records().at(m_target);
        for (const auto& member : m_members) member.note_into(record, composed, own);
    }

private:
    [[nodiscard]] std::optional<Clash> clash_of(const PlannedMember& member) const {
        if (const PlannedMember* earlier = find(member.name())) return Clash::between_traits(*earlier, member);
        if (target_has(member)) return Clash::with_target(m_target_name, member);
        return std::nullopt;
    }

    [[nodiscard]] const PlannedMember* find(const std::string& name) const {
        const auto found = std::ranges::find(m_members, name, &PlannedMember::name);
        return found == m_members.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool target_has(const PlannedMember& member) const {
        const int has = PyDict_Contains(m_own.ptr(), member.name_object().ptr());
        if (has < 0) throw py::error_already_set();
        return has == 1;
    }

    /// The later trait wins: an earlier member of the same name leaves, and this one is written in turn.
    void replace_or_append(PlannedMember member) {
        std::erase_if(m_members, [&](const PlannedMember& planned) { return planned.name() == member.name(); });
        m_members.push_back(std::move(member));
    }

    py::type m_target;
    bool m_override;
    py::dict m_own;              // the target's own namespace: its real dict, live
    std::string m_target_name;
    std::vector<PlannedMember> m_members;
};

/// compose() as a builder over the target: add() the traits, choose override(), then compose().
/// Example: Composer(Person).add(Greeter).add(Loud).compose() is Person, with both traits' members.
class Composer {
public:
    explicit Composer(py::type target) : m_target(std::move(target)) {}

    /// A trait to compose; it is checked when compose() reaches it.
    Composer& add(const py::handle& trait) {
        m_traits.push_back(py::reinterpret_borrow<py::object>(trait));
        return *this;
    }

    /// Whether a member may replace one of the target's own, or one an earlier trait brings.
    Composer& override(bool replace = true) {
        m_override = replace;
        return *this;
    }

    /// Plan every trait — each member classified, rewritten and admitted, every refusal raised — then
    /// write the members and record them. With nothing planned the target is left exactly as it was.
    py::type compose() const {
        Plan plan(m_target, m_override);
        for (const py::object& candidate : m_traits) {
            const TraitMove trait(candidate, m_target);    // refuses a candidate that is not a class
            for (const auto& [key, value] : trait.members())
                if (auto member = trait.planned(key, value))  // nothing for the trait class's own machinery
                    plan.admit(std::move(*member));
        }
        if (plan.empty()) return m_target;
        const std::string composed = caller();
        plan.write();                                       // the members first: a write may fire a hook
        plan.record(composed);                              // then the record, out of any hook's reach
        return m_target;
    }

private:
    py::type m_target;
    std::vector<py::object> m_traits;
    bool m_override = false;
};

}  // namespace detail

/// Copy every member of every trait into `target`, rewritten to belong there, and record where each
/// came from. Refuses — writing nothing — a trait that is not a class, a member no kind composes,
/// a name `target` already has, or a name two traits define; `override=True` replaces instead (the
/// later trait winning). Returns `target`.
/// Cost: per member, one CodeBuilder build per function in it; per call, one frame lookup.
/// Example: compose(Person, Greeter) is Person, and Person("ann").greet() is 'hello ann'.
inline py::type compose(const py::type& target, const py::args& traits, bool override_existing) {
    detail::Composer composer(target);
    for (const auto& trait : traits) composer.add(trait);
    return composer.override(override_existing).compose();
}

/// The record of what was composed into `cls` itself and is still there: a member replaced or
/// deleted since is left out (D15). Empty for a class nothing was composed into.
/// Example: list(traits_of(Person)) is ['greet'].
inline py::object traits_of(const py::type& cls) {
    const TraitRecord* found = detail::records().find(cls);
    // A snapshot: the store stays private.
    return py::cast(found != nullptr ? found->standing_in(detail::own_namespace(cls)) : TraitRecord{});
}

}  // namespace pygim::traits
