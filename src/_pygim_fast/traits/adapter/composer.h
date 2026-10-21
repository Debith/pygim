// traits/adapter/composer.h — Composer: compose() as a builder over the target.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "introspection.h"
#include "member.h"
#include "plan.h"
#include "trait_move.h"

namespace pygim::traits {

namespace py = pybind11;

namespace detail {

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

}  // namespace pygim::traits
