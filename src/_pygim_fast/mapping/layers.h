#pragma once
// mapping/layers.h — the layered channel: merge with MEMORY.
//
// CORE layer: pybind-free. Where merge_in folds and forgets, a layered map
// RECORDS: every contribution is stored tagged with its source; folding
// happens on observation, so removing a source brings the old value back
// with no inverse math. Provenance ("who touches this key?") and undo are
// the two questions eager merging cannot answer — assembly flows live here
// (docs/design/mapping_toolkit.md: assembly = layered, accumulation =
// merge-in-place).
//
// Requires merge + mutable (enforced by gimmap's dependency gate): folding
// goes through the merge machinery, and layer state rides next to the base
// channel that set() writes. A source -> keys reverse index (the source's
// FOOTPRINT) is maintained at apply time, so removing everything one cause
// contributed is O(footprint), never a scan of the whole map.
//
// The Python adapter folds py::object contributions itself (its resolution
// includes type strategies); this trait's observe/snapshot serve C++ value
// domains and the compile-time proofs.

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "merge.h"

namespace pygim::mapping {

#if PYGIM_HAS_DEDUCING_THIS

template <typename K, typename T, typename SourceId = std::string>
struct layer_trait : layer_trait_tag {
    // One layered contribution: who wrote it, how it folds, what it adds.
    struct Contribution {
        SourceId source;
        MergeStrategy strategy;
        T value;
    };

    // ── recording ───────────────────────────────────────────────────────────
    // Strategy captured at apply time: explicit, or the map's rule for the key.
    template <typename Self>
    constexpr void apply(this Self& self, const SourceId& source, const K& key,
                         const T& value) {
        self.apply(source, key, self.strategy_for(key), value);
    }
    constexpr void apply(const SourceId& source, const K& key, MergeStrategy strategy,
                         const T& value) {
        std::vector<Contribution>* layers = m_layers.find(key);
        if (layers == nullptr) {
            m_layers.insert(key, {});
            layers = m_layers.find(key);
        }
        layers->push_back({source, strategy, value});

        std::vector<K>* keys = m_footprint.find(source);
        if (keys == nullptr) {
            m_footprint.insert(source, {});
            keys = m_footprint.find(source);
        }
        if (std::find(keys->begin(), keys->end(), key) == keys->end()) {
            keys->push_back(key);
        }
    }

    // Drop every contribution `source` made — O(footprint) via the index.
    constexpr void remove(const SourceId& source) {
        std::vector<K>* keys = m_footprint.find(source);
        if (keys == nullptr) return;
        for (const K& key : *keys) {
            std::vector<Contribution>* layers = m_layers.find(key);
            if (layers == nullptr) continue;
            std::erase_if(*layers, [&](const Contribution& c) { return c.source == source; });
            if (layers->empty()) m_layers.erase(key);
        }
        m_footprint.erase(source);
    }

    // ── provenance ──────────────────────────────────────────────────────────
    [[nodiscard]] constexpr std::vector<SourceId> sources(const K& key) const {
        std::vector<SourceId> out;
        if (const std::vector<Contribution>* layers = m_layers.find(key)) {
            for (const Contribution& c : *layers) out.push_back(c.source);
        }
        return out;
    }
    [[nodiscard]] constexpr std::vector<K> footprint(const SourceId& source) const {
        const std::vector<K>* keys = m_footprint.find(source);
        return keys == nullptr ? std::vector<K>{} : *keys;
    }
    [[nodiscard]] constexpr const std::vector<Contribution>* contributions(const K& key) const {
        return m_layers.find(key);
    }
    [[nodiscard]] constexpr bool has_layers(const K& key) const {
        return m_layers.find(key) != nullptr;
    }
    [[nodiscard]] constexpr const auto& layer_items() const noexcept {
        return m_layers.items();
    }

    // ── observation (fold on demand; the live map keeps every layer) ────────
    // Base channel first (when present), then contributions in application
    // order; without a base the first contribution establishes the value.
    template <typename Self>
    [[nodiscard]] constexpr T observe(this const Self& self, const K& key) {
        std::optional<T> acc;
        if (const T* base = self.storage().find(key)) acc = *base;
        if (const std::vector<Contribution>* layers = self.contributions(key)) {
            for (const Contribution& c : *layers) {
                acc = acc.has_value() ? merge_combine(c.strategy, *acc, c.value) : c.value;
            }
        }
        if (!acc.has_value()) throw std::out_of_range("layer_trait: key not found");
        return *acc;
    }
    template <typename Self>
    [[nodiscard]] constexpr bool holds(this const Self& self, const K& key) {
        return self.storage().find(key) != nullptr || self.has_layers(key);
    }

    // A NEW frozen map of the fully folded state — later edits to the live
    // map do not touch it. The design's "a snapshot IS the frozen type."
    template <typename Self>
    [[nodiscard]] constexpr auto snapshot(this const Self& self) {
        auto out = self.storage();                       // base channel copied
        for (const auto& [key, layers] : self.layer_items()) {
            out.insert(key, self.observe(key));          // folded value wins
        }
        return gimmap<std::remove_cvref_t<decltype(out)>>(std::move(out));
    }

private:
    flat_storage<K, std::vector<Contribution>> m_layers{};
    flat_storage<SourceId, std::vector<K>> m_footprint{};
};

template <typename M>
concept has_layers_trait = std::derived_from<M, layer_trait_tag>;

#endif  // PYGIM_HAS_DEDUCING_THIS

}  // namespace pygim::mapping
