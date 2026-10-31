#pragma once
// pathlike/adapter/engines/toml.h — the toml++ engine, both directions.
//
// One file IS one engine: the descriptor at the bottom is what the build
// discovers into the registry (see ../../registry.h). The implementation stays
// in namespace detail: inside `struct engines::toml` the injected class name
// would hide the toml++ namespace, so the descriptor only forwards. Dates and times
// materialise as datetime.date / time / datetime — matching what the stdlib's
// tomllib produces, so the two are drop-in comparable — and convert back on
// write, both through datetime's C API, whose table on_load() takes while the
// module loads: nothing is imported while reading or writing (#248). TOML's own
// constraints are enforced loudly: documents are tables (mapping root), there is
// no null, integers are int64.

#include <sstream>
#include <array>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>
#include <datetime.h>   // CPython's datetime C API: PyDateTimeAPI, one per translation unit

#define TOML_EXCEPTIONS 0   // error-code API (toml::parse_result), no throw across nogil
#include "../third_party/tomlplusplus/toml.hpp"
#include "../../core.h"
#include "../common.h"
#include "../materialize.h"

namespace pygim::pathlike::detail {

// datetime's C API table, taken while the module loads (engines::toml::on_load).
inline void import_datetime_api() {
    PyDateTime_IMPORT;
    if (PyDateTimeAPI == nullptr) throw py::error_already_set();
}

// What a datetime C API call made, owned; its error raised.
[[nodiscard]] inline py::object made(PyObject* object) {
    if (object == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(object);
}

// ── Read side ──────────────────────────────────────────────────────────────

[[nodiscard]] inline py::object toml_date_to_py(const toml::date& d) {
    return made(PyDate_FromDate(d.year, d.month, d.day));
}

[[nodiscard]] inline py::object toml_time_to_py(const toml::time& t) {
    return made(PyTime_FromTime(t.hour, t.minute, t.second, static_cast<int>(t.nanosecond / 1000)));
}

// A datetime at its offset — timezone(timedelta(minutes=offset)), which for 0 is timezone.utc, as
// tomllib's — or naive without one.
[[nodiscard]] inline py::object toml_datetime_to_py(const toml::date_time& dt) {
    py::object tz = py::none();
    if (dt.offset) tz = made(PyTimeZone_FromOffset(made(PyDelta_FromDSU(0, dt.offset->minutes * 60, 0)).ptr()));
    return made(PyDateTimeAPI->DateTime_FromDateAndTime(dt.date.year, dt.date.month, dt.date.day, dt.time.hour, dt.time.minute,
                                                         dt.time.second, static_cast<int>(dt.time.nanosecond / 1000), tz.ptr(),
                                                         PyDateTimeAPI->DateTimeType));
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
        throw std::invalid_argument("TOML parse error (" + f.fspath() + ", line " +
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
    PyObject* const o = obj.ptr();
    const auto day_of = [o] {
        return toml::date{static_cast<uint16_t>(PyDateTime_GET_YEAR(o)), static_cast<uint8_t>(PyDateTime_GET_MONTH(o)),
                          static_cast<uint8_t>(PyDateTime_GET_DAY(o))};
    };
    if (PyDateTime_Check(o)) {        // before date: a datetime IS a date
        toml::date_time dt;
        dt.date = day_of();
        dt.time = {static_cast<uint8_t>(PyDateTime_DATE_GET_HOUR(o)), static_cast<uint8_t>(PyDateTime_DATE_GET_MINUTE(o)),
                   static_cast<uint8_t>(PyDateTime_DATE_GET_SECOND(o)), static_cast<uint32_t>(PyDateTime_DATE_GET_MICROSECOND(o)) * 1000u};
        const py::object off = obj.attr("utcoffset")();   // its tzinfo's answer: any tzinfo, not only timezone
        if (!off.is_none()) {
            const int seconds = PyDateTime_DELTA_GET_DAYS(off.ptr()) * 86'400 + PyDateTime_DELTA_GET_SECONDS(off.ptr());
            dt.offset = toml::time_offset(0, static_cast<int16_t>(seconds / 60));
        }
        return ins(dt);
    }
    if (PyDate_Check(o)) return ins(day_of());
    if (PyTime_Check(o)) {
        return ins(toml::time{static_cast<uint8_t>(PyDateTime_TIME_GET_HOUR(o)), static_cast<uint8_t>(PyDateTime_TIME_GET_MINUTE(o)),
                              static_cast<uint8_t>(PyDateTime_TIME_GET_SECOND(o)),
                              static_cast<uint32_t>(PyDateTime_TIME_GET_MICROSECOND(o)) * 1000u});
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

    static void on_load() { detail::import_datetime_api(); }
    static py::object load(const file& f, detail::KeyCache& keys) { return detail::load_toml(f, keys); }
    static void write(const file& f, py::handle obj) { detail::write_toml(f, obj); }
};

}  // namespace pygim::pathlike::engines
