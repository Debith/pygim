#pragma once
// mapping/basic_map.h — trait assembly over a storage engine.
//
// The inversion at the heart of the toolkit (docs/design/mapping_toolkit.md):
// the base map is FROZEN — the read-only Mapping surface. Mutability is the
// FIRST TRAIT, not the default, so a frozen map does not have a set() that
// throws: the method does not exist, and misuse is a compile error. The
// negative proofs in tests/static/mapping_core_proofs.cpp pin that down.
//
//   basic_map<S>                 -> Mapping        (frozen)
//   basic_map<S, mutable_trait>  -> MutableMapping
//   + merge / layers / hooks     -> later steps
//
// Traits are plain structs whose methods use C++23 deducing this (P0847):
// each method sees the full derived map as `self`, so traits compose without
// CRTP plumbing. The frontend feature ships in GCC 14+/MSVC 19.32+; on older
// compilers (feature-test macro absent) the traits and their proofs drop out
// while the frozen base — which needs nothing beyond C++20 — still compiles
// and is still proven. Graduated coverage, not silent vacuity: a build
// without the macro proves the storage laws and the frozen surface only.

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <utility>

#include "storage.h"

namespace pygim::mapping {

#if defined(__cpp_explicit_this_parameter)
#define PYGIM_HAS_DEDUCING_THIS 1
#else
#define PYGIM_HAS_DEDUCING_THIS 0
#endif

#if PYGIM_HAS_DEDUCING_THIS
// mutable_trait — the map gains set / erase / clear. Everything routes through
// the storage concept; no engine specifics leak in.
struct mutable_trait {
    constexpr void set(this auto& self, const auto& key, auto value) {
        self.storage().insert(key, std::move(value));
    }
    constexpr bool erase(this auto& self, const auto& key) {
        return self.storage().erase(key);
    }
    constexpr void clear(this auto& self) { self.storage().clear(); }
};
#endif

// Dependency gate, checked at assembly. Grows with the trait set:
// merge (step 2) has no requirements; layers (step 4) will require
// merge + mutable; hooks (step 5) none. Today only mutable exists.
template <typename... Traits>
consteval bool traits_dependencies_ok() {
    return true;
}

template <storage S, typename... Traits>
class basic_map : public Traits... {
    static_assert(traits_dependencies_ok<Traits...>());

public:
    using key_type = typename S::key_type;
    using mapped_type = typename S::mapped_type;
    using storage_type = S;

    constexpr basic_map() = default;
    constexpr explicit basic_map(S storage_) : m_storage(std::move(storage_)) {}
    // Construction fills the storage directly; construction is not mutation,
    // so the frozen base is buildable without the mutable trait.
    constexpr basic_map(std::initializer_list<typename S::item_type> items)
        requires std::constructible_from<S, std::initializer_list<typename S::item_type>>
        : m_storage(items) {}

    // ── frozen base surface (always present) ────────────────────────────────
    [[nodiscard]] constexpr bool contains(const key_type& key) const {
        return m_storage.find(key) != nullptr;
    }
    [[nodiscard]] constexpr const mapped_type& at(const key_type& key) const {
        const mapped_type* value = m_storage.find(key);
        if (value == nullptr) throw std::out_of_range("basic_map: key not found");
        return *value;
    }
    [[nodiscard]] constexpr mapped_type value_or(const key_type& key,
                                                 mapped_type fallback) const {
        const mapped_type* value = m_storage.find(key);
        return value == nullptr ? std::move(fallback) : *value;
    }
    [[nodiscard]] constexpr decltype(auto) items() const noexcept {
        return m_storage.items();
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_storage.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_storage.empty(); }

    friend constexpr bool operator==(const basic_map& lhs, const basic_map& rhs) {
        return lhs.m_storage == rhs.m_storage;
    }

    // ── transitions ─────────────────────────────────────────────────────────
    // freeze(): the storage moves (or copies, from an lvalue) into a bare
    // frozen basic_map<S> — traits drop away in the TYPE, not by runtime flag.
    [[nodiscard]] constexpr basic_map<S> freeze() && {
        return basic_map<S>(std::move(m_storage));
    }
    [[nodiscard]] constexpr basic_map<S> freeze() const&
        requires std::copyable<S>
    {
        return basic_map<S>(m_storage);
    }
    // thaw<Ts...>(): copy into a trait-bearing variant of the same storage.
    template <typename... Ts>
    [[nodiscard]] constexpr basic_map<S, Ts...> thaw() const
        requires std::copyable<S>
    {
        return basic_map<S, Ts...>(m_storage);
    }

    // ── trait interface (documented internal) ───────────────────────────────
    // Traits reach the engine exclusively through this accessor; it is public
    // so that third-party traits can be written outside this header.
    constexpr S& storage() noexcept { return m_storage; }
    constexpr const S& storage() const noexcept { return m_storage; }

private:
    S m_storage{};
};

#if PYGIM_HAS_DEDUCING_THIS
// Structural trait detection: drives dependency checks and the binding
// layer's surface emission (bind_mapping's `if constexpr`).
template <typename M>
concept has_mutable = std::derived_from<M, mutable_trait>;

// Spot proofs only — the exhaustive suites (including the negative
// composition proofs) live in tests/static/mapping_core_proofs.cpp.
static_assert(has_mutable<basic_map<flat_storage<int, int>, mutable_trait>>);
static_assert(!has_mutable<basic_map<flat_storage<int, int>>>);
#endif

}  // namespace pygim::mapping
