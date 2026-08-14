#pragma once
// mapping/hooks.h — the Registry-convergence pair: lifecycle hooks and
// strict registration semantics, as toolkit traits.
//
// CORE layer: pybind-free. hooks_trait carries what RegistryCore's
// HooksBundle carried; the CALLS are woven inside operations by the traits
// that own those operations (mutable_trait::set fires run_register when this
// trait is present — a compile-time presence check, so a hookless map pays
// nothing and, per the surface-honesty rule, HAS no add_on_* methods at all:
// the silent-discard misuse class is unrepresentable here).
//
// strict_trait carries register_or_override's duplicate/override contract
// (requires mutable — enforced by gimmap's dependency gate). Registration
// routes through set(), so hooks fire for it automatically when present.
//
// The callback type is a template parameter: std::function for runtime users
// (Registry), a constexpr-friendly functor for the compile-time proofs.

#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gimmap.h"

namespace pygim::mapping {

#if PYGIM_HAS_DEDUCING_THIS

template <typename K, typename V,
          typename OnRegister = std::function<void(const K&, const V&)>>
struct hooks_trait : hooks_trait_tag {
    constexpr void add_on_register(OnRegister hook) {
        m_on_register.push_back(std::move(hook));
    }
    constexpr void run_register(const K& key, const V& value) {
        for (auto& hook : m_on_register) hook(key, value);
    }
    [[nodiscard]] constexpr std::size_t on_register_count() const noexcept {
        return m_on_register.size();
    }

private:
    std::vector<OnRegister> m_on_register{};
};

// Strict registration: insert must be NEW, override must be EXISTING —
// deterministic either way, exactly RegistryCore's contract.
struct strict_trait : strict_trait_tag {
    template <typename Self>
    constexpr void register_or_override(this Self& self, const auto& key, auto value,
                                        bool override_existing) {
        const bool exists = self.storage().find(key) != nullptr;
        if (exists && !override_existing) {
            throw std::runtime_error("Duplicate key registration (use override=True)");
        }
        if (!exists && override_existing) {
            throw std::runtime_error("override=True requires existing key");
        }
        self.set(key, std::move(value));     // through mutable: hooks fire
    }
};

template <typename M>
concept has_hooks = std::derived_from<M, hooks_trait_tag>;
template <typename M>
concept has_strict = std::derived_from<M, strict_trait_tag>;

#endif  // PYGIM_HAS_DEDUCING_THIS

}  // namespace pygim::mapping
