
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include "core_utils.h"
#include "adapter_utils.h"
#include "memory.h"
#include "duration.h"
#include "timestamp.h"
#include "quick_timer_adapter.h"
#include "../mapping/dynamic_merge_map.h"

namespace py = pybind11;

namespace {

using MergeStrategy = pygim::mapping::MergeStrategy;

MergeStrategy parse_merge_strategy(const std::string& value) {
      if (value == "sum") return MergeStrategy::Sum;
      if (value == "max") return MergeStrategy::Max;
      if (value == "min") return MergeStrategy::Min;
      if (value == "replace") return MergeStrategy::Replace;
      throw py::value_error("invalid merge strategy: " + value);
}

std::string merge_strategy_name(MergeStrategy strategy) {
      switch (strategy) {
            case MergeStrategy::Sum:
                  return "sum";
            case MergeStrategy::Max:
                  return "max";
            case MergeStrategy::Min:
                  return "min";
            case MergeStrategy::Replace:
                  return "replace";
      }
      return "replace";
}

MergeStrategy parse_merge_strategy_obj(py::handle obj) {
      if (py::isinstance<py::str>(obj)) {
            return parse_merge_strategy(obj.cast<std::string>());
      }
      if (py::hasattr(obj, "__name__")) {
            return parse_merge_strategy(py::str(obj.attr("__name__")).cast<std::string>());
      }
      throw py::value_error("invalid merge strategy object");
}

std::string py_type_name(py::handle obj) {
      return py::str(py::type::of(obj).attr("__name__")).cast<std::string>();
}

MergeStrategy default_strategy_for_type(const std::string& type_name) {
      if (type_name == "int" || type_name == "float" || type_name == "bool") {
            return MergeStrategy::Sum;
      }
      return MergeStrategy::Replace;
}

class PyGimDict {
public:
      PyGimDict(py::object initial = py::none(), py::kwargs type_strategies = py::kwargs()) {
            for (const auto& item : type_strategies) {
                  const auto type_name = py::str(item.first).cast<std::string>();
                  m_type_strategies[type_name] = parse_merge_strategy_obj(item.second);
            }

            if (!initial.is_none()) {
                  if (!PyMapping_Check(initial.ptr())) {
                        throw py::type_error("gimdict initializer must be a mapping");
                  }
                  py::dict d(initial);
                  for (const auto& item : d) {
                        m_values[item.first] = item.second;
                  }
            }
      }

      std::size_t size() const {
            return py::len(m_values);
      }

      bool contains(py::handle key) const {
            return m_values.contains(key);
      }

      py::object getitem(py::handle key) const {
            if (!m_values.contains(key)) {
                  throw py::key_error("key not found");
            }
            return py::reinterpret_borrow<py::object>(m_values[key]);
      }

      void setitem(py::handle key, py::handle value) {
            m_values[key] = value;
      }

      void delitem(py::handle key) {
            if (!m_values.contains(key)) {
                  throw py::key_error("key not found");
            }
            m_values.attr("pop")(key);
      }

      py::object get(py::handle key, py::object default_value = py::none()) const {
            if (m_values.contains(key)) {
                  return py::reinterpret_borrow<py::object>(m_values[key]);
            }
            if (default_value.is_none()) {
                  throw py::key_error("key not found");
            }
            return default_value;
      }

      void set(py::handle key, py::handle value) {
            setitem(key, value);
      }

      void set_strategy(py::handle key, py::handle strategy) {
            m_key_strategies[key] = py::str(merge_strategy_name(parse_merge_strategy_obj(strategy)));
      }

      void set_type_strategy(const std::string& type_name, py::handle strategy) {
            m_type_strategies[type_name] = parse_merge_strategy_obj(strategy);
      }

      std::string type_strategy(const std::string& type_name) const {
            const auto it = m_type_strategies.find(type_name);
            if (it == m_type_strategies.end()) {
                  return merge_strategy_name(default_strategy_for_type(type_name));
            }
            return merge_strategy_name(it->second);
      }

      void set_default_strategy(py::handle strategy) {
            m_explicit_default = parse_merge_strategy_obj(strategy);
      }

      std::string default_strategy() const {
            return m_explicit_default.has_value()
                  ? merge_strategy_name(*m_explicit_default)
                  : "type-default";
      }

      void merge_in(py::handle key, py::handle value) {
            if (!m_values.contains(key)) {
                  m_values[key] = value;
                  return;
            }
            auto lhs = py::reinterpret_borrow<py::object>(m_values[key]);
            auto rhs = py::reinterpret_borrow<py::object>(value);
            auto strategy = strategy_for(key, lhs, rhs);
            m_values[key] = apply(strategy, lhs, rhs);
      }

      PyGimDict merged(const PyGimDict& other) const {
            PyGimDict out;
            out.m_values = py::dict(m_values);
            out.m_key_strategies = py::dict(m_key_strategies);
            out.m_type_strategies = m_type_strategies;
            out.m_explicit_default = m_explicit_default;

            for (const auto& item : other.m_values) {
                  out.merge_in(item.first, item.second);
            }
            return out;
      }

      py::dict to_dict() const {
            return py::dict(m_values);
      }

      py::object iter() const {
            return m_values.attr("__iter__")();
      }

private:
      MergeStrategy strategy_for(py::handle key, py::handle lhs, py::handle rhs) const {
            if (m_key_strategies.contains(key)) {
                  return parse_merge_strategy(py::str(m_key_strategies[key]).cast<std::string>());
            }

            const auto rhs_type = py_type_name(rhs);
            const auto rhs_it = m_type_strategies.find(rhs_type);
            if (rhs_it != m_type_strategies.end()) {
                  return rhs_it->second;
            }

            const auto lhs_type = py_type_name(lhs);
            const auto lhs_it = m_type_strategies.find(lhs_type);
            if (lhs_it != m_type_strategies.end()) {
                  return lhs_it->second;
            }

            if (m_explicit_default.has_value()) {
                  return *m_explicit_default;
            }
            return default_strategy_for_type(rhs_type);
      }

      static py::object apply(MergeStrategy strategy, py::handle lhs, py::handle rhs) {
            switch (strategy) {
                  case MergeStrategy::Replace:
                        return py::reinterpret_borrow<py::object>(rhs);
                  case MergeStrategy::Sum: {
                        PyObject* out = PyNumber_Add(lhs.ptr(), rhs.ptr());
                        if (out == nullptr) {
                              throw py::type_error("sum strategy failed for incompatible values");
                        }
                        return py::reinterpret_steal<py::object>(out);
                  }
                  case MergeStrategy::Max:
                        return py::module_::import("builtins").attr("max")(lhs, rhs);
                  case MergeStrategy::Min:
                        return py::module_::import("builtins").attr("min")(lhs, rhs);
            }
            return py::reinterpret_borrow<py::object>(rhs);
      }

private:
      py::dict m_values{};
      py::dict m_key_strategies{};
      std::unordered_map<std::string, MergeStrategy> m_type_strategies{};
      std::optional<MergeStrategy> m_explicit_default{};
};

// One keyword amount of one unit, as Python writes it: an integer exactly — an int, or anything
// with __index__ (numpy's integers); any other real number (float, Fraction, Decimal) through its
// float, rounded exactly by the core. A bool is no amount of time.
pygim::duration amount_of(const pygim::time_unit& unit, py::handle amount) {
    if (PyBool_Check(amount.ptr())) throw py::type_error(std::string(unit.name()) + " must be a real number, not bool");
    if (PyLong_Check(amount.ptr()) || PyIndex_Check(amount.ptr())) {
        const py::object integer = py::reinterpret_steal<py::object>(PyNumber_Index(amount.ptr()));
        if (!integer) throw py::error_already_set();
        int overflow = 0;
        const long long whole = PyLong_AsLongLongAndOverflow(integer.ptr(), &overflow);
        if (overflow != 0) throw std::overflow_error("a duration past 64 bits of nanoseconds (about 292 years)");
        if (whole == -1 && PyErr_Occurred()) throw py::error_already_set();
        return unit.of(static_cast<std::int64_t>(whole));
    }
    if (!PyNumber_Check(amount.ptr())) {
        throw py::type_error(std::string(unit.name()) + " must be a real number, not " + Py_TYPE(amount.ptr())->tp_name);
    }
    const double real = PyFloat_AsDouble(amount.ptr());
    if (real == -1.0 && PyErr_Occurred()) throw py::error_already_set();
    return unit.of(real);
}

// The duration that **units name: each amount converted, then added exactly.
pygim::duration duration_of_units(const py::kwargs& units) {
    pygim::duration total(0);
    for (const auto& [name, amount] : units) {
        const std::string unit_name = py::str(name);
        const auto unit = pygim::time_unit::named(unit_name);
        if (!unit) {
            throw py::type_error("unknown unit '" + unit_name +
                                 "'; the units are years, weeks, days, hours, mins, secs, msecs, usecs and nsecs");
        }
        total = pygim::sum_exactly(total, amount_of(*unit, amount));
    }
    return total;
}

// Duration(mins=2, secs=30): the units it is spelt in, longest first, which eval() reads back.
std::string duration_repr(pygim::duration d) {
    std::string out = "Duration(";
    std::string_view separator;
    for (const pygim::unit_amount& part : pygim::spelt_in_units(d)) {
        out += std::string(separator) + std::string(part.unit_name()) + "=" + std::to_string(part.amount());
        separator = ", ";
    }
    return out + ")";
}

} // namespace

PYBIND11_MODULE(utils, m) {
    m.doc() = "Utilities for Python.";
    m.def("is_dunder",
          py::overload_cast<const std::string &>(&is_dunder),
          "Check if a std::string is a Python dunder name");
    m.def("is_dunder",
          py::overload_cast<const py::str &>(&is_dunder),
          "Check if a py::str is a Python dunder name");

    m.def("to_csv",
          py::overload_cast<std::vector<std::string>, bool>(&to_csv),
          "Convert a vector of strings to a CSV string");

    m.def("format_bytes_per_second",
          &format_bytes_per_second,
          py::arg("bytes_per_second"),
          py::arg("precision") = 2,
          "Format a bytes-per-second value using human-readable units with configurable precision.");

    // Process memory: one syscall each (utils/memory.h). Process-wide numbers —
    // a benchmark's before/after probe, never a per-object size.
    m.def("rss_bytes", &pygim::memory::resident_bytes,
          "The process's resident set size in bytes (what the OS holds in physical memory now).");
    m.def("rss_mb", &pygim::memory::resident_mb, "rss_bytes() in mebibytes.");
    m.def("peak_rss_bytes", &pygim::memory::peak_resident_bytes,
          "The process's peak resident set size in bytes (its high-water mark).");
    m.def("peak_rss_mb", &pygim::memory::peak_resident_mb, "peak_rss_bytes() in mebibytes.");

    // A length of time (utils/duration.h): one signed 64-bit count of nanoseconds; units are conversions.
    py::class_<pygim::duration>(m, "Duration",
                                "A length of time: one signed 64-bit count of nanoseconds. Duration(mins=1, secs=30).\n"
                                "Units: years (365.2425 days, as C++ counts them), weeks, days, hours, mins, secs, msecs,\n"
                                "usecs, nsecs. They add up; an amount may be negative or a fraction, rounded exactly to\n"
                                "the nearest nanosecond (ties to even). Past 64 bits (about 292 years) is an OverflowError.")
        .def(py::init([](const py::kwargs& units) { return duration_of_units(units); }))
        .def_property_readonly("nanoseconds", [](pygim::duration d) { return d.count(); }, "The number itself.")
        .def_property_readonly("seconds", [](pygim::duration d) { return static_cast<double>(d.count()) / 1e9; },
                               "The same in seconds, as a float: a view, not a second store.")
        .def("__int__", [](pygim::duration d) { return d.count(); })
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self < py::self)
        .def(py::self <= py::self)
        .def(py::self > py::self)
        .def(py::self >= py::self)
        .def("__hash__", [](pygim::duration d) { return d.count(); })
        .def("__repr__", &duration_repr);

    // Time that moves only when told (utils/timestamp.h): the clock for testing timing code exactly.
    py::class_<pygim::manual_time>(m, "ManualTime",
                                   "Time that moves only when told: a clock for testing timing code exactly.")
        .def(py::init<>())
        .def("now", &pygim::manual_time::now_ns, "Nanoseconds since this time started at 0.")
        .def("advance",
             [](pygim::manual_time& time, std::optional<pygim::duration> by, const py::kwargs& units) {
                 if (by && units.size() != 0) throw py::type_error("advance() takes a Duration or units, not both");
                 time.advance(by ? *by : duration_of_units(units));
             },
             py::arg("by") = py::none(),
             "Moves time by a Duration or by units: advance(mins=1, secs=30). Past 64 bits it raises\n"
             "OverflowError and time does not move.");

    // A calendar moment (utils/timestamp.h): nanoseconds since the Unix epoch, from the wall clock.
    py::class_<pygim::timestamp>(m, "Timestamp", "A moment: nanoseconds since the Unix epoch (1970-01-01T00:00:00Z).")
        .def(py::init<std::int64_t>(), py::arg("nanoseconds"), "The moment `nanoseconds` after the epoch: Timestamp(12333).")
        .def_static("now", [] { return pygim::timestamp::now(); }, "The wall clock's current moment.")
        .def_property_readonly("nanoseconds", &pygim::timestamp::nanoseconds, "Nanoseconds since the epoch.")
        .def("__int__", &pygim::timestamp::nanoseconds)
        .def("__sub__", [](const pygim::timestamp& later, const pygim::timestamp& earlier) { return later - earlier; },
             py::is_operator(), "The Duration from the other moment to this one.")
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self < py::self)
        .def(py::self <= py::self)
        .def(py::self > py::self)
        .def(py::self >= py::self)
        .def("__hash__", &pygim::timestamp::nanoseconds)
        .def("__repr__", [](const pygim::timestamp& t) { return "Timestamp(" + std::to_string(t.nanoseconds()) + ")"; });

    // One timer for a block and its laps (utils/quick_timer.h), its clock and its report given.
    pygim::adapter::bind_quick_timer(m);

    m.def("calculate_rate",
          &calculate_rate,
          py::arg("quantity"),
          py::arg("quantity_unit"),
          py::arg("duration"),
          py::arg("duration_unit"),
          py::arg("precision") = 2,
          "Compute a throughput string from quantity+unit over duration+unit.");

      auto gimdict_cls = py::class_<PyGimDict>(m, "gimdict", "Dynamic MutableMapping with merge strategies")
            .def(py::init([](py::object initial, py::kwargs kwargs) {
                        return PyGimDict(initial, kwargs);
                   }),
                   py::arg("initial") = py::none())
        .def("set", &PyGimDict::set, py::arg("key"), py::arg("value"))
        .def("get", &PyGimDict::get, py::arg("key"), py::arg("default") = py::none())
        .def("contains", &PyGimDict::contains, py::arg("key"))
        .def("set_strategy", &PyGimDict::set_strategy, py::arg("key"), py::arg("strategy"))
        .def("set_type_strategy", &PyGimDict::set_type_strategy, py::arg("type_name"), py::arg("strategy"))
        .def("type_strategy", &PyGimDict::type_strategy, py::arg("type_name"))
        .def("default_strategy", &PyGimDict::default_strategy)
        .def("set_default_strategy", &PyGimDict::set_default_strategy, py::arg("strategy"))
        .def("merge_in", &PyGimDict::merge_in, py::arg("key"), py::arg("value"))
        .def("merge", &PyGimDict::merged, py::arg("other"))
        .def("to_dict", &PyGimDict::to_dict)
        .def("__getitem__", &PyGimDict::getitem)
        .def("__setitem__", &PyGimDict::setitem)
        .def("__delitem__", &PyGimDict::delitem)
        .def("__iter__", &PyGimDict::iter)
        .def("__len__", &PyGimDict::size)
        .def("__contains__", &PyGimDict::contains, py::is_operator())
        .def("__or__", &PyGimDict::merged, py::is_operator());

    py::module_ collections_abc = py::module_::import("collections.abc");
    collections_abc.attr("MutableMapping").attr("register")(gimdict_cls);

    // Convenience named strategy for ergonomic kwargs like str=utils.replace
    m.attr("replace") = py::str("replace");
}
