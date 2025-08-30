#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <functional>
#include <type_traits>
#include <stdexcept>
#include <utility>

namespace pygim {
namespace py = pybind11;

// ─────────────────────────────── Key Policies ────────────────────────────────
// Each policy defines: key_type, Hash, Eq, and builders from Python inputs.

struct PyIdentityKeyPolicy {
    struct key_type {
        PyObject*   ptr;   // identity of the Python object
        std::string name;  // required variant name (use e.g. "default")
    };

    struct Hash {
        size_t operator()(key_type const& k) const noexcept {
            size_t h1 = std::hash<void*>()(k.ptr);
            size_t h2 = std::hash<std::string>()(k.name);
            return h1 ^ (h2 << 1);
        }
    };
    struct Eq {
        bool operator()(key_type const& a, key_type const& b) const noexcept {
            return a.ptr == b.ptr && a.name == b.name;
        }
    };

    static key_type make_from_python(py::object t, std::string n) {
        return { t.ptr(), std::move(n) };
    }
};

struct QualnameKeyPolicy {
    struct key_type {
        std::string id;    // "module.qualname"
        std::string name;  // required variant name
    };

    struct Hash {
        size_t operator()(key_type const& k) const noexcept {
            size_t h1 = std::hash<std::string>()(k.id);
            size_t h2 = std::hash<std::string>()(k.name);
            return h1 ^ (h2 << 1);
        }
    };
    struct Eq {
        bool operator()(key_type const& a, key_type const& b) const noexcept {
            return a.id == b.id && a.name == b.name;
        }
    };

    static key_type make_from_python(py::object t, std::string n) {
        auto mod = py::str(t.attr("__module__")).cast<std::string>();
        auto qn  = py::str(t.attr("__qualname__")).cast<std::string>();
        return { mod + "." + qn, std::move(n) };
    }
    static key_type make_from_id(std::string id, std::string n) {
        return { std::move(id), std::move(n) };
    }
};

// ───────────────────────────── Hooks plumbing ────────────────────────────────
// Compile-time on/off. When disabled, methods are no-ops and const-correctness
// of lookups is preserved.

template<class K, class V, bool Enable>
struct Hooks;

template<class K, class V>
struct Hooks<K,V,true> {
    using OnRegister = std::function<void(const K&, const V&)>;
    using OnPre      = std::function<void(const K&, V&)>;
    using OnPost     = std::function<void(const K&, const py::object&)>;

    std::vector<OnRegister> on_register;
    std::vector<OnPre>      on_pre;
    std::vector<OnPost>     on_post;

    void run_register(const K& k, const V& v)                { for (auto& f : on_register) f(k, v); }
    void run_pre(const K& k, V& v)                           { for (auto& f : on_pre)      f(k, v); }
    void run_post(const K& k, const py::object& o)           { for (auto& f : on_post)     f(k, o); }

    void add_on_register(OnRegister f) { on_register.push_back(std::move(f)); }
    void add_on_pre(OnPre f)           { on_pre.push_back(std::move(f)); }
    void add_on_post(OnPost f)         { on_post.push_back(std::move(f)); }
};

template<class K, class V>
struct Hooks<K,V,false> {
    template<class...Args> void run_register(Args&&...) {}
    template<class...Args> void run_pre(Args&&...) {}
    template<class...Args> void run_post(Args&&...) {}

    template<class F> void add_on_register(F&&) {}
    template<class F> void add_on_pre(F&&) {}
    template<class F> void add_on_post(F&&) {}
};

// ───────────────────────────── Generic Registry ──────────────────────────────

template<class KeyPolicy, class Value, bool EnableHooks=false>
class RegistryT {
public:
    using Policy   = KeyPolicy;
    using key_type = typename KeyPolicy::key_type;
    using Hash     = typename KeyPolicy::Hash;
    using Eq       = typename KeyPolicy::Eq;

    void register_value(const key_type& k, Value v) {
        m_hooks.run_register(k, v);
        m_map.emplace(k, std::move(v));
    }

    void upsert_value(const key_type& k, Value v) {
        m_hooks.run_register(k, v);
        m_map.insert_or_assign(k, std::move(v));
    }

    bool contains(const key_type& k) const {
        return m_map.find(k) != m_map.end();
    }

    // Non-const lookup (runs pre-hooks when enabled)
    Value* try_get(const key_type& k) {
        auto it = m_map.find(k);
        if (it == m_map.end()) return nullptr;
        m_hooks.run_pre(k, it->second);
        return &it->second;
    }

    // Const lookup only exists when hooks are disabled.
    const Value* try_get(const key_type& k) const requires(!EnableHooks) {
        auto it = m_map.find(k);
        if (it == m_map.end()) return nullptr;
        return &it->second;
    }

    void post(const key_type& k, const py::object& o) {
        m_hooks.run_post(k, o);
    }

    size_t size() const noexcept { return m_map.size(); }

    // Inside RegistryT (public:)
    std::vector<key_type> keys() const {
        std::vector<key_type> out;
        out.reserve(m_map.size());
        for (auto const& kv : m_map) out.push_back(kv.first);
        return out;
    }

    template<class F>
    void for_each_key(F&& f) const {
        for (auto const& kv : m_map) f(kv.first);
    }

    // Hook API (present in both modes; meaningful only if EnableHooks=true)
    void add_on_register(std::function<void(const key_type&, const Value&)> f) { m_hooks.add_on_register(std::move(f)); }
    void add_on_pre(std::function<void(const key_type&, Value&)> f)            { m_hooks.add_on_pre(std::move(f)); }
    void add_on_post(std::function<void(const key_type&, const py::object&)> f){ m_hooks.add_on_post(std::move(f)); }

private:
    std::unordered_map<key_type, Value, Hash, Eq> m_map;
    [[no_unique_address]] Hooks<key_type, Value, EnableHooks> m_hooks;
};

// ───────────────────────────── Python-facing wrapper ─────────────────────────

enum class KeyPolicyKind { Qualname = 0, Identity = 1 };

namespace detail {
// Convert internal C++ key structs back to a Python 2-tuple for hook callbacks.
inline py::object to_py_tuple(const QualnameKeyPolicy::key_type& k) {
    return py::make_tuple(py::str(k.id), py::str(k.name));
}
inline py::object to_py_tuple(const PyIdentityKeyPolicy::key_type& k) {
    py::handle h(k.ptr);
    return py::make_tuple(py::reinterpret_borrow<py::object>(h), py::str(k.name));
}
} // namespace detail

class Registry {
    using Value = py::object;
    using R_QN_No  = RegistryT<QualnameKeyPolicy, Value, false>;
    using R_QN_Yes = RegistryT<QualnameKeyPolicy, Value, true>;
    using R_ID_No  = RegistryT<PyIdentityKeyPolicy, Value, false>;
    using R_ID_Yes = RegistryT<PyIdentityKeyPolicy, Value, true>;

    std::variant<R_QN_No, R_QN_Yes, R_ID_No, R_ID_Yes> m_var;
    KeyPolicyKind m_policy;
    bool m_hooks;

    // Build a concrete policy key from a single Python key: (thing_or_id, name:str)
    template<class R>
    static typename R::Policy::key_type make_key(pybind11::object key) {
        namespace py = pybind11;
        using Policy = typename R::Policy;

        py::object first, second;

        if (py::isinstance<py::tuple>(key)) {
            if (py::len(key) != 2)
                throw py::type_error("Registry key tuple must be (thing_or_id, name|None)");
            py::tuple t = py::reinterpret_borrow<py::tuple>(key);
            first  = t[0];
            second = t[1];
        } else {
            // Accept single object: interpret as (object, None)
            first  = key;
            second = py::none();
        }

        std::string name;
        if (!second.is_none()) {
            if (!py::isinstance<py::str>(second))
                throw py::type_error("name must be str or None");
            name = second.cast<std::string>();
        }

        if constexpr (std::is_same_v<Policy, QualnameKeyPolicy>) {
            if (py::isinstance<py::str>(first))
                return Policy::make_from_id(first.cast<std::string>(), std::move(name));
            return Policy::make_from_python(first, std::move(name));
        } else { // PyIdentityKeyPolicy
            if (py::isinstance<py::str>(first))
                throw py::type_error("Identity policy requires a Python object as first element");
            return Policy::make_from_python(first, std::move(name));
        }
    }

public:
    Registry(bool hooks=false, KeyPolicyKind policy=KeyPolicyKind::Qualname)
      : m_policy(policy), m_hooks(hooks)
    {
        if (policy == KeyPolicyKind::Qualname) {
            m_var = hooks ? std::variant<R_QN_No,R_QN_Yes,R_ID_No,R_ID_Yes>(R_QN_Yes{})
                          : std::variant<R_QN_No,R_QN_Yes,R_ID_No,R_ID_Yes>(R_QN_No{});
        } else {
            m_var = hooks ? std::variant<R_QN_No,R_QN_Yes,R_ID_No,R_ID_Yes>(R_ID_Yes{})
                          : std::variant<R_QN_No,R_QN_Yes,R_ID_No,R_ID_Yes>(R_ID_No{});
        }
    }

    size_t size() const {
        return std::visit([](auto const& r){ return r.size(); }, m_var);
    }

    // Register with a single 2-tuple key
    void register_key(py::object key, py::object value) {
        std::visit([&](auto& r){
            auto k = make_key<std::decay_t<decltype(r)>>(key);
            r.register_value(k, std::move(value));
        }, m_var);
    }

    // Upsert with the same single-arg key
    void upsert_key(py::object key, py::object value) {
        std::visit([&](auto& r){
            auto k = make_key<std::decay_t<decltype(r)>>(key);
            r.upsert_value(k, std::move(value));
        }, m_var);
    }

    // Lookup (works for __getitem__ binding)
    py::object get(py::object key) {
        return std::visit([&](auto& r) -> py::object {
            auto k = make_key<std::decay_t<decltype(r)>>(key);
            if (auto* v = r.try_get(k)) return *v;
            throw std::runtime_error("Key not found in Registry");
        }, m_var);
    }

    bool contains(py::object key) const {
        return std::visit([&](auto const& r) -> bool {
            auto k = make_key<std::decay_t<decltype(r)>>(key);
            if constexpr (requires { r.contains(k); }) {
                return r.contains(k);
            } else {
                return false;
            }
        }, m_var);
    }

    // Run post hooks with a given key and Python object
    void post(py::object key, const py::object& o) {
        std::visit([&](auto& r){
            auto k = make_key<std::decay_t<decltype(r)>>(key);
            r.post(k, o);
        }, m_var);
    }

    // Hook registration (callers pass Python callables)
    void on_register(py::function f) {
        std::visit([&](auto& r){
            r.add_on_register([f](auto const& k, auto const& v){
                f(detail::to_py_tuple(k), v);
            });
        }, m_var);
    }
    void on_pre(py::function f) {
        std::visit([&](auto& r){
            r.add_on_pre([f](auto const& k, auto& v){
                f(detail::to_py_tuple(k), v);
            });
        }, m_var);
    }
    void on_post(py::function f) {
        std::visit([&](auto& r){
            r.add_on_post([f](auto const& k, const py::object& o){
                f(detail::to_py_tuple(k), o);
            });
        }, m_var);
    }
};

} // namespace pygim
