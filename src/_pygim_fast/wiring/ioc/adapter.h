#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../common/adapter_validation.h"
#include "core.h"

namespace pygim {

namespace py = pybind11;

struct InterfaceKeyPolicy {
    struct key_type {
        PyObject* ptr;
        std::optional<std::string> name;
    };

    struct Hash {
        std::size_t operator()(const key_type& key) const noexcept {
            std::size_t h1 = std::hash<void*>{}(key.ptr);
            std::size_t h2 = key.name ? std::hash<std::string>{}(*key.name) : 0;
            return h1 ^ (h2 << 1);
        }
    };

    struct Eq {
        bool operator()(const key_type& lhs, const key_type& rhs) const noexcept {
            return lhs.ptr == rhs.ptr && lhs.name == rhs.name;
        }
    };

    static key_type make_from_python(const py::object& interface, std::optional<std::string> name) {
        return {interface.ptr(), std::move(name)};
    }
};

inline std::optional<std::string> normalize_name(const py::object& name) {
    if (name.is_none()) {
        return std::nullopt;
    }
    if (!py::isinstance<py::str>(name)) {
        throw py::type_error("name must be str or None");
    }
    return name.cast<std::string>();
}

inline std::string lifecycle_to_string(core::Lifecycle lifecycle) {
    return lifecycle == core::Lifecycle::Singleton ? "singleton" : "transient";
}

inline core::Lifecycle parse_lifecycle(std::string_view lifecycle) {
    if (lifecycle == "transient") {
        return core::Lifecycle::Transient;
    }
    if (lifecycle == "singleton") {
        return core::Lifecycle::Singleton;
    }
    throw std::runtime_error("lifecycle must be 'transient' or 'singleton'");
}

namespace detail {
inline py::tuple to_py_tuple(const InterfaceKeyPolicy::key_type& key) {
    py::handle handle(key.ptr);
    py::object name = key.name ? py::cast(*key.name) : py::none();
    return py::make_tuple(
        py::reinterpret_borrow<py::object>(handle),
        std::move(name));
}

inline void ensure_autowire_provider(const py::object& provider, bool autowire) {
    if (autowire && !py::isinstance<py::type>(provider)) {
        throw py::type_error("autowire=True requires a class provider");
    }
}
} // namespace detail

class Container {
public:
    using DescriptorType = core::ServiceDescriptor<py::object, py::object, py::object>;
    using CoreType = core::ContainerCore<
        InterfaceKeyPolicy::key_type,
        DescriptorType,
        py::object,
        InterfaceKeyPolicy::Hash,
        InterfaceKeyPolicy::Eq>;

    explicit Container(std::size_t capacity = 0)
        : m_core(capacity) {}

    void register_service(
        const py::object& interface,
        const py::object& provider,
        py::object name = py::none(),
        std::string lifecycle = "transient",
        std::vector<py::object> decorators = {},
        bool autowire = false,
        bool override_existing = false) {
        wiring::detail::ensure_callable(provider, "provider");
        wiring::detail::ensure_callables(decorators, "decorator");
        detail::ensure_autowire_provider(provider, autowire);

        auto normalized_name = normalize_name(name);
        auto key = InterfaceKeyPolicy::make_from_python(interface, normalized_name);
        m_core.register_or_override(
            key,
            DescriptorType{
                py::object(interface),
                py::object(provider),
                parse_lifecycle(lifecycle),
                std::move(normalized_name),
                std::move(decorators),
                autowire},
            override_existing);
    }

    [[nodiscard]] py::object resolve(const py::object& key) {
        auto resolved_key = make_key(key);
        return m_core.resolve(
            resolved_key,
            [this](const DescriptorType& descriptor) {
                return invoke_provider(descriptor);
            },
            [](const std::vector<py::object>& decorators, py::object instance, const py::object& interface) {
                for (const auto& decorator : decorators) {
                    instance = decorator(instance);
                }
                wiring::detail::ensure_instance_matches_interface(instance, interface);
                return instance;
            });
    }

    [[nodiscard]] py::object operator[](const py::object& key) {
        return resolve(key);
    }

    [[nodiscard]] bool contains(const py::object& key) const {
        return m_core.contains(make_key(key));
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_core.size();
    }

    [[nodiscard]] py::list registered_keys() const {
        py::list result;
        for (const auto& key : m_core.keys()) {
            result.append(detail::to_py_tuple(key));
        }
        return result;
    }

    [[nodiscard]] DescriptorType describe(const py::object& key) const {
        if (const auto* descriptor = m_core.try_descriptor(make_key(key))) {
            return *descriptor;
        }
        throw std::runtime_error("No provider for key");
    }

    [[nodiscard]] std::string repr() const {
        return "Container(size=" + std::to_string(size()) + ")";
    }

private:
    [[nodiscard]] py::object invoke_provider(const DescriptorType& descriptor) {
        if (!descriptor.autowire) {
            return descriptor.provider();
        }
        return invoke_autowired_class_provider(descriptor.provider);
    }

    [[nodiscard]] py::object invoke_autowired_class_provider(const py::object& provider) {
        py::module_ inspect = py::module_::import("inspect");
        py::module_ typing = py::module_::import("typing");
        py::object empty = inspect.attr("_empty");

        py::object signature;
        try {
            signature = inspect.attr("signature")(provider);
        } catch (const py::error_already_set&) {
            throw std::runtime_error("autowire requires an inspectable class provider");
        }

        py::dict type_hints;
        try {
            type_hints = typing.attr("get_type_hints")(provider.attr("__init__")).cast<py::dict>();
        } catch (const py::error_already_set&) {
            throw std::runtime_error("autowire could not resolve constructor type hints");
        }

        py::dict kwargs;
        py::dict parameters = signature.attr("parameters").cast<py::dict>();

        for (const auto& item : parameters) {
            py::object name_obj = py::reinterpret_borrow<py::object>(item.first);
            py::object parameter = py::reinterpret_borrow<py::object>(item.second);
            std::string name = name_obj.cast<std::string>();
            std::string kind = py::str(parameter.attr("kind")).cast<std::string>();

            if (name == "self" || kind == "VAR_POSITIONAL" || kind == "VAR_KEYWORD") {
                continue;
            }

            if (kind == "POSITIONAL_ONLY") {
                throw std::runtime_error("autowire does not support positional-only parameter '" + name + "'");
            }

            py::object annotation = parameter.attr("annotation");
            if (type_hints.contains(name_obj)) {
                annotation = type_hints[name_obj];
            }

            py::object default_value = parameter.attr("default");
            if (annotation.is(empty)) {
                if (default_value.is(empty)) {
                    throw std::runtime_error("autowire requires a type annotation for parameter '" + name + "'");
                }
                continue;
            }

            if (!contains(annotation)) {
                if (default_value.is(empty)) {
                    throw std::runtime_error("No provider registered for autowired dependency '" + name + "'");
                }
                continue;
            }

            kwargs[name_obj] = resolve(annotation);
        }

        if (kwargs.empty()) {
            return provider();
        }

        py::kwargs keyword_args = py::reinterpret_borrow<py::kwargs>(kwargs);
        return provider(*py::tuple(), **keyword_args);
    }

    static InterfaceKeyPolicy::key_type make_key(const py::object& key) {
        py::object interface = key;
        py::object name = py::none();

        if (py::isinstance<py::tuple>(key)) {
            py::tuple tuple_key = py::reinterpret_borrow<py::tuple>(key);
            if (tuple_key.size() != 2) {
                throw py::type_error("Container key tuple must be (interface, name|None)");
            }
            interface = tuple_key[0];
            name = tuple_key[1];
        }

        if (interface.is_none()) {
            throw py::type_error("interface must be a Python object");
        }

        return InterfaceKeyPolicy::make_from_python(interface, normalize_name(name));
    }

    CoreType m_core;
};

} // namespace pygim