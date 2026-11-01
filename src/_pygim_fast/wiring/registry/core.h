#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "../../mapping/bucket_storage.h"
#include "../../mapping/hash_storage.h"
#include "../../utils/hooks.h"
#include "../../mapping/storage.h"

namespace pygim::core {

/*
 * HooksBundle and NoHooks are the registry's hook policies, built on the one hooks mechanism
 * (utils/hooks.h) over the registry's three events:
 *
 * - HooksBundle stores user-provided callbacks and runs them on lifecycle events.
 * - NoHooks has the same API surface but compiles to no-ops — and keeps the
 *   registry a literal type, so a NoHooks registry over flat_storage can be
 *   built and queried in constant evaluation.
 *
 * Usage example:
 *   using Hooks = HooksBundle<MyKey, MyValue, MyPostPayload>;
 *   Hooks hooks;
 *   hooks.add<register_event<MyKey, MyValue>>([](const MyKey&, const MyValue&) {});
 */
/// An entry is added or overridden: (key, value).
template<class K, class V>
struct register_event { using signature = void(const K&, const V&); };
/// A mutable lookup, before the value is returned: (key, value&).
template<class K, class V>
struct pre_access_event { using signature = void(const K&, V&); };
/// Fired by post(key, payload): (key, payload).
template<class K, class PostArg>
struct post_event { using signature = void(const K&, const PostArg&); };

template<class K, class V, class PostArg>
using HooksBundle = pygim::hooks<register_event<K, V>, pre_access_event<K, V>, post_event<K, PostArg>>;

template<class K, class V, class PostArg>
using NoHooks = pygim::no_hooks<register_event<K, V>, pre_access_event<K, V>, post_event<K, PostArg>>;

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
 * - Storage = mapping::bucket_storage<...>  (StaticWordRegistry): the same, for
 *   short ASCII words read on a hot path (a reader's keywords, punctuators, node
 *   kinds): a word is compared only with those of its first byte and length.
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
        m_hooks.template run<register_event<Key, Value>>(key, value);
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
        m_hooks.template run<register_event<Key, Value>>(key, value);
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
            m_hooks.template run<register_event<Key, Value>>(key, value);
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
    [[nodiscard]] constexpr bool contains(const key_type& key) const { return m_store.contains(key); }   // the engine's: no pointer compared

    /**
     * \brief The value at `key`, or `fallback` (dict.get), without pre-hook side effects.
     * \param[in] key Key to find.
     * \param[in] fallback Returned when `key` is absent.
     * \return A copy of the value, or `fallback`.
     * \note Through the engine's own value_or when it has one, so a constexpr table on the
     *       bucketed engine answers without comparing a pointer (a sanitizer build needs that).
     */
    [[nodiscard]] constexpr value_type value_or(const key_type& key, value_type fallback) const {
        return m_store.value_or(key, std::move(fallback));
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
        m_hooks.template run<pre_access_event<Key, Value>>(key, *found);
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
        m_hooks.template run<post_event<Key, PostArg>>(key, payload);
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
     * \return Vector containing all current keys, in the storage's items() order: key-sorted
     *         (flat_storage), insertion (bucket_storage, until an erase), unspecified (hash_storage).
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
        m_hooks.template add<register_event<Key, Value>>(std::move(hook));
    }

    /**
     * \brief Register callback for pre-access lifecycle event.
     * \param[in] hook Callback `(key, value&)`.
     * \return void.
     */
    void add_on_pre(std::function<void(const key_type&, value_type&)> hook) {
        m_hooks.template add<pre_access_event<Key, Value>>(std::move(hook));
    }

    /**
     * \brief Register callback for post lifecycle event.
     * \param[in] hook Callback `(key, payload)`.
     * \return void.
     */
    void add_on_post(std::function<void(const key_type&, const PostArg&)> hook) {
        m_hooks.template add<post_event<Key, PostArg>>(std::move(hook));
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

// The compile-time registry of short ASCII words read on a hot path — a reader's keywords and
// punctuators: the bucketed engine (mapping/bucket_storage.h), a word compared only with those of
// its first byte and length. At most `Capacity` words.
template<class Value, std::size_t Capacity>
using StaticWordRegistry =
    RegistryCore<std::string_view, Value, mapping::bucket_storage<Value, Capacity>, NoHooks<std::string_view, Value, Value>, Value>;

} // namespace pygim::core
