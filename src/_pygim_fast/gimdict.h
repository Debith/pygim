#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <unordered_map>
#include <string>
#include <optional>
#include <vector>
#include <sstream>

namespace py = pybind11;

/**
 * GimDict - A high-performance dictionary-like class with C++ backing.
 * 
 * Implements the full MutableMapping interface with support for multiple backends.
 * Currently uses std::unordered_map (compatible with tsl::robin_map interface).
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

    // Get an item with default value
    py::object get(const std::string &key, py::object default_value = py::none()) const {
        auto it = m_data.find(key);
        if (it == m_data.end()) {
            return default_value;
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

    // Delete an item
    void del_item(const std::string &key) {
        auto it = m_data.find(key);
        if (it == m_data.end()) {
            throw py::key_error("Key not found: " + key);
        }
        m_data.erase(it);
    }

    // Pop an item (with optional default)
    py::object pop(const std::string &key, py::object default_value = py::none()) {
        auto it = m_data.find(key);
        if (it == m_data.end()) {
            if (default_value.is_none()) {
                throw py::key_error("Key not found: " + key);
            }
            return default_value;
        }
        py::object value = it->second;
        m_data.erase(it);
        return value;
    }

    // Pop an arbitrary item
    py::tuple popitem() {
        if (m_data.empty()) {
            throw py::key_error("popitem(): dictionary is empty");
        }
        auto it = m_data.begin();
        py::tuple result = py::make_tuple(it->first, it->second);
        m_data.erase(it);
        return result;
    }

    // Set default value if key doesn't exist
    py::object setdefault(const std::string &key, py::object default_value = py::none()) {
        auto it = m_data.find(key);
        if (it != m_data.end()) {
            return it->second;
        }
        m_data[key] = default_value;
        return default_value;
    }

    // Update from another dict or mapping
    void update(const py::dict &other) {
        for (auto item : other) {
            m_data[py::str(item.first)] = py::reinterpret_borrow<py::object>(item.second);
        }
    }

    // Update from another GimDict
    void update_from_gimdict(const GimDict &other) {
        for (const auto &pair : other.m_data) {
            m_data[pair.first] = pair.second;
        }
    }

    // In-place OR operator (|=)
    GimDict& ior(const py::dict &other) {
        update(other);
        return *this;
    }

    // Get all keys
    py::list keys() const {
        py::list result;
        for (const auto &pair : m_data) {
            result.append(pair.first);
        }
        return result;
    }

    // Get all values
    py::list values() const {
        py::list result;
        for (const auto &pair : m_data) {
            result.append(pair.second);
        }
        return result;
    }

    // Get all items as tuples
    py::list items() const {
        py::list result;
        for (const auto &pair : m_data) {
            result.append(py::make_tuple(pair.first, pair.second));
        }
        return result;
    }

    // Iterator support - returns keys
    py::iterator iter() const {
        return py::iter(keys());
    }

    // String representation
    std::string repr() const {
        if (m_data.empty()) {
            return "gimdict({})";
        }
        
        std::ostringstream oss;
        oss << "gimdict({";
        bool first = true;
        for (const auto &pair : m_data) {
            if (!first) oss << ", ";
            first = false;
            oss << "'" << pair.first << "': ";
            // Get repr of the value
            oss << py::repr(pair.second).cast<std::string>();
        }
        oss << "})";
        return oss.str();
    }

    // Equality comparison
    bool eq(const GimDict &other) const {
        if (m_data.size() != other.m_data.size()) {
            return false;
        }
        for (const auto &pair : m_data) {
            auto it = other.m_data.find(pair.first);
            if (it == other.m_data.end()) {
                return false;
            }
            if (!pair.second.equal(it->second)) {
                return false;
            }
        }
        return true;
    }

private:
    std::unordered_map<std::string, py::object> m_data;
};
