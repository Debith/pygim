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
using RegistryT = core::DynamicRegistryCore<
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
            std::visit([&](auto& reg) { reg.reserve(capacity); }, m_var);
        }
    }

    void register_or_override(py::object key, py::object value, bool override_existing) {
        std::visit(
            [&](auto& reg) {
                using R = std::decay_t<decltype(reg)>;
                reg.register_or_override(make_key<R>(key), std::move(value), override_existing);
            },
            m_var);
    }

    [[nodiscard]] py::object get(py::object key) {
        return std::visit(
            [&](auto& reg) -> py::object {
                using R = std::decay_t<decltype(reg)>;
                if (auto* found = reg.try_get(make_key<R>(key))) {
                    return *found;
                }
                throw std::runtime_error("Unknown registry key");
            },
            m_var);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return std::visit([](const auto& reg) { return reg.size(); }, m_var);
    }

    [[nodiscard]] bool contains(py::object key) const {
        return std::visit(
            [&](const auto& reg) {
                using R = std::decay_t<decltype(reg)>;
                return reg.contains(make_key<R>(key));
            },
            m_var);
    }

    void post(py::object key, py::object value) {
        std::visit(
            [&](auto& reg) {
                using R = std::decay_t<decltype(reg)>;
                reg.post(make_key<R>(key), value);
            },
            m_var);
    }

    void on_register(std::function<void(py::object, py::object)> fn) {
        std::visit(
            [&](auto& reg) {
                reg.add_on_register([fn = std::move(fn)](const auto& key, const auto& value) {
                    fn(detail::to_py_tuple(key), value);
                });
            },
            m_var);
    }

    void on_pre(std::function<void(py::object, py::object)> fn) {
        std::visit(
            [&](auto& reg) {
                reg.add_on_pre([fn = std::move(fn)](const auto& key, auto& value) {
                    fn(detail::to_py_tuple(key), value);
                });
            },
            m_var);
    }

    void on_post(std::function<void(py::object, py::object)> fn) {
        std::visit(
            [&](auto& reg) {
                reg.add_on_post([fn = std::move(fn)](const auto& key, const py::object& payload) {
                    fn(detail::to_py_tuple(key), payload);
                });
            },
            m_var);
    }

    [[nodiscard]] py::list registered_keys() const {
        py::list result;
        std::visit(
            [&](const auto& reg) {
                for (const auto& key : reg.keys()) {
                    result.append(detail::to_py_tuple(key));
                }
            },
            m_var);
        return result;
    }

    [[nodiscard]] py::object find_id(py::object id, py::object name = py::none()) const {
        if (m_policy != KeyPolicyKind::Qualname) {
            throw std::runtime_error("find_id is only available for qualname policy");
        }
        if (!py::isinstance<py::str>(id)) {
            return py::none();
        }

        std::string id_value = id.cast<std::string>();
        std::string variant_name;
        if (!name.is_none()) {
            if (!py::isinstance<py::str>(name)) {
                throw py::type_error("name must be str or None");
            }
            variant_name = name.cast<std::string>();
        }

        QualnameKeyPolicy::key_type key = QualnameKeyPolicy::make_from_id(id_value, variant_name);
        return std::visit(
            [&](const auto& reg) -> py::object {
                using R = std::decay_t<decltype(reg)>;
                if constexpr (std::is_same_v<typename R::key_type, QualnameKeyPolicy::key_type>) {
                    if (auto* found = reg.try_get_const(key)) {
                        return *found;
                    }
                    if (!variant_name.empty()) {
                        auto fallback_key = QualnameKeyPolicy::make_from_id(id_value, "");
                        if (auto* fallback = reg.try_get_const(fallback_key)) {
                            return *fallback;
                        }
                    }
                }
                return py::none();
            },
            m_var);
    }

    [[nodiscard]] std::string repr() const {
        return "Registry(policy=" + std::string(m_policy == KeyPolicyKind::Qualname ? "qualname" : "identity") +
               ", hooks=" + std::string(m_hooks ? "True" : "False") +
               ", size=" + std::to_string(size()) + ")";
    }
};

} // namespace pygim