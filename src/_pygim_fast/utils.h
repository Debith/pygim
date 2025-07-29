#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <string>
#include <unordered_map>
#include <sstream>
#include <ranges>
#include <algorithm>

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