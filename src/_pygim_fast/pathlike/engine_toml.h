#pragma once
// pathlike/engine_toml.h — the toml++ engine, both directions. Dates and times
// materialise as datetime.date / time / datetime — matching what the stdlib's
// tomllib produces, so the two are drop-in comparable — and convert back on
// write. TOML's own constraints are enforced loudly: documents are tables
// (mapping root), there is no null, integers are int64.

#include <sstream>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#define TOML_EXCEPTIONS 0   // error-code API (toml::parse_result), no throw across nogil
#include "third_party/tomlplusplus/toml.hpp"
#include "common.h"
#include "core.h"
#include "scalars.h"

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

[[nodiscard]] inline py::object load_toml(const file& f, KeyCache& keys) {
    toml::parse_result result = [&f] {
        py::gil_scoped_release nogil;
        const std::string bytes = f.read_bytes();
        return toml::parse(std::string_view(bytes), std::string_view(f.fspath()));
    }();
    if (!result) {
        const auto& err = result.error();
        throw std::runtime_error("TOML parse error (" + f.fspath() + ", line " +
                                 std::to_string(err.source().begin.line) +
                                 "): " + std::string(err.description()));
    }
    return toml_to_py(result.table(), keys);
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
    throw std::invalid_argument("toml write: unsupported type " +
                                py::str(py::type::of(obj)).cast<std::string>());
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
            throw std::invalid_argument("write: mapping keys must be str, got " +
                                        py::str(py::type::of(item.first)).cast<std::string>());
        }
        const std::string key = item.first.cast<std::string>();
        py_to_toml_value(item.second, [&out, &key](auto&& v) {
            out.insert_or_assign(key, std::forward<decltype(v)>(v));
        });
    }
    return out;
}

inline void write_toml(const file& f, py::handle obj) {
    if (!py::isinstance<py::dict>(obj)) {
        throw std::invalid_argument(
            "toml write: content must be a mapping (TOML documents are tables)");
    }
    toml::table root = py_to_toml_table(obj);
    py::gil_scoped_release nogil;
    std::stringstream ss;
    ss << root << '\n';
    write_text_file(f, ss.str());
}

}  // namespace pygim::pathlike::detail
