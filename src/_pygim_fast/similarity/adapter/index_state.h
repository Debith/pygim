#pragma once
// similarity/adapter/index_state.h — what an Index and every stream it opened share: one corpus,
// the lock that guards it, a generation every change moves on, and what each corpus state gives
// every query (its units' columns, each kind's sweep order).
//
// ADAPTER layer: no Python. Every way to the corpus is a command that takes the lock it needs, so
// no caller reads or grows it unlocked:
//
//     state.write<C>([&](C& c) { c.add(std::move(units), origin); });   // exclusive; the generation moves on
//     state.read<C>([&](const corpus_reading<C>& r) {                    // shared
//         sweep(r.corpus(), r.sweep_order(kind, min_size));
//     });
//     state.read_at<C>(generation, [&](const C& c) { ... });             // shared, if nothing changed since
//
// A stream keeps the generation its reading saw and sweeps each block through read_at: a block
// sees that same corpus or learns that it changed, never one an add() is moving. Before this was
// one class's job, each caller took the lock itself, and three did not: len(), stats() and
// repr() read the units while add_files() grew them (a segfault), and a placed class stream
// counted its tiles' rows after letting go of the lock (2026-10-29).

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "../core.h"
#include "tables.h"

namespace pygim::similarity {

template <class Variant>
class index_state;

/// One corpus state, read under the shared lock: the corpus, the generation it is at, and what the
/// state keeps for every query of it. Lives only for the read that hands it over.
template <class Corpus, class Variant>
class corpus_reading {
public:
    [[nodiscard]] const Corpus& corpus() const noexcept { return m_corpus; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }
    /// The units' columns (names, files, lines: tables::unit_columns), the same for every result
    /// of this state — built by its first query that shows them.
    [[nodiscard]] std::shared_ptr<const tables::unit_columns> unit_columns() const { return m_state.unit_columns(m_corpus); }
    /// corpus::sweep_order(kind, min_size), from each kind's order sorted once a state.
    [[nodiscard]] std::vector<std::uint32_t> sweep_order(unit_kind kind, std::uint32_t min_size) const {
        return m_state.sweep_order(m_corpus, kind, min_size);
    }

private:
    friend class index_state<Variant>;
    corpus_reading(const Corpus& c, std::uint64_t generation, const index_state<Variant>& state) noexcept
        : m_corpus(c), m_generation(generation), m_state(state) {}

    const Corpus& m_corpus;
    std::uint64_t m_generation;
    const index_state<Variant>& m_state;
};

/// See the file comment. `Variant` is a std::variant of corpus<L>; which one it holds is fixed
/// when the state is made.
template <class Variant>
class index_state {
public:
    explicit index_state(Variant v) : m_corpus(std::move(v)) {}

    /// f(std::type_identity<C>{}) for the corpus type C it holds: what a caller parses for before
    /// it takes a lock. Reads only which alternative the variant holds, which never changes.
    template <class F>
    decltype(auto) of_type(F&& f) const {
        return std::visit([&](const auto& c) -> decltype(auto) { return f(std::type_identity<std::decay_t<decltype(c)>>{}); }, m_corpus);
    }

    /// f(corpus_reading) under the shared lock.
    template <class C, class F>
    decltype(auto) read(F&& f) const {
        std::shared_lock lock(m_lock);
        return f(corpus_reading<C, Variant>(std::get<C>(m_corpus), m_generation, *this));
    }

    /// f(corpus) under the shared lock, if the corpus is still at `generation`: true; false, f not
    /// called, if a write came between.
    template <class C, class F>
    bool read_at(std::uint64_t generation, F&& f) const {
        std::shared_lock lock(m_lock);
        if (m_generation != generation) return false;
        f(std::get<C>(m_corpus));
        return true;
    }

    /// f(corpus) under the exclusive lock; the generation then moves on, so every open stream
    /// fails at its next block and every kept column or order is built again.
    template <class C, class F>
    decltype(auto) write(F&& f) {
        std::unique_lock lock(m_lock);
        struct moves_on {
            std::uint64_t& generation;
            ~moves_on() { ++generation; }   // also when f throws part way: what it did is a change
        } after{m_generation};
        return f(std::get<C>(m_corpus));
    }

private:
    template <class, class>
    friend class corpus_reading;

    // One kind's units in sweep order, each with its node count, as of one corpus generation.
    class kind_order {
    public:
        kind_order() = default;
        template <class Corpus>
        kind_order(const Corpus& c, unit_kind kind, std::uint64_t generation) : m_ids(c.sweep_order(kind)), m_at(generation), m_built(true) {
            m_nodes.reserve(m_ids.size());
            for (std::uint32_t i : m_ids) m_nodes.push_back(saturated(c.nodes(i)));
        }
        [[nodiscard]] bool current(std::uint64_t generation) const noexcept { return m_built && m_at == generation; }
        [[nodiscard]] std::vector<std::uint32_t> of_size(std::uint32_t min_size) const {
            std::vector<std::uint32_t> order;
            order.reserve(m_ids.size());
            for (std::size_t k = 0; k < m_ids.size(); ++k) {
                if (m_nodes[k] >= min_size) order.push_back(m_ids[k]);
            }
            return order;
        }

    private:
        // A node count as min_size compares it: past 2^32 - 1, every min_size is reached.
        [[nodiscard]] static std::uint32_t saturated(std::size_t n) noexcept {
            return n > std::numeric_limits<std::uint32_t>::max() ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint32_t>(n);
        }

        std::vector<std::uint32_t> m_ids;
        std::vector<std::uint32_t> m_nodes;
        std::uint64_t m_at = 0;
        bool m_built = false;
    };

    // The caches, filled under the shared lock by whichever query needs them first.
    template <class Corpus>
    [[nodiscard]] std::shared_ptr<const tables::unit_columns> unit_columns(const Corpus& c) const {
        std::lock_guard lock(m_cache_lock);
        if (!m_columns || m_columns_at != m_generation) {
            m_columns = std::make_shared<const tables::unit_columns>(c);
            m_columns_at = m_generation;
        }
        return m_columns;
    }
    template <class Corpus>
    [[nodiscard]] std::vector<std::uint32_t> sweep_order(const Corpus& c, unit_kind kind, std::uint32_t min_size) const {
        std::lock_guard lock(m_cache_lock);
        kind_order& all = m_orders[number_of(kind)];
        if (!all.current(m_generation)) all = kind_order(c, kind, m_generation);
        return all.of_size(min_size);
    }

    Variant m_corpus;
    mutable std::shared_mutex m_lock;   // write() exclusive; read() and read_at() shared
    std::uint64_t m_generation = 0;     // moved on under the exclusive lock
    mutable std::mutex m_cache_lock;    // the caches below
    mutable std::shared_ptr<const tables::unit_columns> m_columns;
    mutable std::uint64_t m_columns_at = 0;
    mutable std::array<kind_order, unit_kind_names.size()> m_orders;   // by the kind's number
};

}  // namespace pygim::similarity
