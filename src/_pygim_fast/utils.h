#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <string>
#include <unordered_map>
#include <sstream>
#include <ranges>
#include <algorithm>

namespace py = pybind11;


inline std::string to_csv(std::vector<std::string> strings, bool sorted = false) {
    if (sorted) {
        std::ranges::sort(strings);
    }
    std::ostringstream oss;
    bool first = true;
    for (auto&& s : strings) {
        if (!first) oss << ", ";
        first = false;
        oss << s;
    }
    return oss.str();
}

// Primary overload: works on std::string
inline bool is_dunder(const std::string &attr) {
    // Must be at least 4 characters: two leading and two trailing underscores
    if (attr.size() < 4) {
        return false;
    }
    // Check first two and last two characters
    return attr[0] == '_' &&
           attr[1] == '_' &&
           attr[attr.size() - 2] == '_' &&
           attr[attr.size() - 1] == '_';
}

// Overload for py::str
inline bool is_dunder(const py::str &attr) {
    // Convert to std::string and reuse string overload
    std::string s = static_cast<std::string>(attr);
    return is_dunder(s);
}

// (Optional) Overload for const char*
inline bool is_dunder(const char *attr) {
    if (!attr) {
        return false;
    }
    std::string s(attr);
    return is_dunder(s);
}


inline bool is_generator(const py::object &instance) {
    // Import types.GeneratorType and check isinstance
    static py::object generator_type = py::module::import("types").attr("GeneratorType");
    return py::isinstance(instance, generator_type);
}