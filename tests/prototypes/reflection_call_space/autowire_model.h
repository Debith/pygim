#pragma once

// Shared model: Lookup{Available::Both, FailAt::Second} succeeds once, then
// throws. evaluate(key, trace) uses caller-owned state in either evaluation mode.
// Real IoC policy, with int dependency keys and an explicit lookup environment.
#include "wiring/ioc/core.h"
#include "call_space.h"

using IntParam = pygim::core::ParamSpec<int>;
using IntParams = std::vector<IntParam>;
using IntInjection = pygim::core::PlannedInjection<int>;
using IntPlan = std::vector<IntInjection>;
using LookupTrace = std::vector<int>;

enum class Available { Neither, Zero, One, Both };
enum class FailAt { Never, First, Second };
struct lookup_failure : std::runtime_error {
    constexpr lookup_failure() : std::runtime_error("injected lookup failure") {}
};
// Only the observer is mutable; each generated call starts with a fresh trace.
inline LookupTrace lookup_trace;
struct Lookup {
    Available available;
    FailAt fail_at;
    constexpr bool evaluate(int key, LookupTrace& trace) const {
        trace.push_back(key);
        if ((fail_at == FailAt::First && trace.size() == 1) ||
            (fail_at == FailAt::Second && trace.size() == 2))
            throw lookup_failure{};
        return available == Available::Both ||
               (available == Available::Zero && key == 0) ||
               (available == Available::One && key == 1);
    }
    bool operator()(int key) const { return evaluate(key, lookup_trace); }
};
struct autowire_model : call_space::default_model {
    static constexpr std::size_t max_length = 2;
    static void before_call() { lookup_trace.clear(); }
    static void observations(std::ostream& out) {
        out << ",\"lookup_trace\":";
        call_space::json(out, lookup_trace);
    }
    static constexpr std::string_view error_type(const std::exception& error) {
        if (dynamic_cast<const lookup_failure*>(&error)) return "lookup_failure";
        if (dynamic_cast<const pygim::core::missing_key*>(&error)) return "missing_key";
        return default_model::error_type(error);
    }
};
namespace call_space {
template <> inline constexpr bool reflected_record<IntParam> = true;
template <> inline constexpr bool reflected_record<IntInjection> = true;
template <> inline constexpr bool reflected_record<Lookup> = true;
template <> struct domain<int, autowire_model> {
    static constexpr std::string_view kind = "integer";
    static constexpr std::string_view scope = "two distinct dependency identities";
    static constexpr bool exhaustive_type = false;
    static constexpr std::vector<int> values() { return {0, 1}; }
};
template <> struct domain<std::string, autowire_model> {
    static constexpr std::string_view kind = "string";
    static constexpr std::string_view scope = "two distinct parameter names";
    static constexpr bool exhaustive_type = false;
    static constexpr std::vector<std::string> values() { return {"first", "second"}; }
};
}

consteval auto autowire_target() {
    return ^^pygim::core::plan_autowiring<int, const Lookup&>;
}
