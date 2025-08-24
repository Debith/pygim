
#include <variant>
#include <functional>
#include <pybind11/stl.h>
#include <pybind11/pybind11.h>

namespace pygim {
namespace py = pybind11;

// --- Key Policies -----------------------------------------------------------
struct PyIdentityKeyPolicy {
    struct key_type { PyObject* ptr; std::optional<std::string> name; };
    struct Hash {
        size_t operator()(key_type const& k) const noexcept {
            size_t h1 = std::hash<void*>()(k.ptr);
            size_t h2 = k.name ? std::hash<std::string>()(*k.name) : 0;
            return h1 ^ (h2 << 1);
        }
    };
    struct Eq {
        bool operator()(key_type const& a, key_type const& b) const noexcept {
            return a.ptr == b.ptr && a.name == b.name;
        }
    };
    static key_type make_from_python(py::object t, std::optional<std::string> n) {
        return { t.ptr(), std::move(n) };
    }
};

struct QualnameKeyPolicy {
    struct key_type { std::string id; std::optional<std::string> name; };
    struct Hash {
        size_t operator()(key_type const& k) const noexcept {
            size_t h1 = std::hash<std::string>()(k.id);
            size_t h2 = k.name ? std::hash<std::string>()(*k.name) : 0;
            return h1 ^ (h2 << 1);
        }
    };
    struct Eq {
        bool operator()(key_type const& a, key_type const& b) const noexcept {
            return a.id == b.id && a.name == b.name;
        }
    };
    static key_type make_from_python(py::object t, std::optional<std::string> n) {
        // module.qualname is stable across identity changes
        auto mod = py::str(t.attr("__module__")).cast<std::string>();
        auto qn  = py::str(t.attr("__qualname__")).cast<std::string>();
        return { mod + "." + qn, std::move(n) };
    }
};

// --- Hooks plumbing (compile-time optional) ---------------------------------

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

    void run_register(const K& k, const V& v) { for (auto& f: on_register) f(k,v); }
    void run_pre(const K& k, V& v)            { for (auto& f: on_pre)      f(k,v); }
    void run_post(const K& k, const py::object& o){ for (auto& f: on_post) f(k,o); }

    void add_on_register(OnRegister f) { on_register.push_back(std::move(f)); }
    void add_on_pre(OnPre f)           { on_pre.push_back(std::move(f)); }
    void add_on_post(OnPost f)         { on_post.push_back(std::move(f)); }
};

template<class K, class V>
struct Hooks<K,V,false> {
    template<class...Args> void run_register(Args&&...) {}
    template<class...Args> void run_pre(Args&&...) {}
    template<class...Args> void run_post(Args&&...) {}

    // no-op adders to keep RegistryT code uniform
    template<class F> void add_on_register(F&&) {}
    template<class F> void add_on_pre(F&&) {}
    template<class F> void add_on_post(F&&) {}
};

// --- Generic Registry (templated) ------------------------------------------

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

    // Insert or overwrite existing
    void upsert_value(const key_type& k, Value v) {
        m_hooks.run_register(k, v);
        m_map.insert_or_assign(k, std::move(v));
    }

    bool contains(const key_type& k) const {
        return m_map.find(k) != m_map.end();
    }

    // Non-const lookup (available in both modes). Runs pre-hooks when enabled.
    Value* try_get(const key_type& k) {
        auto it = m_map.find(k);
        if (it == m_map.end()) return nullptr;
        m_hooks.run_pre(k, it->second);
        return &it->second;
    }

    // Const lookup: only participates when hooks are disabled so it stays truly const.
    const Value* try_get(const key_type& k) const requires(!EnableHooks) {
        auto it = m_map.find(k);
        if (it == m_map.end()) return nullptr;
        return &it->second;
    }

    void post(const key_type& k, const py::object& o) {
        m_hooks.run_post(k, o);
    }

    size_t size() const noexcept { return m_map.size(); }

    // Hook API (only meaningful if EnableHooks=true). Always present for simplicity.
    void add_on_register(std::function<void(const key_type&, const Value&)> f) { m_hooks.add_on_register(std::move(f)); }
    void add_on_pre(std::function<void(const key_type&, Value&)> f)            { m_hooks.add_on_pre(std::move(f)); }
    void add_on_post(std::function<void(const key_type&, const py::object&)> f){ m_hooks.add_on_post(std::move(f)); }

private:
    std::unordered_map<key_type, Value, Hash, Eq> m_map;
    [[no_unique_address]] Hooks<key_type, Value, EnableHooks> m_hooks;
};

// --- Python-facing wrapper over the 2x2 matrix ------------------------------

enum class KeyPolicyKind { Qualname = 0, Identity = 1 };

class Registry {
    using Value = py::object; // store any Python object (callable or registration)
    using R_QN_No  = RegistryT<QualnameKeyPolicy, Value, false>;
    using R_QN_Yes = RegistryT<QualnameKeyPolicy, Value, true>;
    using R_ID_No  = RegistryT<PyIdentityKeyPolicy, Value, false>;
    using R_ID_Yes = RegistryT<PyIdentityKeyPolicy, Value, true>;

    std::variant<R_QN_No, R_QN_Yes, R_ID_No, R_ID_Yes> m_var;
    KeyPolicyKind m_policy;
    bool m_hooks;

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

    // C++ method named 'reg' to avoid the 'register' keyword; bound to Python as 'register'
    void reg(py::object py_type, std::optional<std::string> name, py::object value) {
        std::visit([&](auto& r){
            using R = std::decay_t<decltype(r)>;
            auto key = R::Policy::make_from_python(py_type, std::move(name));
            r.register_value(key, std::move(value));
        }, m_var);
    }

    py::object get(py::object py_key, std::optional<std::string> name = std::nullopt) {
        return std::visit([&](auto& r) -> py::object {
            using R = std::decay_t<decltype(r)>;
            typename R::Policy::key_type key;
            bool has_named = name.has_value() && !py_key.is_none();
            if (has_named) {
                key = R::Policy::make_from_python(py_key, name);
            } else if (PyTuple_Check(py_key.ptr()) && PyTuple_Size(py_key.ptr()) == 2) {
                py::tuple t = py::reinterpret_borrow<py::tuple>(py_key);
                py::object iface = t[0];
                std::optional<std::string> nm = t[1].is_none()
                    ? std::nullopt
                    : std::optional<std::string>(py::cast<std::string>(t[1]));
                key = R::Policy::make_from_python(iface, std::move(nm));
            } else {
                key = R::Policy::make_from_python(py_key, std::nullopt);
            }
            if (auto* v = r.try_get(key)) return *v;
            throw std::runtime_error("Key not found in Registry");
        }, m_var);
    }

    // Pythonic sugar
    py::object getitem(py::object key) { return get(key); }

    // Hook registration (no-ops if hooks were disabled at construction)
    void on_register(py::function f) {
        std::visit([&](auto& r){ r.add_on_register([f](auto const& k, auto const& v){ f(k, v); }); }, m_var);
    }
    void on_pre(py::function f) {
        std::visit([&](auto& r){ r.add_on_pre([f](auto const& k, auto& v){ f(k, v); }); }, m_var);
    }
    void on_post(py::function f) {
        std::visit([&](auto& r){ r.add_on_post([f](auto const& k, const py::object& o){ f(k, o); }); }, m_var);
    }
};

} // namespace pygim
