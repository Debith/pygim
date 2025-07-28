
#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <string>
#include <unordered_map>
#include <functional>

#include "utils.h"

// Simple Registry for callables only
class Registry {
public:
    // Register a callable (any py::function)

    // Register a callable (any py::function). If 'override_existing' is false and name exists, throw.
    void register_callable(const std::string& name, pybind11::function func, bool override_existing = false) {
        if (!override_existing && m_callables.count(name) > 0) {
            throw std::runtime_error("Callable already registered: " + name + ". Use override_existing=True to override.");
        }
        m_callables[name] = func;
    }

    // Pythonic: __contains__
    bool contains(const std::string& name) const noexcept {
        return m_callables.count(name) > 0;
    }

    // Pythonic: __len__

    std::size_t size() const noexcept {
        return m_callables.size();
    }


    // Pythonic: __getitem__ (get a callable by name)

    pybind11::function getitem(const std::string& name) const {
        auto it = m_callables.find(name);
        if (it != m_callables.end()) {
            return it->second;
        }
        throw std::runtime_error("Callable not found: " + name
            + ". Available callables: " + to_csv(registered_names(), true));
    }

    // Remove/unregister a callable by name. Returns true if removed, false if not found.
    bool remove(const std::string& name) noexcept {
        return m_callables.erase(name) > 0;
    }

    // Returns a list of all registered callable names
    std::vector<std::string> registered_names() const {
        std::vector<std::string> names;
        names.reserve(m_callables.size());
        for (const auto& pair : m_callables) {
            names.push_back(pair.first);
        }
        return names;
    }

private:
    std::unordered_map<std::string, pybind11::function> m_callables;
};
