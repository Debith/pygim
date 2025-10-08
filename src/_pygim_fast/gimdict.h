#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <unordered_map>
#include <string>
#include <optional>

namespace py = pybind11;

/**
 * GimDict - A simple dictionary-like class with C++ backing.
 * 
 * Provides basic dictionary operations with high performance.
 */
class GimDict {
public:
    GimDict() = default;

    // Set an item
    void set_item(const std::string &key, const py::object &value) {
        m_data[key] = value;
    }

    // Get an item (throws KeyError if not found)
    py::object get_item(const std::string &key) const {
        auto it = m_data.find(key);
        if (it == m_data.end()) {
            throw py::key_error("Key not found: " + key);
        }
        return it->second;
    }

    // Check if key exists
    bool contains(const std::string &key) const {
        return m_data.find(key) != m_data.end();
    }

    // Get size
    size_t size() const {
        return m_data.size();
    }

    // Clear all items
    void clear() {
        m_data.clear();
    }

    // String representation
    std::string repr() const {
        return "<GimDict with " + std::to_string(m_data.size()) + " items>";
    }

private:
    std::unordered_map<std::string, py::object> m_data;
};
