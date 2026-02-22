#pragma once

#include <optional>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "core.h"

namespace pygim {

namespace py = pybind11;

/*
 * PyObjectValidator is a Python-bound validator policy.
 *
 * - If interface is empty, validation is effectively disabled.
 * - If interface is set, created objects must satisfy isinstance(obj, interface).
 *
 * Usage example:
 *   PyObjectValidator v{py::none()};
 *   bool ok = v(py::str("value"));
 */
struct PyObjectValidator {
    std::optional<py::object> interface;

    /**
     * \brief Validate a created Python object against optional interface/protocol.
     * \param[in] obj Produced Python object.
     * \return `true` when interface is unset or `isinstance(obj, interface)` is true.
     * \note Exists to keep Python protocol checks out of pybind-free core code.
     */
    bool operator()(const py::object& obj) const {
        if (!interface) {
            return true;
        }
        return py::isinstance(obj, *interface);
    }
};

/*
 * Factory is the thin pybind adapter over FactoryCore.
 * It delegates creator storage and override semantics to core while
 * handling Python args/kwargs invocation and module imports.
 *
 * Usage example:
 *   Factory f;
 *   f.register_creator("double", py::cpp_function([](int x){ return py::int_(x * 2); }));
 *   auto out = f.create("double", py::make_tuple(3), py::dict());
 */
class Factory {
    using CoreType = core::FactoryCore<std::string, py::object, py::function, PyObjectValidator>;

public:
    /**
     * \brief Construct Python-facing factory adapter.
     * \param[in] interface Optional Python type/protocol for output validation.
     * \note Exists to expose runtime Python constraints while delegating logic to `FactoryCore`.
     */
    Factory(std::optional<py::object> interface = std::nullopt)
        : m_core(PyObjectValidator{std::move(interface)}) {}

    /**
     * \brief Register a Python callable creator.
     * \param[in] name Registry key.
     * \param[in] func Python callable used as creator.
     * \param[in] override_existing When `false`, duplicate keys throw; when `true`, missing keys throw.
     * \return void.
     * \throws std::runtime_error On invalid override state.
     * \note Uses `py::function` intentionally because this class is the pybind adapter boundary.
     *       For pybind-independent templating, use `core::FactoryCore<Key, Product, Creator, Validator>`.
     */
    void register_creator(const std::string& name, py::function func, bool override_existing = false) {
        m_core.register_creator(name, std::move(func), override_existing);
    }

    /**
     * \brief Retrieve a registered Python creator by name.
     * \param[in] name Registry key.
     * \return Stored Python callable.
     * \throws std::runtime_error If key is not registered.
     * \note Exists to support direct callable inspection/access from Python API.
     */
    py::function getitem(const std::string& name) const {
        return m_core.get_creator(name);
    }

    /**
     * \brief Invoke a creator with Python call arguments.
     * \param[in] name Registry key.
     * \param[in] args Positional arguments forwarded to creator.
     * \param[in] kwargs Keyword arguments forwarded to creator.
     * \return Created Python object.
     * \throws std::runtime_error If creator is missing or validation fails.
     * \note Exists to isolate `args/kwargs` forwarding mechanics in adapter layer.
     */
    py::object create(const std::string& name, py::args args, py::kwargs kwargs) const {
        return m_core.create(name, [&](py::function& creator) {
            return creator(*args, **kwargs);
        });
    }

    /**
     * \brief List names of all registered creators.
     * \return Vector of creator names.
     * \note Exists for API introspection and test assertions.
     */
    std::vector<std::string> registered_callables() const {
        return m_core.registered_names();
    }

    /**
     * \brief Import module to trigger side-effect registrations.
     * \param[in] module_name Import path string.
     * \return void.
     * \throws pybind11::error_already_set If import fails.
     * \note Exists to support registration-by-import patterns in Python codebases.
     */
    void use_module(const std::string& module_name) {
        py::module_::import(module_name.c_str());
    }

private:
    CoreType m_core;
};

} // namespace pygim
