#pragma once

#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../../mapping/hash_storage.h"
#include "../../mapping/storage.h"

namespace pygim::core {

/*
 * HooksBundle and NoHooks are policy types consumed by RegistryCore.
 *
 * - HooksBundle stores user-provided callbacks and executes them on lifecycle events.
 * - NoHooks provides the same API surface but compiles to no-ops — and keeps the
 *   registry a literal type, so a NoHooks registry over flat_storage can be
 *   built and queried in constant evaluation.
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
    constexpr void run_register(Args&&...) {}

    template<class... Args>
    constexpr void run_pre(Args&&...) {}

    template<class... Args>
    constexpr void run_post(Args&&...) {}

    template<class F>
    constexpr void add_on_register(F&&) {}

    template<class F>
    constexpr void add_on_pre(F&&) {}

    template<class F>
    constexpr void add_on_post(F&&) {}
};

/*
 * RegistryCore is a pybind-free storage and policy engine, written against the
 * mapping `storage` concept (mapping/storage.h) rather than a concrete map. The
 * SAME core therefore serves both phases:
 *
 * - Storage = mapping::hash_storage<...>  (DynamicRegistryCore): the run-time
 *   registry — entries arrive while the program runs, O(1) expected access,
 *   hooks allowed. This is what the Python-facing Registry and Factory use.
 * - Storage = mapping::flat_storage<...>  (StaticRegistryCore): the compile-time
 *   registry — built in one constant evaluation from an enumeration (a type list,
 *   a generated table), then queried by static_assert and at run time alike.
 *   A duplicate registration is a thrown exception, which in constant evaluation
 *   is a build error: the uniqueness of the set is proven, not checked at import.
 *
 * What the two phases can NOT share is population: a compile-time registry must
 * receive its complete set in one expression (standard C++ has no cross-file
 * mutable compile-time state); only the run-time one can accumulate.
 *
 * Semantics:
 * - register_or_override(key, value, false): insert only, duplicate -> error.
 * - register_or_override(key, value, true): replace only, missing -> error.
 * - try_get(key): mutable lookup, runs pre-hook policy.
 * - try_get_const(key): const lookup without pre-hook mutation path.
 *
 * Usage example:
 *   using Core = RegistryCore<MyKey, MyValue, mapping::hash_storage<MyKey, MyValue, MyHash, MyEq>,
 *                             NoHooks<MyKey, MyValue, MyPostPayload>, MyPostPayload>;
 *   Core core;
 *   core.register_or_override(key, value, false);
 *   auto* found = core.try_get(key);
 */
template<class Key, class Value, class Storage, class HooksPolicy, class PostArg>
    requires mapping::storage<Storage>
class RegistryCore {
public:
    using key_type = Key;
    using value_type = Value;
    using storage_type = Storage;

    // Whether keys() (and items()) iterate in a deterministic order — the
    // storage engine's contract, surfaced rather than assumed.
    static constexpr bool ordered = Storage::ordered;

    constexpr RegistryCore() = default;

    /**
     * \brief Pre-reserve storage to reduce rehashing during bulk inserts.
     * \param[in] capacity Desired bucket capacity.
     * \return void.
     * \note A no-op on engines without reserve() (the flat engine).
     */
    constexpr void reserve(std::size_t capacity) {
        if constexpr (requires(Storage& s) { s.reserve(capacity); }) m_store.reserve(capacity);
    }

    /**
     * \brief Insert new key/value and run register hooks.
     * \param[in] key Key to insert.
     * \param[in] value Value to insert.
     * \return void.
     * \note Exists for raw insert behavior when caller controls duplicate policy externally.
     */
    constexpr void register_value(const key_type& key, value_type value) {
        m_hooks.run_register(key, value);
        if (m_store.find(key) == nullptr) m_store.insert(key, std::move(value));
    }

    /**
     * \brief Upsert key/value and run register hooks.
     * \param[in] key Key to insert/assign.
     * \param[in] value Value to store.
     * \return void.
     * \note Exists for explicit replace behavior independent of strict override checks.
     */
    constexpr void upsert_value(const key_type& key, value_type value) {
        m_hooks.run_register(key, value);
        m_store.insert(key, std::move(value));
    }

    /**
     * \brief Insert-or-override with strict semantics.
     * \param[in] key Target key.
     * \param[in] value Value to store.
     * \param[in] override_existing `false` forbids duplicates, `true` requires prior existence.
     * \return void.
     * \throws std::runtime_error On duplicate insert or missing override target.
     * \note Exists to enforce deterministic override behavior used by Python API contracts —
     *       and, in constant evaluation, to turn a duplicate into a build error.
     */
    constexpr void register_or_override(const key_type& key, value_type value, bool override_existing) {
        value_type* existing = m_store.find(key);
        if (existing != nullptr) {
            if (!override_existing) {
                throw std::runtime_error("Duplicate key registration (use override=True)");
            }
            m_hooks.run_register(key, value);
            *existing = std::move(value);
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
    [[nodiscard]] constexpr bool contains(const key_type& key) const {
        return m_store.find(key) != nullptr;
    }

    /**
     * \brief Lookup mutable value and trigger pre hooks.
     * \param[in] key Key to find.
     * \return Pointer to mutable value, or `nullptr` if absent.
     * \note Exists for hook-enabled read paths where mutable pre-processing is required.
     */
    [[nodiscard]] constexpr value_type* try_get(const key_type& key) {
        value_type* found = m_store.find(key);
        if (found == nullptr) {
            return nullptr;
        }
        m_hooks.run_pre(key, *found);
        return found;
    }

    /**
     * \brief Lookup const value without pre-hook side effects.
     * \param[in] key Key to find.
     * \return Pointer to const value, or `nullptr` if absent.
     * \note Exists for read-only access paths.
     */
    [[nodiscard]] constexpr const value_type* try_get_const(const key_type& key) const {
        return m_store.find(key);
    }

    /**
     * \brief Trigger post-phase hooks.
     * \param[in] key Target key.
     * \param[in] payload User payload.
     * \return void.
     * \note Exists so callers can emit post events independently of get/register APIs.
     */
    constexpr void post(const key_type& key, const PostArg& payload) {
        m_hooks.run_post(key, payload);
    }

    /**
     * \brief Return number of entries.
     * \return Count of stored key/value pairs.
     */
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return m_store.size();
    }

    /**
     * \brief Return snapshot of all keys.
     * \return Vector containing all current keys (key-sorted when `ordered`).
     * \note Exists for introspection and debug/test tooling.
     */
    [[nodiscard]] constexpr std::vector<key_type> keys() const {
        std::vector<key_type> out;
        out.reserve(m_store.size());
        for (const auto& item : m_store.items()) out.push_back(item.first);
        return out;
    }

    /**
     * \brief The storage engine itself (read-only).
     * \return The underlying storage.
     */
    [[nodiscard]] constexpr const storage_type& storage() const noexcept { return m_store; }

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
    Storage m_store{};
    [[no_unique_address]] HooksPolicy m_hooks{};
};

// The run-time registry: hashed, growable, hooks optional.
template<class Key, class Value, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>,
         class HooksPolicy = NoHooks<Key, Value, Value>, class PostArg = Value>
using DynamicRegistryCore =
    RegistryCore<Key, Value, mapping::hash_storage<Key, Value, Hash, Eq>, HooksPolicy, PostArg>;

// The compile-time registry: flat, key-sorted, a literal type — build it in a
// constexpr/consteval function, prove it with static_assert, query it at run time.
template<class Key, class Value, class PostArg = Value>
using StaticRegistryCore =
    RegistryCore<Key, Value, mapping::flat_storage<Key, Value>, NoHooks<Key, Value, PostArg>, PostArg>;

} // namespace pygim::core
