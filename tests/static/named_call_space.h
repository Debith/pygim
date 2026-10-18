// tests/static/named_call_space.h — GCC 16 reflection (-std=c++26 -freflection): every function in a
// namespace, its parameters by name, and the full product of their domains, called on the real
// declarations and written out as JSON for an independent checker.
//
// It follows tests/prototypes/reflection_call_space/call_space.h (branch codex/autowire-concrete-
// behaviors): the same idea — reflect the declaration, enumerate finite domains, call the function
// itself, emit cases with their arguments and native results — and the same refusal to compile a
// parameter no domain models. One difference, on purpose: there a domain is chosen by the parameter's
// TYPE; here every parameter is a std::string_view, so a domain is chosen by the parameter's ROLE, the
// name the declaration gives it (`class_name`, `name`, ...). The domains themselves are not written
// here: a model generates them (Domains, below) and the checker compares what was called with the
// model's declaration.
//
//   Domains::roles        role -> its values, e.g. {"name", names}
//   Domains::functions    the functions the model covers
//
// Refused at compile time, with the name in the message: a function in the namespace the model does
// not cover, a parameter whose role has no domain, a parameter that is not a std::string_view, and a
// result that is not bool, text or optional text — so a declaration that changes fails the build of
// the probe instead of silently changing what is covered.

#pragma once

#include <meta>

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace named_call_space {

struct role_domain {
    std::string_view role;
    std::span<const std::string_view> values;
};

inline void json(std::ostream& out, std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
}

inline void json(std::ostream& out, const std::string& text) { json(out, std::string_view{text}); }

inline void json(std::ostream& out, bool value) { out << (value ? "true" : "false"); }

inline void json(std::ostream& out, const std::optional<std::string>& value) {
    if (value) json(out, std::string_view{*value});
    else out << "null";
}

template <class Domains>
consteval std::span<const std::string_view> domain_of(std::string_view role) {
    for (const auto& entry : Domains::roles)
        if (entry.role == role) return entry.values;
    return {};
}

template <class Domains>
consteval bool covered(std::string_view function) {
    for (const auto name : Domains::functions)
        if (name == function) return true;
    return false;
}

template <class R>
consteval std::string_view result_kind() {
    if constexpr (std::is_same_v<R, bool>) return "bool";
    else if constexpr (std::is_same_v<R, std::optional<std::string>>) return "optional text";
    else if constexpr (std::is_convertible_v<R, std::string_view>) return "text";
    else return {};
}

template <std::meta::info F>
consteval auto parameters() {
    return std::define_static_array(std::meta::parameters_of(F));
}

template <std::meta::info F, class Domains>
consteval bool modeled() {
    constexpr auto name = std::meta::identifier_of(F);
    static_assert(covered<Domains>(name), std::string("Unmodeled reflected function: ") + std::string(name));
    template for (constexpr auto p : parameters<F>()) {
        static_assert(std::meta::dealias(std::meta::remove_cvref(std::meta::type_of(p))) == ^^std::string_view,
                      std::string("Unmodeled reflected parameter type: ") + std::string(name) + "(" +
                          std::string(std::meta::identifier_of(p)) + ")");
        static_assert(!domain_of<Domains>(std::meta::identifier_of(p)).empty(),
                      std::string("Unmodeled reflected parameter role: ") + std::string(name) + "(" +
                          std::string(std::meta::identifier_of(p)) + ")");
    }
    using R = typename [:std::meta::return_type_of(F):];
    static_assert(!result_kind<std::remove_cvref_t<R>>().empty(),
                  std::string("Unmodeled reflected result: ") + std::string(name));
    return true;
}

template <std::meta::info F, std::size_t... I>
auto call(const std::array<std::string_view, sizeof...(I)>& arguments, std::index_sequence<I...>) {
    // Splice the declaration itself: the call is the production function, not a copy of its signature.
    return [:F:](arguments[I]...);
}

template <std::meta::info F, class Domains, std::size_t I, std::size_t N>
void enumerate(std::ostream& out, std::size_t& count, std::array<std::string_view, N>& arguments) {
    if constexpr (I == N) {
        out << (count++ ? "," : "") << "{\"arguments\":{";
        std::size_t k = 0;
        template for (constexpr auto p : parameters<F>()) {
            out << (k ? "," : "");
            json(out, std::meta::identifier_of(p));
            out << ':';
            json(out, arguments[k++]);
        }
        out << "},\"native_result\":";
        json(out, call<F>(arguments, std::make_index_sequence<N>{}));
        out << '}';
    } else {
        constexpr auto values = domain_of<Domains>(std::meta::identifier_of(parameters<F>()[I]));
        for (const auto value : values) {
            arguments[I] = value;
            enumerate<F, Domains, I + 1, N>(out, count, arguments);
        }
    }
}

template <std::meta::info F, class Domains>
void emit_function(std::ostream& out) {
    static_assert(modeled<F, Domains>());
    constexpr auto N = parameters<F>().size();
    using R = std::remove_cvref_t<typename [:std::meta::return_type_of(F):]>;
    out << "{\"name\":";
    json(out, std::meta::identifier_of(F));
    out << ",\"result_kind\":";
    json(out, result_kind<R>());
    out << ",\"parameters\":[";
    std::size_t k = 0;
    template for (constexpr auto p : parameters<F>()) {
        out << (k++ ? "," : "") << "{\"name\":";
        json(out, std::meta::identifier_of(p));
        out << ",\"cpp_type\":";
        json(out, std::meta::display_string_of(std::meta::type_of(p)));
        out << ",\"domain\":[";
        std::size_t v = 0;
        for (const auto value : domain_of<Domains>(std::meta::identifier_of(p))) {
            out << (v++ ? "," : "");
            json(out, value);
        }
        out << "]}";
    }
    out << "],\"cases\":[";
    std::size_t count = 0;
    std::array<std::string_view, N> arguments{};
    enumerate<F, Domains, 0, N>(out, count, arguments);
    out << "],\"case_count\":" << count << '}';
}

/// Every function of namespace `Ns`, as one JSON document on `out`.
template <std::meta::info Ns, class Domains>
void emit(std::ostream& out) {
    out << "{\"schema_version\":1,\"compiler\":";
    json(out, std::string_view{__VERSION__});
    out << ",\"namespace\":";
    json(out, std::meta::display_string_of(Ns));
    out << ",\"functions\":[";
    std::size_t k = 0;
    template for (constexpr auto m :
                  std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()))) {
        if constexpr (std::meta::is_function(m)) {
            out << (k++ ? "," : "");
            emit_function<m, Domains>(out);
        }
    }
    out << "]}\n";
}

}  // namespace named_call_space
