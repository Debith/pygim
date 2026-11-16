// traits/adapter/record.h — the record behind traits_of(): Provenance, TraitRecord, and the side table
// that keeps each class's record off the class (R1).

#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>

#include "../../utils/aliases.h"
#include "../../wiring/registry/core.h"

namespace pygim::traits {

namespace py = pybind11;

/// Where a composed member came from: the trait it first came from, where that trait defined it, and
/// the compose() call that put it in the class. compose() knows the first two when it plans the member
/// and the call's place only when it notes the record, so a Provenance is made in two steps, each
/// named here: defined_by(), then composed_at().
/// Example, for Greeter.greet composed into Person:
/// Provenance::defined_by("tests.Greeter", "/x/tests.py:12").composed_at("/x/tests.py:40").
class Provenance {
public:
    /// A member `trait` (its module and qualified name) defines at `defined` ("file:line"; "" for code
    /// that is not Python's), not composed yet.
    static Provenance defined_by(std::string trait, std::string defined) {
        return Provenance(std::move(trait), std::move(defined), {});
    }

    /// The same member, composed by the call at `composed` ("file:line"). A member carried along from a
    /// class composed earlier keeps its trait and definition; only this changes (R2).
    [[nodiscard]] Provenance composed_at(std::string composed) const {
        return Provenance(m_trait, m_defined, std::move(composed));
    }

    [[nodiscard]] const std::string& trait() const noexcept { return m_trait; }
    [[nodiscard]] const std::string& defined() const noexcept { return m_defined; }
    [[nodiscard]] const std::string& composed() const noexcept { return m_composed; }

    /// What Python's repr() shows: "Provenance(trait='tests.Greeter', defined='...', composed='...')".
    [[nodiscard]] std::string repr() const {
        const auto quoted = [](const std::string& text) { return py::repr(py::str(text)).cast<std::string>(); };
        return std::format("Provenance(trait={}, defined={}, composed={})", quoted(m_trait), quoted(m_defined),
                           quoted(m_composed));
    }

private:
    Provenance(std::string trait, std::string defined, std::string composed)
        : m_trait(std::move(trait)), m_defined(std::move(defined)), m_composed(std::move(composed)) {}

    std::string m_trait;
    std::string m_defined;
    std::string m_composed;
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

}  // namespace detail

}  // namespace pygim::traits
