#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <vector>
#include <string>
#include <sstream>

namespace py = pybind11;


class StringList {
public:
    StringList(std::vector<std::string> strings, std::string sep = "\n", std::string encoding = "utf-8")
    : _sep(sep.empty() ? _sep : sep), _parts(strings), _encoding(encoding) {}

    std::string repr() const {
        return "<" + std::string(py::str(py::type::of(*this))) + ":" + str() + ">";
    }

    std::string str() const {
        std::ostringstream oss;
        for(size_t i = 0; i < _parts.size(); ++i) {
            oss << _parts[i];
            if (i != _parts.size() - 1) {
                oss << _sep;
            }
        }
        return oss.str();
    }

    // Overloading for std::string
    StringList& iadd(const std::string& other) {
        _parts.push_back(other);
        return *this;
    }

    // Overloading for bytes
    StringList& iadd(const char* other) {
        _parts.push_back(std::string(other));
        return *this;
    }

    // Overloading for a list of strings (iterable)
    StringList& iadd(const std::vector<std::string>& other) {
        _parts.insert(_parts.end(), other.begin(), other.end());
        return *this;
    }

    // Additional overloads can be added for other types as needed.

private:
    std::string _sep = "\n";
    std::vector<std::string> _parts;
    std::string _encoding;
};
