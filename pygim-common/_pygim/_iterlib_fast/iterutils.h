#pragma once

#include <pybind11/pybind11.h>
#include <iostream>         // std::string

namespace py = pybind11;

// Base case function template for generic types
template <typename T>
inline std::enable_if_t<
        !std::is_same_v<T, py::str> &&
        !std::is_same_v<T, py::int_> &&
        !std::is_same_v<T, py::float_> &&
        !std::is_same_v<T, py::bool_> &&
        !std::is_same_v<T, py::bytes> &&
        !std::is_same_v<T, py::memoryview> &&
        !std::is_same_v<T, py::bytearray> &&
        !std::is_same_v<T, py::tuple> &&
        !std::is_same_v<T, py::dict> &&
        !std::is_same_v<T, py::list> &&
        !std::is_same_v<T, py::set> &&
        !std::is_same_v<T, py::iterator> &&
        !std::is_same_v<T, py::iterable> &&
        !std::is_same_v<T, py::type>,
    bool>
is_container(const T& obj) {
    std::cout << "is_container" << std::endl;
    if (py::isinstance<py::str>(obj) | py::isinstance<py::bytes>(obj)) {
        std::cout << "isinstance<str> || isinstance<bytes>" << std::endl;
        return false;
    }
    if (py::hasattr(obj, "__iter__")) {
        std::cout << "hasattr __iter__" << std::endl;
        return true;
    }
    std::cout << "return false" << std::endl;
    return false;
}

// Specialization for py::str and py::bytes
template <typename T>
inline std::enable_if_t<
        std::is_same_v<T, py::bool_> ||
        std::is_same_v<T, py::int_> ||
        std::is_same_v<T, py::float_> ||
        std::is_same_v<T, py::str> ||
        std::is_same_v<T, py::type> ||
        std::is_same_v<T, py::bytes>,
    bool>
is_container(const T& obj) {
    return false;
}


// Specialization for py::memoryview
template <typename T>
inline std::enable_if_t<
        std::is_same_v<T, py::set> ||
        std::is_same_v<T, py::list> ||
        std::is_same_v<T, py::tuple> ||
        std::is_same_v<T, py::bytearray> ||
        std::is_same_v<T, py::dict> ||
        std::is_same_v<T, py::iterator> ||
        std::is_same_v<T, py::iterable> ||
        std::is_same_v<T, py::memoryview>,
    bool>
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
