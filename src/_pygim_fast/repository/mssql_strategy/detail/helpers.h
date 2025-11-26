#pragma once

#include <regex>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>

#include "value_objects.h"

namespace pygim::detail {

namespace py = pybind11;

inline bool is_polars_dataframe(const py::object &obj) {
    try {
        py::object cls = obj.attr("__class__");
        std::string mod = py::str(cls.attr("__module__"));
        return mod.find("polars") != std::string::npos && py::hasattr(obj, "get_column");
    } catch (...) {
        return false;
    }
}

inline std::string extract_table(const py::object &key) {
    if (py::isinstance<py::tuple>(key)) {
        auto t = key.cast<py::tuple>();
        if (t.size() >= 1) {
            return t[0].cast<std::string>();
        }
    }
    throw std::runtime_error("MssqlStrategyNative: key must be a tuple(table, pk)");
}

inline py::object extract_pk(const py::object &key) {
    auto t = key.cast<py::tuple>();
    if (t.size() < 2) {
        throw std::runtime_error("MssqlStrategyNative: key missing pk value");
    }
    return t[1];
}

inline bool is_valid_identifier(const std::string &s) {
    static const std::regex r("^[A-Za-z_][A-Za-z0-9_]*$");
    return std::regex_match(s, r);
}

inline TableSpec make_table_spec(std::string table,
                                 std::vector<std::string> columns,
                                 std::optional<std::string> key_column,
                                 std::string table_hint) {
    if (!is_valid_identifier(table)) {
        throw std::runtime_error("Invalid table identifier");
    }
    for (const auto &col : columns) {
        if (!is_valid_identifier(col)) {
            throw std::runtime_error("Invalid column identifier");
        }
    }
    if (key_column && !is_valid_identifier(*key_column)) {
        throw std::runtime_error("Invalid key column identifier");
    }
    return TableSpec{std::move(table), std::move(columns), std::move(key_column), std::move(table_hint)};
}

} // namespace pygim::detail
