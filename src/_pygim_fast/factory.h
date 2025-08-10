#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <optional>
#include "registry.h"

namespace py = pybind11;

namespace pygim {

class Factory {
public:
    using Policy = QualnameKeyPolicy;
    using Key    = typename Policy::key_type;
    using Value  = py::object; // callable or registration object
    using Reg    = RegistryT<Policy, Value, false>; // non-hooked

    explicit Factory(std::optional<py::object> interface = std::nullopt)
        : m_interface(std::move(interface)) {}

    // Register using the factory's interface; name is optional (variant key)
    void register_creator(py::object provider,
                          std::optional<std::string> name = std::nullopt,
                          bool override_existing = false) {
        ensure_interface();
        auto key = Policy::make_from_python(*m_interface, name);
        if (!override_existing && m_registry.contains(key)) {
            throw std::runtime_error("Creator already registered for key (interface + name)");
        }
        if (override_existing) m_registry.upsert_value(key, std::move(provider));
        else                   m_registry.register_value(key, std::move(provider));
        if (name) m_names.insert(*name);
    }

    // Register explicitly specifying interface (ignores m_interface)
    void register_creator_for(py::object interface,
                              py::object provider,
                              std::optional<std::string> name = std::nullopt,
                              bool override_existing = false) {
        auto key = Policy::make_from_python(interface, name);
        if (!override_existing && m_registry.contains(key)) {
            throw std::runtime_error("Creator already registered for key (interface + name)");
        }
        if (override_existing) m_registry.upsert_value(key, std::move(provider));
        else                   m_registry.register_value(key, std::move(provider));
        if (m_interface && interface.is(*m_interface)) {
            if (name) m_names.insert(*name);
        }
    }

    // __getitem__-like access (by name, using this factory's interface)
    py::object getitem(const std::string& name) const {
        ensure_interface();
        auto key = Policy::make_from_python(*m_interface, name);
        if (auto* v = m_registry.try_get(key)) return *v;
        throw std::runtime_error("Creator not found for given name");
    }

    // Create an object by name, forwarding args/kwargs to the provider
    py::object create(const std::string& name, py::args args, py::kwargs kwargs) const {
        auto creator = getitem(name);
        py::object obj = creator(*args, **kwargs);
        if (m_interface && !py::isinstance(obj, *m_interface)) {
            throw std::runtime_error("Created object does not implement required interface/protocol");
        }
        return obj;
    }

    // Optional: list names registered for this factory's interface (best-effort)
    std::vector<std::string> registered_callables() const {
        return std::vector<std::string>(m_names.begin(), m_names.end());
    }

    // Import a Python module by name to trigger registration side effects
    void use_module(const std::string& module_name) {
        py::module_::import(module_name.c_str());
    }

private:
    void ensure_interface() const {
        if (!m_interface) throw std::runtime_error("Factory requires an interface to be set");
    }

    Reg m_registry;
    std::optional<py::object> m_interface;
    std::unordered_set<std::string> m_names; // tracked locally for convenience
};

} // namespace pygim
