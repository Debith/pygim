#pragma once

#include <memory>
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

inline core::ParamKind classify_param_kind(std::string_view kind) {
    if (kind == "POSITIONAL_ONLY") {
        return core::ParamKind::PositionalOnly;
    }
    if (kind == "VAR_POSITIONAL" || kind == "VAR_KEYWORD") {
        return core::ParamKind::Variadic;
    }
    if (kind == "KEYWORD_ONLY") {
        return core::ParamKind::KeywordOnly;
    }
    return core::ParamKind::PositionalOrKeyword;
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
    using ParamSpecs = std::vector<core::ParamSpec<py::object>>;

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
        try {
            m_core.register_or_override(
                key,
                DescriptorType{
                    py::object(interface),
                    py::object(provider),
                    core::parse_lifecycle(lifecycle),
                    std::move(normalized_name),
                    std::move(decorators),
                    autowire},
                override_existing);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error(std::string(error.what()) + key_suffix(key));
        }
    }

    [[nodiscard]] py::object resolve(const py::object& key) {
        return resolve_key(make_key(key));
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
        auto resolved_key = make_key(key);
        if (const auto* descriptor = m_core.try_descriptor(resolved_key)) {
            return *descriptor;
        }
        throw std::runtime_error("No provider for key" + key_suffix(resolved_key));
    }

    [[nodiscard]] std::string repr() const {
        return "Container(size=" + std::to_string(size()) + ")";
    }

private:
    [[nodiscard]] py::object resolve_key(const InterfaceKeyPolicy::key_type& key) {
        try {
            return m_core.resolve(
                key,
                [this](const DescriptorType& descriptor) { return invoke_provider(descriptor); },
                [](const py::object& decorator, py::object instance) {
                    return decorator(std::move(instance));
                },
                [](const py::object& instance, const py::object& interface) {
                    wiring::detail::ensure_instance_matches_interface(instance, interface);
                });
        } catch (const py::error_already_set&) {
            throw;  // live Python exceptions pass through untouched
        } catch (const py::builtin_exception&) {
            throw;  // pybind-raised Python exceptions keep their type
        } catch (const std::runtime_error& error) {
            // Append this frame's key; nested resolves have already appended
            // theirs, so the message reads as the resolution chain.
            throw std::runtime_error(std::string(error.what()) + key_suffix(key));
        }
    }

    [[nodiscard]] py::object invoke_provider(const DescriptorType& descriptor) {
        if (!descriptor.autowire) {
            return descriptor.provider();
        }
        return invoke_autowired(descriptor);
    }

    [[nodiscard]] py::object invoke_autowired(const DescriptorType& descriptor) {
        std::shared_ptr<const ParamSpecs> specs;
        if (descriptor.autowire_slot) {
            specs = descriptor.autowire_slot->params;
        }
        if (!specs) {
            specs = introspect_constructor(descriptor.provider);
            if (descriptor.autowire_slot) {
                descriptor.autowire_slot->params = specs;
            }
        }

        auto injections = core::plan_autowiring(*specs, [this](const py::object& annotation) {
            return m_core.contains(InterfaceKeyPolicy::make_from_python(annotation, std::nullopt));
        });
        if (injections.empty()) {
            return descriptor.provider();
        }

        py::kwargs kwargs;
        for (const auto& injection : injections) {
            kwargs[py::str(injection.name)] =
                resolve_key(InterfaceKeyPolicy::make_from_python(injection.annotation, std::nullopt));
        }
        return descriptor.provider(**kwargs);
    }

    // Reduce a class provider's constructor to neutral ParamSpec records.
    // All Python reflection lives here; the wiring decisions live in
    // core::plan_autowiring.
    [[nodiscard]] static std::shared_ptr<const ParamSpecs> introspect_constructor(const py::object& provider) {
        py::module_ inspect = py::module_::import("inspect");
        py::object empty = inspect.attr("Parameter").attr("empty");

        py::object signature;
        try {
            signature = inspect.attr("signature")(provider);
        } catch (const py::error_already_set&) {
            throw std::runtime_error("autowire requires an inspectable class provider");
        }

        py::dict type_hints;
        try {
            py::module_ typing = py::module_::import("typing");
            type_hints = typing.attr("get_type_hints")(provider.attr("__init__")).cast<py::dict>();
        } catch (const py::error_already_set&) {
            throw std::runtime_error("autowire could not resolve constructor type hints");
        }

        auto specs = std::make_shared<ParamSpecs>();
        py::dict parameters = signature.attr("parameters").cast<py::dict>();

        for (const auto& item : parameters) {
            py::object name_obj = py::reinterpret_borrow<py::object>(item.first);
            py::object parameter = py::reinterpret_borrow<py::object>(item.second);
            std::string name = name_obj.cast<std::string>();
            if (name == "self") {
                continue;
            }

            core::ParamSpec<py::object> spec;
            spec.name = std::move(name);
            spec.kind = detail::classify_param_kind(
                py::str(parameter.attr("kind")).cast<std::string>());

            py::object annotation = parameter.attr("annotation");
            if (type_hints.contains(name_obj)) {
                annotation = type_hints[name_obj];
            }
            if (!annotation.is(empty)) {
                spec.annotation = std::move(annotation);
            }
            py::object default_value = parameter.attr("default");
            spec.has_default = !default_value.is(empty);

            specs->push_back(std::move(spec));
        }
        return specs;
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

    [[nodiscard]] static std::string key_suffix(const InterfaceKeyPolicy::key_type& key) {
        std::string label;
        try {
            py::handle interface(key.ptr);
            py::object qualname = py::getattr(interface, "__qualname__", py::none());
            label = qualname.is_none()
                ? py::repr(interface).cast<std::string>()
                : py::str(qualname).cast<std::string>();
        } catch (py::error_already_set& error) {
            error.discard_as_unraisable("pygim.ioc key repr");
            label = "<unprintable>";
        }
        if (key.name) {
            label += ", name='" + *key.name + "'";
        }
        return " [key: " + label + "]";
    }

    CoreType m_core;
};

} // namespace pygim
