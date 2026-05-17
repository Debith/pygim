
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include <optional>
#include <string>
#include <unordered_map>
#include "core_utils.h"
#include "adapter_utils.h"
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
