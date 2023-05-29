#pragma once

#include <pybind11/pybind11.h>
#include <iostream>         // std::string

namespace py = pybind11;
/*
inline bool is_container(py::handle obj) {
    if (py::isinstance<py::str>(obj) || py::isinstance<py::bytes>(obj)) {
        return false;
    }

    if (py::hasattr(obj, "__iter__")) {
        return true;
    }

    return py::isinstance<py::memoryview>(obj);
};
*/
// Base case function template for generic types
template <typename T>
inline std::enable_if_t<!std::is_same_v<T, py::str> && !std::is_same_v<T, py::bytes> && !std::is_same_v<T, py::memoryview>, bool>
is_container(const T& obj) {
    if (py::hasattr(obj, "__iter__")) {
        return true;
    }
    return false;
}

// Specialization for py::str and py::bytes
template <typename T>
inline std::enable_if_t<std::is_same_v<T, py::str> || std::is_same_v<T, py::bytes>, bool>
is_container(const T& obj) {
    return false;
}

// Specialization for py::memoryview
template <typename T>
inline std::enable_if_t<std::is_same_v<T, py::memoryview>, bool>
is_container(const T& obj) {
    return true;
}



inline py::tuple tuplify(const py::tuple& arg) {
    return arg;
};

inline py::tuple tuplify(const py::dict& arg) {
    py::list kv_pairs;
    for (const auto& item : arg) {
        kv_pairs.append(py::make_tuple(item.first, item.second));
    }
    return py::tuple(kv_pairs);
};

inline py::tuple tuplify(const py::iterable& arg) {
    return py::tuple(arg);
};

inline py::tuple tuplify(const py::handle& arg) {
    return py::make_tuple(arg);
};

inline py::tuple tuplify(const py::str& arg) {
    return py::make_tuple(arg);
};

inline py::tuple tuplify(const py::bytes& arg) {
    return py::make_tuple(arg);
};