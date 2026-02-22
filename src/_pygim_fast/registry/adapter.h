#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <type_traits>
#include <variant>

#include "core.h"

namespace pygim {

namespace py = pybind11;

/*
 * Python-facing key policies used by the registry adapter.
 *
 * - QualnameKeyPolicy: key = (module.qualname, variant)
 * - PyIdentityKeyPolicy: key = (PyObject* identity, variant)
 *
 * Usage example:
 *   auto key = QualnameKeyPolicy::make_from_id("pkg.Type", "default");
 */
struct PyIdentityKeyPolicy {
    struct key_type {
        PyObject* ptr;
        std::string name;
    };

    struct Hash {
        size_t operator()(const key_type& key) const noexcept {
            size_t h1 = std::hash<void*>()(key.ptr);
            size_t h2 = std::hash<std::string>()(key.name);
            return h1 ^ (h2 << 1);
        }
    };

    struct Eq {
        bool operator()(const key_type& lhs, const key_type& rhs) const noexcept {
            return lhs.ptr == rhs.ptr && lhs.name == rhs.name;
        }
    };

    /**
     * \brief Build identity key from Python object and variant.
     * \param[in] obj Python object whose pointer identity is used.
     * \param[in] name Optional variant name.
     * \return Identity key structure.
     * \note Exists to map Python object identity into hashable core key form.
     */
    static key_type make_from_python(py::object obj, std::string name) {
        return {obj.ptr(), std::move(name)};
    }
};

struct QualnameKeyPolicy {
    struct key_type {
        std::string id;
        std::string name;
    };

    struct Hash {
        size_t operator()(const key_type& key) const noexcept {
            size_t h1 = std::hash<std::string>()(key.id);
            size_t h2 = std::hash<std::string>()(key.name);
            return h1 ^ (h2 << 1);
        }
    };

    struct Eq {
        bool operator()(const key_type& lhs, const key_type& rhs) const noexcept {
            return lhs.id == rhs.id && lhs.name == rhs.name;
        }
    };

    /**
     * \brief Build qualname key from Python object metadata.
     * \param[in] obj Python object/type exposing `__module__` and `__qualname__`.
     * \param[in] name Optional variant name.
     * \return Qualname key structure.
     * \note Exists to canonicalize Python entities into stable string identifiers.
     */
    static key_type make_from_python(py::object obj, std::string name) {
        auto module = py::str(obj.attr("__module__")).cast<std::string>();
        auto qualname = py::str(obj.attr("__qualname__")).cast<std::string>();
        return {module + "." + qualname, std::move(name)};
    }

    /**
     * \brief Build qualname key from explicit id string.
     * \param[in] id String identifier (typically module.qualname-like).
     * \param[in] name Optional variant name.
     * \return Qualname key structure.
     * \note Exists to support direct-id registrations without passing Python type objects.
     */
    static key_type make_from_id(std::string id, std::string name) {
        return {std::move(id), std::move(name)};
    }
};

template<class KeyPolicy, class Value, bool EnableHooks = false>
using RegistryT = core::RegistryCore<
    typename KeyPolicy::key_type,
    Value,
    typename KeyPolicy::Hash,
    typename KeyPolicy::Eq,
    std::conditional_t<
        EnableHooks,
        core::HooksBundle<typename KeyPolicy::key_type, Value, py::object>,
        core::NoHooks<typename KeyPolicy::key_type, Value, py::object>>,
    py::object>;

enum class KeyPolicyKind { Qualname = 0, Identity = 1 };

namespace detail {
inline py::object to_py_tuple(const QualnameKeyPolicy::key_type& key) {
    return py::make_tuple(py::str(key.id), py::str(key.name));
}

inline py::object to_py_tuple(const PyIdentityKeyPolicy::key_type& key) {
    py::handle h(key.ptr);
    return py::make_tuple(py::reinterpret_borrow<py::object>(h), py::str(key.name));
}
} // namespace detail

/*
 * Registry is the thin pybind adapter over pybind-free RegistryCore.
 * It owns Python key translation and callback bridging, while core owns
 * storage/override semantics.
 *
 * Usage example:
 *   Registry r(false, KeyPolicyKind::Qualname);
 *   r.register_or_override(py::str("pkg.Type"), py::int_(1), false);
 *   auto value = r.get(py::str("pkg.Type"));
 */
class Registry {
    using Value = py::object;
    using R_QN_No = RegistryT<QualnameKeyPolicy, Value, false>;
    using R_QN_Yes = RegistryT<QualnameKeyPolicy, Value, true>;
    using R_ID_No = RegistryT<PyIdentityKeyPolicy, Value, false>;
    using R_ID_Yes = RegistryT<PyIdentityKeyPolicy, Value, true>;

    std::variant<R_QN_No, R_QN_Yes, R_ID_No, R_ID_Yes> m_var;
    KeyPolicyKind m_policy;
    bool m_hooks;

    /**
     * \brief Normalize Python-facing key input to policy key type.
     * \tparam R Concrete registry instantiation selected by variant.
     * \param[in] key Python key in object form or `(thing_or_id, name)` tuple form.
     * \return Concrete policy key.
     * \throws py::type_error On malformed tuple, invalid name type, or policy-key mismatch.
     * \note Exists to centralize Python parsing rules and keep core key handling pure C++.
     */
    template<class R>
    static typename R::key_type make_key(py::object key) {
        py::object first;
        py::object second;

        if (py::isinstance<py::tuple>(key)) {
            if (py::len(key) != 2) {
                throw py::type_error("Registry key tuple must be (thing_or_id, name|None)");
            }
            py::tuple tuple_key = py::reinterpret_borrow<py::tuple>(key);
            first = tuple_key[0];
            second = tuple_key[1];
        } else {
            first = key;
            second = py::none();
        }

        std::string name;
        if (!second.is_none()) {
            if (!py::isinstance<py::str>(second)) {
                throw py::type_error("name must be str or None");
            }
            name = second.cast<std::string>();
        }

        using key_type = typename R::key_type;
        if constexpr (std::is_same_v<key_type, QualnameKeyPolicy::key_type>) {
            if (py::isinstance<py::str>(first)) {
                return QualnameKeyPolicy::make_from_id(first.cast<std::string>(), std::move(name));
            }
            return QualnameKeyPolicy::make_from_python(first, std::move(name));
        } else {
            if (py::isinstance<py::str>(first)) {
                throw py::type_error("Identity policy requires a Python object as first element");
            }
            return PyIdentityKeyPolicy::make_from_python(first, std::move(name));
        }
    }

public:
    /**
     * \brief Construct registry adapter over selected policy/hook mode.
     * \param[in] hooks Enable hook-capable core variant when true.
     * \param[in] policy Key policy kind (`qualname` or `identity`).
     * \param[in] capacity Optional reserve size for underlying map.
     * \note Exists to keep Python API simple while selecting compile-time-specialized cores.
     */
    Registry(bool hooks = false, KeyPolicyKind policy = KeyPolicyKind::Qualname, std::size_t capacity = 0)
        : m_policy(policy), m_hooks(hooks) {
        if (policy == KeyPolicyKind::Qualname) {
            m_var = hooks ? std::variant<R_QN_No, R_QN_Yes, R_ID_No, R_ID_Yes>(R_QN_Yes{})
                          : std::variant<R_QN_No, R_QN_Yes, R_ID_No, R_ID_Yes>(R_QN_No{});
        } else {
            m_var = hooks ? std::variant<R_QN_No, R_QN_Yes, R_ID_No, R_ID_Yes>(R_ID_Yes{})
                          : std::variant<R_QN_No, R_QN_Yes, R_ID_No, R_ID_Yes>(R_ID_No{});
        }

        if (capacity) {
            std::visit([&](auto& registry) { registry.reserve(capacity); }, m_var);
        }
    }

    /**
     * \brief Return entry count.
     * \return Number of registered key/value pairs.
     */
    size_t size() const {
        return std::visit([](const auto& registry) { return registry.size(); }, m_var);
    }

    /**
     * \brief Lookup value by key.
     * \param[in] key Python-facing key object or tuple form.
     * \return Stored Python object.
     * \throws std::runtime_error If key is missing.
     * \note Exists as backing implementation for Python `__getitem__`.
     */
    py::object get(py::object key) {
        return std::visit(
            [&](auto& registry) -> py::object {
                auto concrete_key = make_key<std::decay_t<decltype(registry)>>(key);
                if (auto* value = registry.try_get(concrete_key)) {
                    return *value;
                }
                throw std::runtime_error("Key not found in Registry");
            },
            m_var);
    }

    /**
     * \brief Check key presence under active policy.
     * \param[in] key Python-facing key.
     * \return `true` when found; otherwise `false`.
     */
    bool contains(py::object key) const {
        return std::visit(
            [&](const auto& registry) -> bool {
                auto concrete_key = make_key<std::decay_t<decltype(registry)>>(key);
                return registry.contains(concrete_key);
            },
            m_var);
    }

    /**
     * \brief Register or override value for key using strict semantics.
     * \param[in] key Python-facing key.
     * \param[in] value Value to store.
     * \param[in] override_existing `false` forbids duplicates; `true` requires existing key.
     * \return void.
     * \throws std::runtime_error On invalid override state.
     * \note Exists to enforce deterministic semantics expected by Python API/tests.
     */
    void register_or_override(py::object key, py::object value, bool override_existing) {
        std::visit(
            [&](auto& registry) {
                auto concrete_key = make_key<std::decay_t<decltype(registry)>>(key);
                registry.register_or_override(concrete_key, std::move(value), override_existing);
            },
            m_var);
    }

    /**
     * \brief Build stable representation string.
     * \return Representation in `Registry(policy=..., hooks=..., size=...)` format.
     * \note Exists for debugging and representation-contract tests.
     */
    std::string repr() const {
        std::string policy_str = (m_policy == KeyPolicyKind::Qualname) ? "qualname" : "identity";
        return "Registry(policy=" + policy_str + ", hooks=" + (m_hooks ? "True" : "False") + ", size=" + std::to_string(size()) + ")";
    }

    /**
     * \brief Export currently registered keys.
     * \return Python list of `(id_or_object, name)` tuples.
     * \note Exists to support introspection and test assertions.
     */
    py::list registered_keys() const {
        py::list out;
        std::visit(
            [&](const auto& registry) {
                for (const auto& key : registry.keys()) {
                    out.append(detail::to_py_tuple(key));
                }
            },
            m_var);
        return out;
    }

    /**
     * \brief Fast lookup by string id for qualname policy.
     * \param[in] id String identifier.
     * \param[in] name Optional variant name.
     * \return Stored object or `None` when not found.
     * \throws std::runtime_error If active policy is not qualname.
     * \note Exists to avoid tuple/object key conversion overhead for id lookups.
     */
    py::object find_id(const std::string& id, const py::object& name = py::none()) {
        if (m_policy != KeyPolicyKind::Qualname) {
            throw std::runtime_error("find_id only valid for qualname policy");
        }

        std::string variant_name;
        if (!name.is_none()) {
            variant_name = py::cast<std::string>(name);
        }

        return std::visit(
            [&](auto& registry) -> py::object {
                using R = std::decay_t<decltype(registry)>;
                using key_type = typename R::key_type;

                if constexpr (std::is_same_v<key_type, QualnameKeyPolicy::key_type>) {
                    key_type direct{id, variant_name};
                    if (auto* value = registry.try_get(direct)) {
                        return *value;
                    }
                    if (variant_name.empty()) {
                        return py::none();
                    }
                    key_type fallback{id, std::string{}};
                    if (auto* value = registry.try_get(fallback)) {
                        return *value;
                    }
                }
                return py::none();
            },
            m_var);
    }

    /**
     * \brief Trigger post hooks for key with payload.
     * \param[in] key Python-facing key.
     * \param[in] value Payload forwarded to post hooks.
     * \return void.
     * \note Exists to allow explicit post events from adapter callers.
     */
    void post(py::object key, const py::object& value) {
        std::visit(
            [&](auto& registry) {
                auto concrete_key = make_key<std::decay_t<decltype(registry)>>(key);
                registry.post(concrete_key, value);
            },
            m_var);
    }

    /**
     * \brief Register callback for register-phase events.
     * \param[in] callback Python callable receiving `(key_tuple, value)`.
     * \return void.
     * \note Exists to expose hook extension points through Python API.
     */
    void on_register(py::function callback) {
        std::visit(
            [&](auto& registry) {
                registry.add_on_register([callback](const auto& key, const auto& value) { callback(detail::to_py_tuple(key), value); });
            },
            m_var);
    }

    /**
     * \brief Register callback for pre-access events.
     * \param[in] callback Python callable receiving `(key_tuple, value)`.
     * \return void.
     * \note Exists for Python-side instrumentation and lazy mutation hooks.
     */
    void on_pre(py::function callback) {
        std::visit(
            [&](auto& registry) {
                registry.add_on_pre([callback](const auto& key, auto& value) { callback(detail::to_py_tuple(key), value); });
            },
            m_var);
    }

    /**
     * \brief Register callback for post events.
     * \param[in] callback Python callable receiving `(key_tuple, payload)`.
     * \return void.
     * \note Exists to expose explicit post-event notifications.
     */
    void on_post(py::function callback) {
        std::visit(
            [&](auto& registry) {
                registry.add_on_post([callback](const auto& key, const py::object& value) { callback(detail::to_py_tuple(key), value); });
            },
            m_var);
    }
};

} // namespace pygim
