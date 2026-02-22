#pragma once

#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pygim::core {

/*
 * HooksBundle and NoHooks are policy types consumed by RegistryCore.
 *
 * - HooksBundle stores user-provided callbacks and executes them on lifecycle events.
 * - NoHooks provides the same API surface but compiles to no-ops.
 *
 * Usage example:
 *   using Hooks = HooksBundle<MyKey, MyValue, MyPostPayload>;
 *   Hooks hooks;
 *   hooks.add_on_register([](const MyKey&, const MyValue&) {});
 */
template<class K, class V, class PostArg>
struct HooksBundle {
    using OnRegister = std::function<void(const K&, const V&)>;
    using OnPre = std::function<void(const K&, V&)>;
    using OnPost = std::function<void(const K&, const PostArg&)>;

    std::vector<OnRegister> on_register;
    std::vector<OnPre> on_pre;
    std::vector<OnPost> on_post;

    /**
     * \brief Execute all register hooks for key/value.
     * \param[in] key Target key.
     * \param[in] value Value being inserted/overridden.
     * \return void.
     * \note Exists to centralize lifecycle notification behavior.
     */
    void run_register(const K& key, const V& value) { for (auto& hook : on_register) hook(key, value); }
    /**
     * \brief Execute pre-access hooks before mutable reads.
     * \param[in] key Lookup key.
     * \param[in,out] value Value that may be observed/mutated by hook policy.
     * \return void.
     * \note Exists to support instrumentation or lazy refresh paths.
     */
    void run_pre(const K& key, V& value) { for (auto& hook : on_pre) hook(key, value); }
    /**
     * \brief Execute post hooks with user payload.
     * \param[in] key Target key.
     * \param[in] payload Post-event payload.
     * \return void.
     * \note Exists for out-of-band notifications after core operations.
     */
    void run_post(const K& key, const PostArg& payload) { for (auto& hook : on_post) hook(key, payload); }

    /**
     * \brief Add register-phase callback.
     * \param[in] hook Callback accepting `(key, value)`.
     * \return void.
     */
    void add_on_register(OnRegister hook) { on_register.push_back(std::move(hook)); }
    /**
     * \brief Add pre-access callback.
     * \param[in] hook Callback accepting `(key, value&)`.
     * \return void.
     */
    void add_on_pre(OnPre hook) { on_pre.push_back(std::move(hook)); }
    /**
     * \brief Add post callback.
     * \param[in] hook Callback accepting `(key, payload)`.
     * \return void.
     */
    void add_on_post(OnPost hook) { on_post.push_back(std::move(hook)); }
};

template<class K, class V, class PostArg>
struct NoHooks {
    template<class... Args>
    void run_register(Args&&...) {}

    template<class... Args>
    void run_pre(Args&&...) {}

    template<class... Args>
    void run_post(Args&&...) {}

    template<class F>
    void add_on_register(F&&) {}

    template<class F>
    void add_on_pre(F&&) {}

    template<class F>
    void add_on_post(F&&) {}
};

/*
 * RegistryCore is a pybind-free storage and policy engine.
 *
 * Semantics:
 * - register_or_override(key, value, false): insert only, duplicate -> error.
 * - register_or_override(key, value, true): replace only, missing -> error.
 * - try_get(key): mutable lookup, runs pre-hook policy.
 * - try_get_const(key): const lookup without pre-hook mutation path.
 *
 * Usage example:
 *   using Core = RegistryCore<MyKey, MyValue, MyHash, MyEq,
 *                            NoHooks<MyKey, MyValue, MyPostPayload>, MyPostPayload>;
 *   Core core;
 *   core.register_or_override(key, value, false);
 *   auto* found = core.try_get(key);
 */
template<class Key, class Value, class Hash, class Eq, class HooksPolicy, class PostArg>
class RegistryCore {
public:
    using key_type = Key;
    using value_type = Value;

    /**
     * \brief Pre-reserve storage to reduce rehashing during bulk inserts.
     * \param[in] capacity Desired bucket capacity.
     * \return void.
     * \note Exists for predictable performance in registration-heavy workloads.
     */
    void reserve(std::size_t capacity) { m_map.reserve(capacity); }

    /**
     * \brief Insert new key/value and run register hooks.
     * \param[in] key Key to insert.
     * \param[in] value Value to insert.
     * \return void.
     * \note Exists for raw insert behavior when caller controls duplicate policy externally.
     */
    void register_value(const key_type& key, value_type value) {
        m_hooks.run_register(key, value);
        m_map.emplace(key, std::move(value));
    }

    /**
     * \brief Upsert key/value and run register hooks.
     * \param[in] key Key to insert/assign.
     * \param[in] value Value to store.
     * \return void.
     * \note Exists for explicit replace behavior independent of strict override checks.
     */
    void upsert_value(const key_type& key, value_type value) {
        m_hooks.run_register(key, value);
        m_map.insert_or_assign(key, std::move(value));
    }

    /**
     * \brief Insert-or-override with strict semantics.
     * \param[in] key Target key.
     * \param[in] value Value to store.
     * \param[in] override_existing `false` forbids duplicates, `true` requires prior existence.
     * \return void.
     * \throws std::runtime_error On duplicate insert or missing override target.
     * \note Exists to enforce deterministic override behavior used by Python API contracts.
     */
    void register_or_override(const key_type& key, value_type value, bool override_existing) {
        auto it = m_map.find(key);
        bool exists = (it != m_map.end());
        if (exists) {
            if (!override_existing) {
                throw std::runtime_error("Duplicate key registration (use override=True)");
            }
            m_hooks.run_register(key, value);
            it->second = std::move(value);
            return;
        }

        if (override_existing) {
            throw std::runtime_error("override=True requires existing key");
        }

        register_value(key, std::move(value));
    }

    /**
     * \brief Check key existence.
     * \param[in] key Key to test.
     * \return `true` when key exists, else `false`.
     * \note Exists to support fast existence checks without lookup side effects.
     */
    bool contains(const key_type& key) const {
        return m_map.find(key) != m_map.end();
    }

    /**
     * \brief Lookup mutable value and trigger pre hooks.
     * \param[in] key Key to find.
     * \return Pointer to mutable value, or `nullptr` if absent.
     * \note Exists for hook-enabled read paths where mutable pre-processing is required.
     */
    value_type* try_get(const key_type& key) {
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            return nullptr;
        }
        m_hooks.run_pre(key, it->second);
        return &it->second;
    }

    /**
     * \brief Lookup const value without pre-hook side effects.
     * \param[in] key Key to find.
     * \return Pointer to const value, or `nullptr` if absent.
     * \note Exists for read-only access paths.
     */
    const value_type* try_get_const(const key_type& key) const {
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            return nullptr;
        }
        return &it->second;
    }

    /**
     * \brief Trigger post-phase hooks.
     * \param[in] key Target key.
     * \param[in] payload User payload.
     * \return void.
     * \note Exists so callers can emit post events independently of get/register APIs.
     */
    void post(const key_type& key, const PostArg& payload) {
        m_hooks.run_post(key, payload);
    }

    /**
     * \brief Return number of entries.
     * \return Count of stored key/value pairs.
     */
    std::size_t size() const noexcept {
        return m_map.size();
    }

    /**
     * \brief Return snapshot of all keys.
     * \return Vector containing all current keys.
     * \note Exists for introspection and debug/test tooling.
     */
    std::vector<key_type> keys() const {
        std::vector<key_type> out;
        out.reserve(m_map.size());
        for (const auto& kv : m_map) {
            out.push_back(kv.first);
        }
        return out;
    }

    /**
     * \brief Register callback for register lifecycle event.
     * \param[in] hook Callback `(key, value)`.
     * \return void.
     */
    void add_on_register(std::function<void(const key_type&, const value_type&)> hook) {
        m_hooks.add_on_register(std::move(hook));
    }

    /**
     * \brief Register callback for pre-access lifecycle event.
     * \param[in] hook Callback `(key, value&)`.
     * \return void.
     */
    void add_on_pre(std::function<void(const key_type&, value_type&)> hook) {
        m_hooks.add_on_pre(std::move(hook));
    }

    /**
     * \brief Register callback for post lifecycle event.
     * \param[in] hook Callback `(key, payload)`.
     * \return void.
     */
    void add_on_post(std::function<void(const key_type&, const PostArg&)> hook) {
        m_hooks.add_on_post(std::move(hook));
    }

private:
    std::unordered_map<key_type, value_type, Hash, Eq> m_map;
    [[no_unique_address]] HooksPolicy m_hooks;
};

} // namespace pygim::core
