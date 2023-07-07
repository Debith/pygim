#pragma once

#include <pybind11/pybind11.h>
#include <iostream>         // std::string

namespace py = pybind11;

// Base case function template for generic types
template <typename T>
inline std::enable_if_t<
        !std::is_same_v<T, py::str> &&
        !std::is_same_v<T, py::bytes> &&
        !std::is_same_v<T, py::memoryview> &&
        !std::is_same_v<T, py::type>,
    bool>
is_container(const T& obj) {
    if (py::isinstance<py::str>(obj) | py::isinstance<py::bytes>(obj)) {
        return false;
    }
    if (py::hasattr(obj, "__iter__")) {
        return true;
    }
    return false;
}

// Specialization for py::str and py::bytes
template <typename T>
inline std::enable_if_t<
        std::is_same_v<T, py::str> ||
        std::is_same_v<T, py::bytes>,
    bool>
is_container(const T& obj) {
    std::cout << "<-> is_container(str, bytes)" << std::endl;
    return false;
}


// Specialization for py::memoryview
template <typename T>
inline std::enable_if_t<std::is_same_v<T, py::type>, bool>
is_container(const T& obj) {
    std::cout << "<-> is_container(type)" << std::endl;
    return false;
}


// Specialization for py::memoryview
template <typename T>
inline std::enable_if_t<std::is_same_v<T, py::memoryview>, bool>
is_container(const T& obj) {
    std::cout << "<-> is_container(memoryview)" << std::endl;
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
