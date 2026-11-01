#pragma once

// Compare a real constexpr planner call with independently supplied expectations.
// Example: a registered optional "first" dependency must be injected; if its
// lookup throws, the same exception and the trace [0] must be observed instead.
// All mutable test state belongs to this evaluation; no global trace is read.
#include "autowire_model.h"

struct LocalLookup {
    const Lookup& scenario;
    LookupTrace& trace;
    constexpr bool operator()(int key) const { return scenario.evaluate(key, trace); }
};

constexpr bool verify_autowire_case(
    const IntParams& params,
    const Lookup& scenario,
    const IntPlan& expected_plan,
    std::string_view expected_type,
    std::string_view expected_message,
    const LookupTrace& expected_trace) {
    LookupTrace trace;
    const LocalLookup lookup{scenario, trace};
    try {
        const auto plan = pygim::core::plan_autowiring(params, lookup);
        if (expected_type != "success" || !expected_message.empty() ||
            plan.size() != expected_plan.size()) return false;
        for (std::size_t i = 0; i < plan.size(); ++i)
            if (plan[i].name != expected_plan[i].name ||
                plan[i].annotation != expected_plan[i].annotation) return false;
    } catch (const std::exception& error) {
        if (expected_type == "success" || !expected_plan.empty() ||
            autowire_model::error_type(error) != expected_type ||
            std::string_view{error.what()} != expected_message) return false;
    } catch (...) {
        return false;
    }
    return trace == expected_trace;
}

// The result contract names these two fields. An added field needs a contract
// update even when an old saved model has no runtime results for the new shape.
static_assert(call_space::members<IntInjection>().size() == 2, "Update result contract");
static_assert(std::meta::identifier_of(call_space::members<IntInjection>()[0]) == "name");
static_assert(std::meta::identifier_of(call_space::members<IntInjection>()[1]) == "annotation");
