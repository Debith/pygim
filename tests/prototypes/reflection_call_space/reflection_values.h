#pragma once

// Finite value models shared by reflection probes. Opting ParamSpec<int> into
// reflected_record discovers its fields; enum fields discover their own values.
// Scalar corpora and sequence bounds are supplied explicitly by a model.
#include <meta>
#include <concepts>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace call_space {
template <class T> inline constexpr bool reflected_record = false;
struct default_model {
    static constexpr std::size_t max_length = 3;
    static void before_call() {}
    static void observations(std::ostream&) {}
    static constexpr std::string_view error_type(const std::exception& error) {
        if (dynamic_cast<const std::invalid_argument*>(&error)) return "invalid_argument";
        if (dynamic_cast<const std::out_of_range*>(&error)) return "out_of_range";
        if (dynamic_cast<const std::runtime_error*>(&error)) return "runtime_error";
        return "std_exception";
    }
};
template <class T> consteval auto members() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unprivileged());
}
template <class T, std::size_t I>
using member_type = std::remove_cvref_t<typename [:std::meta::type_of(members<T>()[I]):]>;
template <class T> struct sequence_traits { static constexpr bool enabled = false; };
template <class T> struct sequence_traits<std::vector<T>> {
    static constexpr bool enabled = true;
    using element = T;
};
template <class T> struct optional_traits { static constexpr bool enabled = false; };
template <class T> struct optional_traits<std::optional<T>> {
    static constexpr bool enabled = true;
    using element = T;
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
template <class T> void json(std::ostream& out, const T& value);
template <class T, std::size_t... I>
void json_members(std::ostream& out, const T& value, std::index_sequence<I...>) {
    ((out << (I ? "," : ""), json(out, std::meta::identifier_of(members<T>()[I])),
      out << ':', json(out, value.[:members<T>()[I]:])), ...);
}
template <class T, std::size_t... I>
std::string_view enum_name(T value, std::index_sequence<I...>) {
    std::string_view name;
    ((value == std::meta::extract<T>(std::meta::enumerators_of(^^T)[I])
        ? (name = std::meta::identifier_of(std::meta::enumerators_of(^^T)[I]), void()) : void()), ...);
    if (name.empty()) throw std::invalid_argument("Enum value has no reflected enumerator");
    return name;
}
template <class T> void json(std::ostream& out, const T& value) {
    if constexpr (std::is_convertible_v<T, std::string_view>) json(out, std::string_view{value});
    else if constexpr (std::same_as<T, bool>) out << (value ? "true" : "false");
    else if constexpr (std::is_integral_v<T>) out << value;
    else if constexpr (std::is_enum_v<T>)
        json(out, enum_name(value, std::make_index_sequence<std::meta::enumerators_of(^^T).size()>{}));
    else if constexpr (optional_traits<T>::enabled) {
        if (value) json(out, *value);
        else out << "null";
    } else if constexpr (sequence_traits<T>::enabled) {
        out << '[';
        bool first = true;
        for (const auto& item : value) {
            if (!std::exchange(first, false)) out << ',';
            json(out, static_cast<const typename sequence_traits<T>::element&>(item));
        }
        out << ']';
    } else if constexpr (reflected_record<T>) {
        out << '{';
        json_members(out, value, std::make_index_sequence<members<T>().size()>{});
        out << '}';
    } else static_assert(sizeof(T) == 0, "Unmodeled JSON value");
}

template <class T, class Model = default_model> struct domain;
template <class Model> struct domain<bool, Model> {
    static constexpr std::string_view kind = "boolean";
    static constexpr std::string_view scope = "both Boolean values";
    static constexpr bool exhaustive_type = true;
    static constexpr std::vector<bool> values() { return {false, true}; }
};
template <class Model> struct domain<std::string, Model> {
    static constexpr std::string_view kind = "string";
    static constexpr std::string_view scope = "five declared string atoms";
    static constexpr bool exhaustive_type = false;
    static constexpr std::vector<std::string> values() { return {"", "b", "a", ", ", "\xc3\xa9"}; }
};
template <class T, class Model> concept modeled = requires {
    { domain<T, Model>::values() } -> std::same_as<std::vector<T>>;
    domain<T, Model>::kind;
    domain<T, Model>::scope;
    domain<T, Model>::exhaustive_type;
};
template <class T, class Model> requires modeled<T, Model>
struct domain<std::vector<T>, Model> {
    static constexpr std::string_view kind = "sequence";
    static constexpr std::string_view scope = "bounded lengths over the element domain";
    static constexpr bool exhaustive_type = false;
    static constexpr std::size_t max_length = Model::max_length;
    static constexpr std::vector<std::vector<T>> values() {
        std::vector<std::vector<T>> result{{}}, layer{{}};
        const auto atoms = domain<T, Model>::values();
        for (std::size_t length = 1; length <= max_length; ++length) {
            std::vector<std::vector<T>> next;
            for (const auto& prefix : layer) for (const auto& atom : atoms) {
                auto value = prefix;
                value.push_back(atom);
                next.push_back(std::move(value));
            }
            result.insert(result.end(), next.begin(), next.end());
            layer = std::move(next);
        }
        return result;
    }
};
template <class T, class Model> requires std::is_enum_v<T>
struct domain<T, Model> {
    static constexpr std::string_view kind = "enum";
    static constexpr std::string_view scope = "every declared enumerator; unnamed underlying values excluded";
    static constexpr bool exhaustive_type = false;
    template <std::size_t... I> static constexpr std::vector<T> make(std::index_sequence<I...>) {
        return {std::meta::extract<T>(std::meta::enumerators_of(^^T)[I])...};
    }
    static constexpr std::vector<T> values() {
        return make(std::make_index_sequence<std::meta::enumerators_of(^^T).size()>{});
    }
};
template <class T, class Model> requires modeled<T, Model>
struct domain<std::optional<T>, Model> {
    static constexpr std::string_view kind = "optional";
    static constexpr std::string_view scope = "absent or each modeled element";
    static constexpr bool exhaustive_type = domain<T, Model>::exhaustive_type;
    static constexpr std::vector<std::optional<T>> values() {
        std::vector<std::optional<T>> result{std::nullopt};
        for (const auto& value : domain<T, Model>::values()) result.emplace_back(value);
        return result;
    }
};
template <class T, class Model> requires reflected_record<T>
struct domain<T, Model> {
    static constexpr std::string_view kind = "record";
    static constexpr std::string_view scope = "product of reflected public field domains";
    static constexpr bool exhaustive_type = false;
    template <std::size_t I = 0, class... Args>
    static constexpr void expand(std::vector<T>& result, const Args&... args) {
        if constexpr (I == members<T>().size()) result.push_back(T{args...});
        else {
            using Field = member_type<T, I>;
            static_assert(modeled<Field, Model>, "Unmodeled reflected field: supply an input domain");
            if constexpr (modeled<Field, Model>)
                for (const auto& value : domain<Field, Model>::values())
                    expand<I + 1>(result, args..., static_cast<const Field&>(value));
        }
    }
    static constexpr std::vector<T> values() {
        static_assert(std::is_aggregate_v<T>, "Reflected domain requires aggregate initialization");
        std::vector<T> result;
        expand(result);
        return result;
    }
};
template <class T, class Model> void emit_domain(std::ostream& out);
template <class T, class Model, std::size_t... I>
void emit_fields(std::ostream& out, std::index_sequence<I...>) {
    ((out << (I ? "," : "") << "{\"name\":",
      json(out, std::meta::identifier_of(members<T>()[I])),
      out << ",\"cpp_type\":", json(out, std::meta::display_string_of(std::meta::type_of(members<T>()[I]))),
      out << ",\"domain\":", emit_domain<member_type<T, I>, Model>(out), out << '}'), ...);
}
template <class T, class Model> void emit_domain(std::ostream& out) {
    using D = domain<T, Model>;
    out << "{\"kind\":"; json(out, D::kind);
    out << ",\"scope\":"; json(out, D::scope);
    out << ",\"exhaustive_type\":"; json(out, D::exhaustive_type);
    out << ",\"values\":"; json(out, D::values());
    if constexpr (reflected_record<T>) {
        out << ",\"fields\":[";
        emit_fields<T, Model>(out, std::make_index_sequence<members<T>().size()>{});
        out << ']';
    } else if constexpr (sequence_traits<T>::enabled) {
        out << ",\"max_length\":" << D::max_length << ",\"element\":";
        emit_domain<typename sequence_traits<T>::element, Model>(out);
    } else if constexpr (optional_traits<T>::enabled) {
        out << ",\"element\":";
        emit_domain<typename optional_traits<T>::element, Model>(out);
    }
    out << '}';
}
} // namespace call_space
