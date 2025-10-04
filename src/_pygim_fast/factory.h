#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <optional>
#include "registry.h"

namespace pygim {

struct StringKeyPolicy {
    using key_type = std::string;
    struct Hash { size_t operator()(const key_type& k) const noexcept {
        return std::hash<std::string>{}(k);
    }};
    struct Eq { bool operator()(const key_type& a, const key_type& b) const noexcept {
        return a == b;
    }};
};



// Factory for creating objects using registered callables in Registry
class Factory {
    using CreatorRegistry = RegistryT<StringKeyPolicy, pybind11::function, /*EnableHooks=*/false>;

public:
    Factory(std::optional<pybind11::object> interface = std::nullopt)
        : m_interface(interface) {}

    // Register a creator function
    void register_creator(const std::string& name, pybind11::function func, bool override=false) {
        if (override) {
            if (!m_registry.contains(name)) {
                throw std::runtime_error("Cannot override non-existent creator: " + name);
            }
            m_registry.upsert_value(name, std::move(func));
        } else {
            if (m_registry.contains(name)) {
                throw std::runtime_error("Creator already registered: " + name);
            }
            m_registry.register_value(name, std::move(func));
        }
    }

    pybind11::function getitem(const std::string& name) const {
        if (auto* f = m_registry.try_get(name)) return *f;
        throw std::runtime_error("Unknown creator: " + name);
    }

    pybind11::object create(const std::string& name,
                            pybind11::args args,
                            pybind11::kwargs kwargs) const {
        auto creator = getitem(name);
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
        return m_registry.keys();     // key_type == std::string here
    }

    // Import a Python module by name to trigger registration side effects
    void use_module(const std::string& module_name) {
        pybind11::module_::import(module_name.c_str());
    }

private:
    CreatorRegistry m_registry;
    std::optional<pybind11::object> m_interface;
};

} // namespace pygim
