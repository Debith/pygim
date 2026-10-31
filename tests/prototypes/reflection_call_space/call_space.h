#pragma once

// Reflect a declaration, enumerate its finite domains, call the actual function.
// Example: emit<^^to_csv>(out) preserves the production sorted=false default.
#include "reflection_values.h"
#include <chrono>
#include <tuple>

namespace call_space {

template <std::meta::info F, std::size_t I>
using parameter_type = std::remove_cvref_t<typename [:std::meta::type_of(std::meta::parameters_of(F)[I]):]>;

template <std::meta::info F, std::size_t I>
consteval bool remaining_defaulted() {
    const auto params = std::meta::parameters_of(F);
    for (std::size_t i = I; i < params.size(); ++i)
        if (!std::meta::has_default_argument(params[i])) return false;
    return true;
}

template <std::meta::info F, class Model, std::size_t I>
void emit_parameter(std::ostream& out) {
    using T = parameter_type<F, I>;
    static_assert(modeled<T, Model>, "Unmodeled reflected parameter: supply an input domain before generating coverage");
    if constexpr (modeled<T, Model>) {
        constexpr auto p = std::meta::parameters_of(F)[I];
        out << "{\"name\":";
        json(out, std::meta::identifier_of(p));
        out << ",\"cpp_type\":";
        json(out, std::meta::display_string_of(std::meta::type_of(p)));
        out << ",\"has_default\":";
        json(out, std::meta::has_default_argument(p));
        out << ",\"domain\":";
        emit_domain<T, Model>(out);
        out << '}';
    }
}

template <std::meta::info F, class Model, std::size_t... I>
void emit_parameters(std::ostream& out, std::index_sequence<I...>) {
    ((out << (I ? "," : ""), emit_parameter<F, Model, I>(out)), ...);
}

template <std::meta::info F, class Tuple, std::size_t... I>
void emit_arguments(std::ostream& out, const Tuple& args, std::index_sequence<I...>) {
    ((out << (I ? "," : ""),
      json(out, std::meta::identifier_of(std::meta::parameters_of(F)[I])),
      out << ':', json(out, std::get<I>(args))), ...);
}

template <std::meta::info F, std::size_t Begin, std::size_t... I>
void emit_omitted(std::ostream& out, std::index_sequence<I...>) {
    ((out << (I ? "," : ""),
      json(out, std::meta::identifier_of(std::meta::parameters_of(F)[Begin + I]))), ...);
}

template <std::meta::info F, class Model, class... Args>
void emit_case(std::ostream& out, std::size_t& count, const std::tuple<Args...>& args) {
    constexpr auto total = std::meta::parameters_of(F).size();
    if (count) out << ',';
    out << "{\"id\":\"case-" << ++count << "\",\"arguments\":{";
    emit_arguments<F>(out, args, std::index_sequence_for<Args...>{});
    out << "},\"omitted\":[";
    emit_omitted<F, sizeof...(Args)>(out, std::make_index_sequence<total - sizeof...(Args)>{});
    out << ']';
    Model::before_call();
    try {
        // Splice the declaration, not a function pointer: omitted arguments
        // must be supplied by the compiler using the production default.
        auto result = std::apply([](const auto&... values) { return [:F:](values...); }, args);
        out << ",\"native_result\":";
        json(out, result);
    } catch (const std::exception& error) {
        out << ",\"native_error\":";
        json(out, std::string_view{error.what()});
        out << ",\"native_error_type\":";
        json(out, Model::error_type(error));
    }
    Model::observations(out);
    out << '}';
}

template <std::meta::info F, class Model, std::size_t I = 0, class... Args>
void enumerate(std::ostream& out, std::size_t& count, const std::tuple<Args...>& args) {
    constexpr auto total = std::meta::parameters_of(F).size();
    if constexpr (I == total) {
        emit_case<F, Model>(out, count, args);
    } else {
        if constexpr (remaining_defaulted<F, I>()) emit_case<F, Model>(out, count, args);
        using T = parameter_type<F, I>;
        static_assert(modeled<T, Model>, "Unmodeled reflected parameter: supply an input domain before generating coverage");
        if constexpr (modeled<T, Model>) {
            for (const auto& value : domain<T, Model>::values())
                enumerate<F, Model, I + 1>(out, count, std::tuple_cat(args, std::tuple<T>{value}));
        }
    }
}

template <std::meta::info F, class Model = default_model>
void emit(std::ostream& out) {
    const auto started = std::chrono::steady_clock::now();
    out << "{\"schema_version\":2,\"compiler\":";
    json(out, std::string_view{__VERSION__});
    out << ",\"function\":{\"name\":";
    if constexpr (std::meta::has_identifier(F)) json(out, std::meta::identifier_of(F));
    else json(out, std::meta::display_string_of(F));
    out << ",\"return_type\":";
    json(out, std::meta::display_string_of(std::meta::return_type_of(F)));
    out << ",\"parameters\":[";
    emit_parameters<F, Model>(out, std::make_index_sequence<std::meta::parameters_of(F).size()>{});
    out << "]},\"cases\":[";
    std::size_t count = 0;
    enumerate<F, Model>(out, count, std::tuple<>{});
    out << "],\"case_count\":" << count << ",\"native_seconds\":"
        << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
        << "}\n";
}

} // namespace call_space
