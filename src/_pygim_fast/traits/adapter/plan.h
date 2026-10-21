// traits/adapter/plan.h — the members one compose() call will write, and the rule for joining:
// PlannedMember, Clash, Plan.

#pragma once

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "introspection.h"
#include "record.h"

namespace pygim::traits {

namespace py = pybind11;

namespace detail {

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

}  // namespace detail

}  // namespace pygim::traits
