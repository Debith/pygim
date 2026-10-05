#pragma once
// pathlike/adapter/engines/toml.h — the toml++ engine, both directions.
//
// One file IS one engine: the descriptor at the bottom is what the build
// discovers into the registry (see ../../registry.h). The implementation stays
// in namespace detail: inside `struct engines::toml` the injected class name
// would hide the toml++ namespace, so the descriptor only forwards. Dates and times
// materialise as datetime.date / time / datetime — matching what the stdlib's
// tomllib produces, so the two are drop-in comparable — and convert back on
// write. TOML's own constraints are enforced loudly: documents are tables
// (mapping root), there is no null, integers are int64.

#include <sstream>
#include <array>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#define TOML_EXCEPTIONS 0   // error-code API (toml::parse_result), no throw across nogil
#include "../third_party/tomlplusplus/toml.hpp"
#include "../../core.h"
#include "../common.h"
#include "../materialize.h"

namespace pygim::pathlike::detail {

// ── Read side ──────────────────────────────────────────────────────────────

[[nodiscard]] inline py::object toml_date_to_py(const toml::date& d) {
    static py::object cls = py::module_::import("datetime").attr("date");
    return cls(d.year, d.month, d.day);
}

[[nodiscard]] inline py::object toml_time_to_py(const toml::time& t) {
    static py::object cls = py::module_::import("datetime").attr("time");
    return cls(t.hour, t.minute, t.second, t.nanosecond / 1000);
}

[[nodiscard]] inline py::object toml_datetime_to_py(const toml::date_time& dt) {
    static py::object dt_cls = py::module_::import("datetime").attr("datetime");
    static py::object tz_cls = py::module_::import("datetime").attr("timezone");
    static py::object td_cls = py::module_::import("datetime").attr("timedelta");
    py::object tz = py::none();
    if (dt.offset) {
        tz = tz_cls(td_cls(py::arg("minutes") = dt.offset->minutes));
    }
    return dt_cls(dt.date.year, dt.date.month, dt.date.day, dt.time.hour, dt.time.minute,
                  dt.time.second, dt.time.nanosecond / 1000, tz);
}

[[nodiscard]] inline py::object toml_to_py(const toml::node& n, KeyCache& keys) {
    if (const toml::table* t = n.as_table()) {
        py::dict out;
        for (const auto& [key, value] : *t) out[keys.get(key.str())] = toml_to_py(value, keys);
        return out;
    }
    if (const toml::array* a = n.as_array()) {
        py::list out;
        for (const toml::node& child : *a) out.append(toml_to_py(child, keys));
        return out;
    }
    if (const auto* v = n.as_string())         return py::str(v->get());
    if (const auto* v = n.as_integer())        return py::int_(v->get());
    if (const auto* v = n.as_floating_point()) return py::float_(v->get());
    if (const auto* v = n.as_boolean())        return py::bool_(v->get());
    if (const auto* v = n.as_date())           return toml_date_to_py(v->get());
    if (const auto* v = n.as_time())           return toml_time_to_py(v->get());
    if (const auto* v = n.as_date_time())      return toml_datetime_to_py(v->get());
    throw std::runtime_error("toml: unhandled node type");
}

// Text -> table, with no Python involved (callers release the GIL). A parse
// error names `origin` (the file, or what the text is) and the line.
[[nodiscard]] inline toml::parse_result parse_toml(std::string_view text, std::string_view origin) {
    return toml::parse(text, origin);
}

[[nodiscard]] inline py::object table_or_throw(const toml::parse_result& result, std::string_view origin, KeyCache& keys) {
    if (!result) {
        const auto& err = result.error();
        throw std::runtime_error("TOML parse error (" + std::string(origin) + ", line " +
                                 std::to_string(err.source().begin.line) +
                                 "): " + std::string(err.description()));
    }
    return toml_to_py(result.table(), keys);
}

[[nodiscard]] inline py::object load_toml(const file& f, KeyCache& keys) {
    toml::parse_result result = [&f] {
        py::gil_scoped_release nogil;
        const std::string bytes = f.read_bytes();
        return parse_toml(bytes, f.fspath());
    }();
    return table_or_throw(result, f.fspath(), keys);
}

// The same from text in memory (front matter inside a markdown file).
[[nodiscard]] inline py::object loads_toml(std::string_view text, std::string_view origin, KeyCache& keys) {
    toml::parse_result result = [&] {
        py::gil_scoped_release nogil;
        return parse_toml(text, origin);
    }();
    return table_or_throw(result, origin, keys);
}

// ── Write side ─────────────────────────────────────────────────────────────
// Python object -> toml++ node, inserted via `ins` (a lambda targeting either
// a table slot or an array slot — one conversion, both containers). Container
// recursion goes through the non-template table/array builders, so the
// template is instantiated exactly twice regardless of nesting depth.

[[nodiscard]] inline toml::table py_to_toml_table(py::handle obj);
[[nodiscard]] inline toml::array py_to_toml_array(py::handle obj);

template <typename Insert>
inline void py_to_toml_value(py::handle obj, Insert&& ins) {
    static py::object date_cls = py::module_::import("datetime").attr("date");
    static py::object time_cls = py::module_::import("datetime").attr("time");
    static py::object datetime_cls = py::module_::import("datetime").attr("datetime");

    if (obj.is_none()) {
        throw std::invalid_argument("toml cannot represent None (TOML has no null)");
    }
    if (py::isinstance<py::bool_>(obj)) return ins(obj.cast<bool>());   // before int
    if (py::isinstance<py::int_>(obj)) {
        try {
            return ins(obj.cast<int64_t>());
        } catch (const py::cast_error&) {
            throw std::invalid_argument("toml integers are 64-bit; value out of range: " +
                                        py::str(obj).cast<std::string>());
        }
    }
    if (py::isinstance<py::float_>(obj)) return ins(obj.cast<double>());
    if (py::isinstance<py::str>(obj)) return ins(obj.cast<std::string>());
    if (py::isinstance(obj, datetime_cls)) {        // before date: datetime IS a date
        toml::date_time dt;
        dt.date = {obj.attr("year").cast<uint16_t>(), obj.attr("month").cast<uint8_t>(),
                   obj.attr("day").cast<uint8_t>()};
        dt.time = {obj.attr("hour").cast<uint8_t>(), obj.attr("minute").cast<uint8_t>(),
                   obj.attr("second").cast<uint8_t>(),
                   obj.attr("microsecond").cast<uint32_t>() * 1000u};
        py::object off = obj.attr("utcoffset")();
        if (!off.is_none()) {
            dt.offset = toml::time_offset(0, static_cast<int16_t>(
                off.attr("total_seconds")().cast<double>() / 60.0));
        }
        return ins(dt);
    }
    if (py::isinstance(obj, date_cls)) {
        return ins(toml::date{obj.attr("year").cast<uint16_t>(),
                              obj.attr("month").cast<uint8_t>(),
                              obj.attr("day").cast<uint8_t>()});
    }
    if (py::isinstance(obj, time_cls)) {
        return ins(toml::time{obj.attr("hour").cast<uint8_t>(),
                              obj.attr("minute").cast<uint8_t>(),
                              obj.attr("second").cast<uint8_t>(),
                              obj.attr("microsecond").cast<uint32_t>() * 1000u});
    }
    if (py::isinstance<py::dict>(obj)) return ins(py_to_toml_table(obj));
    if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
        return ins(py_to_toml_array(obj));
    }
    throw py::type_error("toml write: cannot write a value of type " +
                         py::str(py::type::of(obj).attr("__name__")).cast<std::string>());
}

[[nodiscard]] inline toml::array py_to_toml_array(py::handle obj) {
    toml::array out;
    for (auto item : obj.cast<py::sequence>()) {
        py_to_toml_value(item, [&out](auto&& v) { out.push_back(std::forward<decltype(v)>(v)); });
    }
    return out;
}

[[nodiscard]] inline toml::table py_to_toml_table(py::handle obj) {
    toml::table out;
    for (auto item : obj.cast<py::dict>()) {
        if (!py::isinstance<py::str>(item.first)) {
            throw py::type_error("toml write: mapping keys must be str, got " +
                                 py::str(py::type::of(item.first).attr("__name__")).cast<std::string>());
        }
        const std::string key = item.first.cast<std::string>();
        py_to_toml_value(item.second, [&out, &key](auto&& v) {
            out.insert_or_assign(key, std::forward<decltype(v)>(v));
        });
    }
    return out;
}

[[nodiscard]] inline std::string dumps_toml(py::handle obj,
                                           toml::format_flags flags = toml::toml_formatter::default_flags) {
    if (!py::isinstance<py::dict>(obj)) {
        throw py::type_error("toml write: content must be a mapping (TOML documents are tables), got " +
                             py::str(py::type::of(obj).attr("__name__")).cast<std::string>());
    }
    toml::table root = py_to_toml_table(obj);
    py::gil_scoped_release nogil;
    std::stringstream ss;
    ss << toml::toml_formatter(root, flags) << '\n';
    return ss.str();
}

inline void write_toml(const file& f, py::handle obj) {
    const std::string text = dumps_toml(obj);
    py::gil_scoped_release nogil;
    write_text_file(f, text);
}

}  // namespace pygim::pathlike::detail

// ── Registry entry ─────────────────────────────────────────────────────────
namespace pygim::pathlike::engines {

struct toml {
    static constexpr std::array<std::string_view, 1> exts{".toml"};
    static constexpr std::array<std::string_view, 1> aliases{"tomlplusplus"};
    static constexpr engine_info info{
        .name = "toml",
        .label = "toml++",
        .doc = "TOML via toml++: documents are tables (mapping root), there is no null, "
               "and dates and times are datetime objects (tomllib parity).",
        .exts = exts,
        .aliases = aliases,
    };

    static py::object load(const file& f, detail::KeyCache& keys) { return detail::load_toml(f, keys); }
    static void write(const file& f, py::handle obj) { detail::write_toml(f, obj); }
    // The text half (adapter.h TextEngine): what markdown front matter parses and writes through.
    static py::object loads(std::string_view text, std::string_view origin, detail::KeyCache& keys) {
        return detail::loads_toml(text, origin, keys);
    }
    static std::string dumps(py::handle obj) { return detail::dumps_toml(obj); }
    /// Text to embed in another document (markdown front matter): strings stay
    /// on one line, escaped, so no line of a value can read as a `+++` fence.
    static std::string dumps_embedded(py::handle obj) {
        return detail::dumps_toml(obj, ::toml::toml_formatter::default_flags & ~::toml::format_flags::allow_multi_line_strings);
    }
};

}  // namespace pygim::pathlike::engines
