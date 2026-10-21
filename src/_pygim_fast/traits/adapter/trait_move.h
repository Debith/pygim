// traits/adapter/trait_move.h — TraitMove: one trait moved into one class — the classes' names, the
// rules that move code between them, and the record the trait carries (R2).

#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <pybind11/pybind11.h>

#include "../../utils/adapter_utils.h"
#include "../../utils/python_names.h"
#include "code_builder.h"
#include "function_copy.h"
#include "introspection.h"
#include "plan.h"
#include "record.h"

namespace pygim::traits {

namespace py = pybind11;

namespace detail {

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

}  // namespace detail

}  // namespace pygim::traits
