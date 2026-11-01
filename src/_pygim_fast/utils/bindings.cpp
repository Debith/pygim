
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/gil_safe_call_once.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include "core_utils.h"
#include "adapter_utils.h"
#include "memory.h"
#include "cpu.h"
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
      if (value == "extend") return MergeStrategy::Extend;
      if (value == "union") return MergeStrategy::Union;
      if (value == "deep") return MergeStrategy::Deep;
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
            case MergeStrategy::Extend:
                  return "extend";
            case MergeStrategy::Union:
                  return "union";
            case MergeStrategy::Deep:
                  return "deep";
      }
      return "replace";
}

MergeStrategy parse_merge_strategy_obj(py::handle obj) {
      if (py::isinstance<py::str>(obj)) {
            return parse_merge_strategy(obj.cast<std::string>());
      }
      if (const py::object name = optional_attribute(obj, interned<"__name__">())) {
            return parse_merge_strategy(py::str(name).cast<std::string>());
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

// collections.abc.Mapping, kept when the module loads, where gimdict registers as a MutableMapping: a
// Mapping's == asks isinstance(other, Mapping), and nothing is imported while running (#248).
PYBIND11_CONSTINIT py::gil_safe_call_once_and_store<py::object> mapping_abc;

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
            PyObject* value = PyDict_GetItemWithError(m_values.ptr(), key.ptr());   // borrowed
            if (value == nullptr) throw missing(key);
            return py::reinterpret_borrow<py::object>(value);
      }

      void setitem(py::handle key, py::handle value) {
            m_values[key] = value;
      }

      void delitem(py::handle key) {
            if (PyDict_DelItem(m_values.ptr(), key.ptr()) != 0) throw py::error_already_set();   // KeyError(key) when missing
      }

      py::object get(py::handle key, py::object default_value) const {
            PyObject* value = PyDict_GetItemWithError(m_values.ptr(), key.ptr());
            if (value != nullptr) return py::reinterpret_borrow<py::object>(value);
            if (PyErr_Occurred()) throw py::error_already_set();
            return default_value;
      }

      // pop(key[, default]), as dict's: the value, removed; `default` for a missing key when one is given.
      py::object pop(py::handle key, const py::args& fallback) {
            if (fallback.size() > 1) throw py::type_error("pop expected at most 2 arguments, got " + std::to_string(fallback.size() + 1));
            PyObject* value = PyDict_GetItemWithError(m_values.ptr(), key.ptr());
            if (value == nullptr) {
                  if (PyErr_Occurred() || fallback.empty()) throw missing(key);
                  return fallback[0];
            }
            py::object held = py::reinterpret_borrow<py::object>(value);   // held before the dict lets it go
            if (PyDict_DelItem(m_values.ptr(), key.ptr()) != 0) throw py::error_already_set();
            return held;
      }

      py::object popitem() { return m_values.attr("popitem")(); }

      void clear() { PyDict_Clear(m_values.ptr()); }

      py::object setdefault(py::handle key, py::handle default_value) {
            PyObject* value = PyDict_SetDefault(m_values.ptr(), key.ptr(), default_value.ptr());   // borrowed
            if (value == nullptr) throw py::error_already_set();
            return py::reinterpret_borrow<py::object>(value);
      }

      // update(other=(), /, **values): as dict's, each set as d[key] = value sets it — not merged.
      void update(const py::args& args, const py::kwargs& kwargs) { m_values.attr("update")(*args, **kwargs); }

      // The values' own live views: dict's keys, values and items, which are collections.abc's.
      py::object keys() const { return m_values.attr("keys")(); }
      py::object values() const { return m_values.attr("values")(); }
      py::object items() const { return m_values.attr("items")(); }
      py::object reversed() const { return m_values.attr("__reversed__")(); }

      // == as a Mapping's: any Mapping whose items are these, compared as dicts; NotImplemented for
      // anything else. The strategies are how values merge, not values, and are not compared.
      py::object equals(py::handle other) const {
            py::object theirs;
            if (py::isinstance<PyGimDict>(other)) {
                  theirs = other.cast<const PyGimDict&>().m_values;
            } else if (PyDict_Check(other.ptr())) {
                  theirs = py::reinterpret_borrow<py::object>(other);
            } else {
                  const int mapping = PyObject_IsInstance(other.ptr(), mapping_abc.get_stored().ptr());
                  if (mapping < 0) throw py::error_already_set();
                  if (mapping == 0) return py::reinterpret_borrow<py::object>(Py_NotImplemented);
                  theirs = py::dict(other.attr("items")());
            }
            const int same = PyObject_RichCompareBool(m_values.ptr(), theirs.ptr(), Py_EQ);
            if (same < 0) throw py::error_already_set();
            return py::bool_(same == 1);
      }

      std::string representation() const { return "gimdict(" + py::repr(m_values).cast<std::string>() + ")"; }

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
            out.m_values = copy_of(m_values);
            out.m_key_strategies = copy_of(m_key_strategies);
            out.m_type_strategies = m_type_strategies;
            out.m_explicit_default = m_explicit_default;

            for (const auto& item : other.m_values) {
                  out.merge_in(item.first, item.second);
            }
            return out;
      }

      py::object iter() const {
            return m_values.attr("__iter__")();
      }

private:
      // A new dict of `d`'s items. py::dict(d) is not one: given a dict, it shares it, and every merge
      // merged into its left side's values (2026-10-30).
      [[nodiscard]] static py::dict copy_of(const py::dict& d) {
            PyObject* copy = PyDict_Copy(d.ptr());
            if (copy == nullptr) throw py::error_already_set();
            return py::reinterpret_steal<py::dict>(copy);
      }

      // KeyError(key), as dict raises it — the key in a tuple, so a tuple key is not taken for the
      // error's arguments — unless the lookup raised already (an unhashable key's TypeError).
      [[nodiscard]] static py::error_already_set missing(py::handle key) {
            if (!PyErr_Occurred()) PyErr_SetObject(PyExc_KeyError, py::make_tuple(key).ptr());
            return py::error_already_set();
      }

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

      // `a op b` through the C API, where builtins.max and min were imported on every merge; an
      // error of the comparison (no ordering) is raised as Python raises it.
      [[nodiscard]] static bool compares(py::handle a, py::handle b, int op) {
            const int r = PyObject_RichCompareBool(a.ptr(), b.ptr(), op);
            if (r < 0) throw py::error_already_set();
            return r == 1;
      }

      py::object apply(MergeStrategy strategy, py::handle lhs, py::handle rhs) const {
            switch (strategy) {
                  case MergeStrategy::Replace:
                        return py::reinterpret_borrow<py::object>(rhs);
                  case MergeStrategy::Sum:
                  case MergeStrategy::Extend: {
                        // Extend rides PyNumber_Add: list/tuple/str concatenate there too.
                        PyObject* out = PyNumber_Add(lhs.ptr(), rhs.ptr());
                        if (out == nullptr) {
                              throw py::type_error("sum/extend strategy failed for incompatible values");
                        }
                        return py::reinterpret_steal<py::object>(out);
                  }
                  case MergeStrategy::Max:   // builtins.max(lhs, rhs): rhs only when it is greater — max keeps the first
                        return py::reinterpret_borrow<py::object>(compares(rhs, lhs, Py_GT) ? rhs : lhs);
                  case MergeStrategy::Min:   // builtins.min(lhs, rhs): rhs only when it is less
                        return py::reinterpret_borrow<py::object>(compares(rhs, lhs, Py_LT) ? rhs : lhs);
                  case MergeStrategy::Union:
                        return apply_union(lhs, rhs);
                  case MergeStrategy::Deep:
                        return apply_deep(lhs, rhs);
            }
            return py::reinterpret_borrow<py::object>(rhs);
      }

      // Order-preserving union of sequences: keep lhs, append rhs elements not
      // already present. Non-sequences (or strings) fall back to replace.
      static py::object apply_union(py::handle lhs, py::handle rhs) {
            if (!py::isinstance<py::list>(lhs) || !py::isinstance<py::list>(rhs)) {
                  return py::reinterpret_borrow<py::object>(rhs);
            }
            py::list out(py::reinterpret_borrow<py::list>(lhs));
            py::list merged;
            for (const auto& item : out) merged.append(item);
            for (const auto& item : py::reinterpret_borrow<py::list>(rhs)) {
                  if (!merged.contains(item)) merged.append(item);
            }
            return merged;
      }

      // Recursive mapping merge: shared keys resolve through this dict's own
      // strategies (so nested dicts keep deep-merging, numeric leaves keep
      // stacking). Non-mappings fall back to replace.
      py::object apply_deep(py::handle lhs, py::handle rhs) const {
            if (!py::isinstance<py::dict>(lhs) || !py::isinstance<py::dict>(rhs)) {
                  return py::reinterpret_borrow<py::object>(rhs);
            }
            py::dict out;
            for (const auto& item : py::reinterpret_borrow<py::dict>(lhs)) {
                  out[item.first] = item.second;
            }
            for (const auto& item : py::reinterpret_borrow<py::dict>(rhs)) {
                  if (!out.contains(item.first)) {
                        out[item.first] = item.second;
                        continue;
                  }
                  auto l = py::reinterpret_borrow<py::object>(out[item.first]);
                  auto r = py::reinterpret_borrow<py::object>(item.second);
                  out[item.first] = apply(strategy_for(item.first, l, r), l, r);
            }
            return out;
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
    m.def("is_dunder", [](std::string_view name) { return pygim::is_dunder(name); }, py::arg("name"),
          "Whether name is a dunder name (__x__): four characters at least, two underscores at each end.");

    m.def("to_csv", &pygim::to_csv, py::arg("strings"), py::arg("sorted") = false,
          "The strings with ', ' between each two, sorted first when sorted is true.");

    m.def("format_bytes_per_second",
          &pygim::format_bytes_per_second,
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

    // The processor pygim's kernels size their work for (utils/cpu.h): read once, never 0.
    m.def("cpu", [] {
              const pygim::cpu& c = pygim::this_cpu();
              py::dict d;
              d["l1d"] = c.caches().l1d();
              d["l2"] = c.caches().l2();
              d["l3"] = c.caches().l3();
              d["line"] = c.caches().line();
              d["avx2"] = c.avx2();
              return d;
          },
          "This machine's processor as pygim's kernels see it: data caches in bytes as one core sees them\n"
          "(l1d, l2, l3 the last level; a size the system does not report is a typical one, never 0), the\n"
          "cache line, and whether AVX2 kernels may run.");

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
          &pygim::calculate_rate,
          py::arg("quantity"),
          py::arg("quantity_unit"),
          py::arg("duration"),
          py::arg("duration_unit"),
          py::arg("precision") = 2,
          "Compute a throughput string from quantity+unit over duration+unit.");

    auto gimdict_cls = py::class_<PyGimDict>(m, "gimdict",
        "A mutable mapping whose values merge by strategy: merge_in(key, value) and a | b combine an existing\n"
        "value with a new one by the key's strategy, else the new value's type's, else the default. The\n"
        "strategies: replace, extend, union, deep, sum, max, min (the module's constants, or a builtin: max, min, sum).")
        .def(py::init([](py::object initial, py::kwargs kwargs) { return PyGimDict(initial, kwargs); }), py::arg("initial") = py::none(),
             "The values of mapping `initial`, if given; each keyword a type's strategy: gimdict(d, int=max, str=replace).")
        .def("set", &PyGimDict::set, py::arg("key"), py::arg("value"), "d[key] = value, replacing what is there.")
        .def("get", &PyGimDict::get, py::arg("key"), py::arg("default") = py::none(),
             "The value of `key`; `default` when it has none.")
        .def("pop", &PyGimDict::pop, py::arg("key"),
             "Removes `key` and gives its value; when it has none, `default` if one is given, else KeyError.")
        .def("popitem", &PyGimDict::popitem, "Removes the key set last and gives (key, value); KeyError when empty.")
        .def("clear", &PyGimDict::clear, "Removes every key; the strategies stay.")
        .def("setdefault", &PyGimDict::setdefault, py::arg("key"), py::arg("default") = py::none(),
             "The value of `key`; when it has none, `default`, set as its value.")
        .def("update", &PyGimDict::update,
             "Sets each key of a mapping, each (key, value) pair, then each keyword, as d[key] = value does: replaced, not merged.")
        .def("keys", &PyGimDict::keys, "The keys, a live view.")
        .def("values", &PyGimDict::values, "The values, a live view.")
        .def("items", &PyGimDict::items, "The (key, value) pairs, a live view.")
        .def("contains", &PyGimDict::contains, py::arg("key"), "Whether `key` has a value.")
        .def("set_strategy", &PyGimDict::set_strategy, py::arg("key"), py::arg("strategy"), "How `key`'s values merge, before any type's.")
        .def("set_type_strategy", &PyGimDict::set_type_strategy, py::arg("type_name"), py::arg("strategy"),
             "How values of the type named `type_name` ('int', 'list') merge.")
        .def("type_strategy", &PyGimDict::type_strategy, py::arg("type_name"),
             "The strategy values of the type named `type_name` merge by: set, or that type's default.")
        .def("default_strategy", &PyGimDict::default_strategy, "The strategy set for every type, or 'type-default' (each type's own).")
        .def("set_default_strategy", &PyGimDict::set_default_strategy, py::arg("strategy"),
             "How values merge when neither their key nor their type has a strategy.")
        .def("merge_in", &PyGimDict::merge_in, py::arg("key"), py::arg("value"),
             "`value` merged into `key`'s by its strategy; set when `key` has none.")
        .def("merge", &PyGimDict::merged, py::arg("other"), "A new gimdict: this one with every value of `other` merged in (a | b).")
        .def("__getitem__", &PyGimDict::getitem, py::arg("key"), "The value of `key`; KeyError when it has none.")
        .def("__setitem__", &PyGimDict::setitem, py::arg("key"), py::arg("value"), "Sets `key` to `value`, replacing what is there.")
        .def("__delitem__", &PyGimDict::delitem, py::arg("key"), "Removes `key`; KeyError when it has no value.")
        .def("__iter__", &PyGimDict::iter, "The keys, in the order they were set.")
        .def("__reversed__", &PyGimDict::reversed, "The keys, the one set last first.")
        .def("__eq__", &PyGimDict::equals, py::is_operator(), py::arg("other"),
             "Whether `other` is a mapping of the same items; the strategies are not compared.")
        .def("__repr__", &PyGimDict::representation)
        .def("__len__", &PyGimDict::size, "How many keys.")
        .def("__contains__", &PyGimDict::contains, py::is_operator(), py::arg("key"), "Whether `key` has a value.")
        .def("__or__", &PyGimDict::merged, py::is_operator(), py::arg("other"), "A new gimdict: this one with every value of `other` merged in.");

    py::module_ collections_abc = py::module_::import("collections.abc");
    collections_abc.attr("MutableMapping").attr("register")(gimdict_cls);
    mapping_abc.call_once_and_store_result([&] { return collections_abc.attr("Mapping"); });

    // Convenience named strategies for ergonomic kwargs like str=utils.replace
    m.attr("replace") = py::str("replace");
    m.attr("extend") = py::str("extend");
    m.attr("union") = py::str("union");
    m.attr("deep") = py::str("deep");
}
