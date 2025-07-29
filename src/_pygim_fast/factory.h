#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <optional>
#include "registry.h"

// Factory for creating objects using registered callables in Registry
class Factory {
public:
    Factory(std::optional<pybind11::object> interface = std::nullopt)
        : m_interface(interface) {}

    // Register a creator function
    void register_creator(const std::string& name, pybind11::function func, bool override = false) {
        m_registry.register_callable(name, func, override);
    }

    pybind11::function getitem(const std::string& name) const {
        return m_registry.getitem(name);
    }

    // Create an object by name, passing args/kwargs to the creator
    pybind11::object create(const std::string& name,
                            pybind11::args args,
                            pybind11::kwargs kwargs) const {
        auto creator = m_registry.getitem(name);
        pybind11::object obj = creator(*args, **kwargs);
        if (m_interface) {
            // Check if obj implements the interface/protocol
            if (!pybind11::isinstance(obj, *m_interface)) {
                throw std::runtime_error("Created object does not implement required interface/protocol");
            }
        }
        return obj;
    }

    // Expose registered_callables from the underlying registry
    std::vector<std::string> registered_callables() const {
        return m_registry.registered_names();
    }

    // Import a Python module by name to trigger registration side effects
    void use_module(const std::string& module_name) {
        pybind11::module_::import(module_name.c_str());
    }

private:
    Registry m_registry;
    std::optional<pybind11::object> m_interface;
};
