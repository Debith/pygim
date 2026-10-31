#pragma once
// similarity/sweeps.h — a query's sweep on every core: which way it runs, its rows counted by
// kind, the parts each worker sorts, and their ranking merged in batches; results identical for
// any number of workers.
//
// CORE layer: no Python, no Arrow; it runs on the process's workers (utils/parallel.h). core.h
// supplies the pieces (sweep_order, sweep_rows, collector, before) and proves that any partition of
// the rows, each part in its own collector, gives the sequential result (sweep_partition_law).
// That is what makes the dynamic scheduling here safe: which worker computes which row varies run
// to run, the returned list never does.
//
// What a query does is decided here, once, and by type from there on: best_pairs() picks the
// prefix filter, its ladder or a sweep; with_stream() a stream's pipeline and counting;
// with_counts() turns a unit_kind into the counting type — function_counts, class_counts, or for
// a filtered stream filtered_counts — that a sweep or a stream is instantiated with, so no row asks
// which kind it is.
//
//     const sorted_parts parts = sweeps::best_pairs(c, c.sweep_order(kind, min_size), threshold, limit, kind);
//     sweeps::ranked(parts, limit, batch_size, [&](std::size_t b, std::size_t n, auto&& chunks) {
//         chunks([&](std::size_t at, const match* m, std::size_t k) { ... });   // batch b's pairs, k at a time
//     });
//
// Threading: callers hold whatever lock protects the corpus for the whole call; the workers only
// read it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "../utils/cpu.h"
#include "../utils/parallel.h"
#include "../utils/pieces.h"
#include "core.h"

namespace pygim::similarity::sweeps {

// The worker count and the loop of independent tasks are utils/parallel.h's, shared with code's corpus.
using ::pygim::parallel::for_each;
using ::pygim::parallel::workers_for;

/// A sweep's sorted parts: their union, ranked and cut to the limit, is the query's result
/// (ranked() and select() read it so without ever building it).
using sorted_parts = std::vector<matches>;

/// The rows from which a sweep runs on more than one worker: below, one thread is faster than
/// handing rows out.
inline constexpr std::size_t parallel_from = 256;
[[nodiscard]] inline std::size_t sweep_workers(std::size_t rows) { return rows < parallel_from ? 1 : workers_for(rows); }

/// How many pieces of a worker's share a class sweep's block or a class stream's tile holds at most
/// (1/64 each): enough that the heaviest classes, which come last, spread over every worker.
inline constexpr std::size_t shares_per_worker = 64;

/// Sweep rows cut into blocks in order: block i is rows [bounds[i], bounds[i + 1]), closed once its
/// rows' cost(r) reaches `target` or its pairs `max_pairs` — row r of n pairs with the n - r - 1
/// after it, so early blocks hold few rows and late blocks many.
template <class Cost>
[[nodiscard]] std::vector<std::size_t> sweep_blocks(std::size_t rows, double target, std::size_t max_pairs, const Cost& cost) {
    std::vector<std::size_t> bounds{0};
    double spent = 0.0;
    std::size_t pairs = 0;
    for (std::size_t r = 0; r < rows; ++r) {
        spent += cost(r);
        pairs += rows - r - 1;
        if (spent >= target || pairs >= max_pairs) {
            bounds.push_back(r + 1);
            spent = 0.0;
            pairs = 0;
        }
    }
    if (bounds.back() != rows) bounds.push_back(rows);
    return bounds;
}

/// Blocks of about `target` pairs, for rows whose pairs cost alike (functions). Depends on
/// (rows, target) only.
[[nodiscard]] inline std::vector<std::size_t> sweep_blocks(std::size_t rows, std::size_t target) {
    return sweep_blocks(rows, std::numeric_limits<double>::infinity(), target, [](std::size_t) { return 0.0; });
}

/// What a class sweep's work costs a worker: row r against its partners [from, end) costs r's
/// methods times theirs — the counts it takes, the matrices it fills and assigns — and 4 a pair
/// besides (fitted to Lib/test's 11,115 class rows timed one by one, 2026-10-28). The heaviest
/// classes come last in a sweep and cost most: Lib/test's last tenth of rows holds 1% of the pairs
/// and 45% of the time, so work cut by pairs alone left a stream's end to a few heavy blocks.
class class_costs {
public:
    template <class Corpus>
    class_costs(const Corpus& c, const std::vector<std::uint32_t>& order) : m_methods(order.size()), m_first(order.size() + 1, 0) {
        for (std::size_t r = 0; r < order.size(); ++r) {
            m_methods[r] = static_cast<std::uint32_t>(c.units()[order[r]].methods().size());
            m_first[r + 1] = m_first[r] + m_methods[r];
        }
    }

    [[nodiscard]] double operator()(std::size_t r, std::size_t from, std::size_t end) const noexcept {
        return static_cast<double>(m_methods[r]) * static_cast<double>(m_first[end] - m_first[from]) + 4.0 * static_cast<double>(end - from);
    }
    /// Row r against every later class.
    [[nodiscard]] double row(std::size_t r) const noexcept { return (*this)(r, r + 1, m_methods.size()); }
    [[nodiscard]] double total() const noexcept {
        double t = 0.0;
        for (std::size_t r = 0; r < m_methods.size(); ++r) t += row(r);
        return t;
    }
    /// What one piece of work may cost: 1/shares_per_worker of a worker's share of the total.
    [[nodiscard]] double piece(std::size_t workers) const noexcept {
        return total() / static_cast<double>(shares_per_worker * std::max<std::size_t>(workers, 1));
    }

private:
    std::vector<std::uint32_t> m_methods;   // each row's
    std::vector<std::uint64_t> m_first;     // rows [0, r)'s methods
};

/// A placed stream's tiles: the places [0, places.pairs()) cut in order at every batch's first
/// place, every `max_pairs` places, and where the pieces' cost since the tile began reaches
/// `target` — cost(r, from, end) of row r's partners [from, end), growing with end; a piece of a
/// row may end a tile. Within its batch a tile starts at a multiple of `align` places, so two
/// tiles never write one byte of a bit column (`max_pairs` a multiple of it too). Returns each
/// tile's first place, then places.pairs().
///
///     place_tiles(sweep_positions(4), 4, 3, 1, inf, none)   -> {0, 3, 4, 6}   (6 pairs: a cap, a batch)
template <class Cost>
[[nodiscard]] constexpr std::vector<std::size_t> place_tiles(const sweep_positions& places, std::size_t batch, std::size_t max_pairs, std::size_t align,
                                                             double target, const Cost& cost) {
    const std::size_t pairs = places.pairs();
    std::vector<std::size_t> first;
    for (std::size_t p = 0; p < pairs;) {
        first.push_back(p);
        const std::size_t start = p / batch * batch;   // the batch's first place
        const std::size_t q = std::min({pairs, start + batch, p + max_pairs});
        double spent = 0.0;
        std::size_t cut = q;
        places.for_each_piece(p, q, [&](std::size_t r, std::size_t from, std::size_t end, std::size_t at) {
            if (const double piece = cost(r, from, end); spent + piece < target) {
                spent += piece;
                return true;
            }
            std::size_t lo = from + 1, hi = end;   // the first partner whose piece reaches the target
            while (lo < hi) {
                const std::size_t mid = lo + (hi - lo) / 2;
                if (spent + cost(r, from, mid) >= target) hi = mid;
                else lo = mid + 1;
            }
            cut = std::min(q, start + (at + (lo - from) - start + align - 1) / align * align);   // up to the next aligned place
            return false;   // the tile ends here: no later piece of it is looked at
        });
        p = cut;
    }
    first.push_back(pairs);
    return first;
}
static_assert([] {
    auto none = [](std::size_t, std::size_t, std::size_t) { return 0.0; };
    auto partners = [](std::size_t, std::size_t from, std::size_t end) { return static_cast<double>(end - from); };
    const sweep_positions places(4);   // (0,1) (0,2) (0,3) | (1,2) (1,3) (2,3)
    return place_tiles(places, 4, 3, 1, std::numeric_limits<double>::infinity(), none) == std::vector<std::size_t>{0, 3, 4, 6} &&
           place_tiles(places, 6, 6, 1, 2.0, partners) == std::vector<std::size_t>{0, 2, 4, 6} &&   // two pairs' cost a tile, cut inside row 0
           place_tiles(places, 6, 6, 4, 2.0, partners) == std::vector<std::size_t>{0, 4, 6};        // the cut at 2 moved up to 4
}());

/// The candidate pairs of sweep rows [first, last) of n: what a block keeps
/// at threshold 0, so its collector is sized once instead of doubling.
[[nodiscard]] inline std::size_t candidates(std::size_t n, std::size_t first, std::size_t last) {
    std::size_t c = 0;
    for (std::size_t r = first; r < last; ++r) c += n - r - 1;
    return c;
}

/// The pairs a stream worker writes in one go at `row_bytes` a pair: three quarters of its L2, so
/// what it writes is still there when it hands it over; a multiple of 64 rows (whole bytes of a bit
/// column), at least 1,024. Measured on the stdlib's 127M pairs (2026-10-27, Ryzen 7 5800X):
/// 16K-pair tiles of 24 bytes 114 ms, 512K-pair blocks 360 ms (docs/design/vectorisation.md).
[[nodiscard]] constexpr std::size_t tile_pairs(const cache_layout& c, std::size_t row_bytes) noexcept {
    const std::size_t fit = c.l2() * 3 / 4 / std::max<std::size_t>(row_bytes, 1);
    return std::max<std::size_t>(1024, fit / 64 * 64);
}
static_assert(tile_pairs(cache_layout(32768, 524288, 33554432, 64), 24) == 16384 &&   // a, b, shared, total on a 5800X
              tile_pairs(cache_layout(32768, 524288, 33554432, 64), 57) == 6848 && tile_pairs(cache_layout(), 1 << 20) == 1024);

/// The method counts a class row may hold at once: three quarters of a worker's L2 of them.
[[nodiscard]] constexpr std::size_t class_tile_cells(const cache_layout& c) noexcept { return std::max<std::size_t>(1024, c.l2() * 3 / 4 / sizeof(std::uint32_t)); }
static_assert(class_tile_cells(cache_layout(32768, 524288, 33554432, 64)) == 98304);

/// How many tiles of `tile_bytes` a stream's workers may run ahead of the batch being read: what
/// fits in an eighth of the shared L3 — so the reader finds them in cache — at least 4, and no more
/// than two per worker. In the C++ harness of docs/design/vectorisation.md ("Caches"): 8 ahead
/// 114 ms, 18 ahead 142, 32 ahead 191.
[[nodiscard]] constexpr std::size_t tiles_ahead(const cache_layout& c, std::size_t tile_bytes, std::size_t workers) noexcept {
    return std::clamp<std::size_t>(c.l3() / 8 / std::max<std::size_t>(tile_bytes, 1), 4, std::max<std::size_t>(4, 2 * workers));
}
static_assert(tiles_ahead(cache_layout(32768, 524288, 33554432, 64), 16384 * 24, 16) == 10 && tiles_ahead(cache_layout(), 1 << 30, 16) == 4);

/// A token_index built on every core: sequential_build's tasks, the same cut, run across the
/// workers. The tasks touch disjoint data, so the index is the sequential one.
class parallel_build {
public:
    explicit parallel_build(build_grain grain = {}) noexcept : m_grain(grain) {}

    [[nodiscard]] const build_grain& grain() const noexcept { return m_grain; }
    template <class F>
    void for_each(std::size_t tasks, F&& f) const {
        ::pygim::parallel::for_each(tasks, workers_for(tasks), [&](std::size_t k, std::size_t) { f(k); });
    }

private:
    build_grain m_grain;
};

// ── counting, by kind ──────────────────────────────────────────────────────

/// How rows are claimed without a limit: in blocks of equal candidates (functions: a counted row
/// costs its partners), or one by one, heaviest first (classes: a class pair costs its method
/// matrix, not its place in the order, and the heaviest rows came last — Lib/test's classes at 0.8
/// took 10.1 s in 32 blocks of equal candidate count, 1.2 s so).
enum class unlimited_rows : std::uint8_t { equal_blocks, heaviest_first };

/// Function rows counted: every row's shared counts with all its partners at once, from a
/// token_index of every token (sweep_rows::counted_row), not a merge per pair.
class function_counts {
public:
    static constexpr unit_kind kind = unit_kind::function;
    static constexpr unlimited_rows unlimited = unlimited_rows::equal_blocks;

    /// One worker's buffers, kept from row to row: one count per row, zeroes between rows.
    class worker {
    public:
        explicit worker(std::size_t rows) : m_counts(rows, 0) {}
        [[nodiscard]] std::vector<std::uint32_t>& counts() noexcept { return m_counts; }

    private:
        std::vector<std::uint32_t> m_counts;
    };

    /// Every token indexed, whatever the threshold: a row's counts with all its partners.
    template <class Corpus>
    function_counts(const Corpus& c, const std::vector<std::uint32_t>& order, double) : m_index(c.units(), order, 0.0, parallel_build{}) {}

    /// Every row's weight, in sweep order.
    [[nodiscard]] const std::vector<std::uint64_t>& weights() const noexcept { return m_index.weights(); }

    /// Row r into `into`.
    template <class L>
    void row(const sweep_rows<L>& rows, const std::vector<std::uint32_t>& order, std::size_t r, worker& w, collector& into) const {
        rows.counted_row(m_index, order, r, w.counts(), into);
    }
    /// Row r's shared counts with its partners [from, end): write(shared), shared[0..end - from).
    template <class L, class Write>
    void piece(const sweep_rows<L>&, const std::vector<std::uint32_t>&, std::size_t r, std::size_t from, std::size_t end, worker& w,
               Write&& write) const {
        const zeroed_counts counts(w.counts().data() + from, end - from);
        m_index.count_shared_into(r, counts.data(), from, end);
        write(counts.data());
    }

    /// A stream's blocks: about `max_pairs` pairs each, as a function pair costs alike wherever it is.
    template <class Corpus>
    [[nodiscard]] static std::vector<std::size_t> blocks(const Corpus&, const std::vector<std::uint32_t>& order, std::size_t max_pairs) {
        return sweep_blocks(order.size(), max_pairs);
    }
    /// A placed stream's tiles: by places alone.
    template <class Corpus>
    [[nodiscard]] static std::vector<std::size_t> tiles(const Corpus&, const std::vector<std::uint32_t>&, const sweep_positions& places, std::size_t batch,
                                                        std::size_t max_pairs, std::size_t align) {
        return place_tiles(places, batch, max_pairs, align, std::numeric_limits<double>::infinity(), [](std::size_t, std::size_t, std::size_t) { return 0.0; });
    }

private:
    token_index m_index;
};

/// Class rows counted: every class's header counts and its methods' counts with all its partners'
/// from a class_index (sweep_rows::counted_class_row); only each pair's method assignment is solved.
class class_counts {
public:
    static constexpr unit_kind kind = unit_kind::class_;
    static constexpr unlimited_rows unlimited = unlimited_rows::heaviest_first;

    /// One worker's buffers, kept from row to row: its counts (class_row_scratch), and a piece's
    /// scores, each written before it is read.
    class worker {
    public:
        explicit worker(std::size_t rows) : m_scratch(rows, class_tile_cells(this_cpu().caches())), m_rows(rows) {}
        [[nodiscard]] class_row_scratch& scratch() noexcept { return m_scratch; }
        [[nodiscard]] std::vector<std::uint64_t>& scores() {
            if (m_scores.empty()) m_scores.resize(m_rows);   // only a placed stream's pieces score so
            return m_scores;
        }

    private:
        class_row_scratch m_scratch;
        std::vector<std::uint64_t> m_scores;   // a class pair's shared count: its header's and its methods'
        std::size_t m_rows;
    };

    template <class Corpus>
    class_counts(const Corpus& c, const std::vector<std::uint32_t>& order, double) : m_index(c.units(), order, parallel_build{}) {}

    /// Every row's weight, in sweep order.
    [[nodiscard]] const std::vector<std::uint64_t>& weights() const noexcept { return m_index.headers().weights(); }

    /// Row r into `into`.
    template <class L>
    void row(const sweep_rows<L>& rows, const std::vector<std::uint32_t>& order, std::size_t r, worker& w, collector& into) const {
        rows.counted_class_row(m_index, order, r, w.scratch(), into);
    }
    /// Row r's shared counts with its partners [from, end): write(shared), shared[0..end - from).
    template <class L, class Write>
    void piece(const sweep_rows<L>& rows, const std::vector<std::uint32_t>& order, std::size_t r, std::size_t from, std::size_t end, worker& w,
               Write&& write) const {
        std::vector<std::uint64_t>& scores = w.scores();
        rows.counted_class_partners(m_index, order, r, from, end, w.scratch(), [] { return 0.0; },
                                    [&](std::size_t j, const score& s) { scores[j] = s.shared(); });
        write(scores.data() + from);
    }

    /// A stream's blocks: `max_pairs` pairs at most, cut smaller where their rows' method matrices
    /// cost more — a piece of a worker's share each (class_costs). Simulated on Lib/test's measured
    /// rows, 16 workers took 814 ms cut by pairs alone, where an even share is 418; cut so, 434.
    template <class Corpus>
    [[nodiscard]] static std::vector<std::size_t> blocks(const Corpus& c, const std::vector<std::uint32_t>& order, std::size_t max_pairs) {
        const class_costs cost(c, order);
        return sweep_blocks(order.size(), cost.piece(workers_for(order.size())), max_pairs, [&](std::size_t r) { return cost.row(r); });
    }
    /// A placed stream's tiles: by places, and a piece of a worker's share of the work each, so a
    /// heavy row's pieces spread over the workers.
    template <class Corpus>
    [[nodiscard]] static std::vector<std::size_t> tiles(const Corpus& c, const std::vector<std::uint32_t>& order, const sweep_positions& places,
                                                        std::size_t batch, std::size_t max_pairs, std::size_t align) {
        const class_costs cost(c, order);
        return place_tiles(places, batch, max_pairs, align, cost.piece(workers_for(order.size())), cost);
    }

private:
    class_index m_index;
};

/// The candidate pairs a filtered stream's block holds at most: what it may keep.
inline constexpr std::size_t filtered_block_pairs = std::size_t{1} << 24;
/// How many blocks a filtered stream cuts a worker's share of the offers into: few enough that the
/// reader joins few blocks into a batch, enough to balance the workers. 64, as classes cut theirs,
/// took the stdlib's functions at 0.8 to 37 ms, joining a thousand small blocks; 8, 11 ms.
inline constexpr std::size_t filtered_shares_per_worker = 8;

/// Function rows at a threshold the prefix filter serves, in sweep order: only the partners whose
/// prefix shares a rare subtree with the row's are compared, sorted first so the row's pairs come in
/// the order the sweep visits them (sweep_rows::filtered_row_in_order) — what a stream at a
/// threshold emits. Rows only: a placed stream keeps every pair, which no filter cuts.
class filtered_counts {
public:
    static constexpr unit_kind kind = unit_kind::function;

    /// One worker's buffers: the index's marks, and a row's partners.
    class worker {
    public:
        explicit worker(std::size_t rows) : m_seen(rows, 0) {}
        [[nodiscard]] std::vector<std::uint32_t>& seen() noexcept { return m_seen; }
        [[nodiscard]] std::vector<std::uint32_t>& partners() noexcept { return m_partners; }

    private:
        std::vector<std::uint32_t> m_seen;
        std::vector<std::uint32_t> m_partners;
    };

    template <class Corpus>
    filtered_counts(const Corpus& c, const std::vector<std::uint32_t>& order, double threshold) : m_index(c.units(), order, threshold, parallel_build{}) {}

    /// Row r into `into`, a collector of the index's threshold.
    template <class L>
    void row(const sweep_rows<L>& rows, const std::vector<std::uint32_t>& order, std::size_t r, worker& w, collector& into) const {
        rows.filtered_row_in_order(m_index, order, r, w.seen(), w.partners(), into);
    }
    /// A stream's blocks: a piece of a worker's share of the rows' offers (token_index::offers) each,
    /// and at most filtered_block_pairs candidates, which bound what a block keeps. Cut by candidates
    /// alone, as a counted stream is, 512K a block made Lib/test's functions at 0.8 25,000 blocks of
    /// little work (97 ms, the ranking 37), and 16M a block left the stdlib's at 0.5 a few heavy ones
    /// (59 ms where 512K took 32). Cut so: 33 and 31 ms (2026-10-29, 16 threads).
    template <class Corpus>
    [[nodiscard]] std::vector<std::size_t> blocks(const Corpus&, const std::vector<std::uint32_t>& order, std::size_t) const {
        std::vector<double> offers(order.size());
        double total = 0.0;
        for (std::size_t r = 0; r < offers.size(); ++r) total += offers[r] = static_cast<double>(m_index.offers(r));
        const double piece = total / static_cast<double>(filtered_shares_per_worker * std::max<std::size_t>(workers_for(order.size()), 1));
        return sweep_blocks(order.size(), piece, filtered_block_pairs, [&](std::size_t r) { return offers[r]; });
    }

private:
    token_index m_index;
};

/// f(std::type_identity<Counts>{}) for the counting of `kind`'s rows: the one place a query turns
/// its kind into a type.
template <class F>
decltype(auto) with_counts(unit_kind kind, F&& f) {
    if (kind == unit_kind::function) return f(std::type_identity<function_counts>{});
    return f(std::type_identity<class_counts>{});
}

// ── sweeps ─────────────────────────────────────────────────────────────────

/// The fewest candidate pairs a block of an unlimited function sweep holds: smaller blocks cost
/// more to hand out than they spread.
inline constexpr std::size_t min_block_pairs = std::size_t{1} << 16;

/// The sweep on every core, its rows counted by `Counts`, as sorted parts whose union, ranked and
/// cut to the limit, is exactly c.pairs(Counts::kind, threshold, min_size, limit).
///
/// Without a limit, function rows are cut into about two blocks per worker of equal candidate
/// count, each swept into a collector sized once (exactly, at threshold 0) and sorted by the worker
/// that swept it, while still in cache; class rows are claimed one by one, heaviest first
/// (unlimited_rows). With a limit, rows are claimed one by one, lightest first, one collector per
/// worker, so a limit's floor rises over all of a worker's rows and cuts them — exact copies raise
/// it at once (heaviest first, Lib/test's top 200 classes took 333 ms, not 28). Each collector is
/// cut and sorted in its own thread.
///
/// Classes are counted with a limit too. Merged, since a floor that rises at once cuts most merges,
/// until the floor turned out not to rise on most corpora: the stdlib's top 2,000 classes took
/// 432 ms merged, 23 counted; its top 200 at 0.8, 107 and 8.5. Lib/test's top 200, where exact
/// copies raise it at once, lost 19 → 28 ms (2026-10-29, Ryzen 7 5800X).
template <class Counts, class Corpus>
[[nodiscard]] sorted_parts sweep(const Corpus& c, const std::vector<std::uint32_t>& order, double threshold, std::size_t limit) {
    const std::size_t n = order.size();
    const std::size_t threads = sweep_workers(n);
    const Counts counts(c, order, threshold);
    const sweep_rows rows(c);
    std::vector<std::optional<typename Counts::worker>> workers(threads);   // made on a worker's first row
    auto row = [&](std::size_t r, std::size_t worker, collector& into) {
        if (!workers[worker]) workers[worker].emplace(n);
        counts.row(rows, order, r, *workers[worker], into);
    };
    if (limit == 0 && Counts::unlimited == unlimited_rows::equal_blocks) {
        const std::size_t pairs = n < 2 ? 0 : n * (n - 1) / 2;
        const std::vector<std::size_t> bounds = sweep_blocks(n, std::max(min_block_pairs, pieces(pairs, 2 * threads)));
        sorted_parts parts(bounds.size() - 1);
        for_each(parts.size(), threads, [&](std::size_t b, std::size_t worker) {
            collector into(threshold, 0);
            if (threshold == 0.0) into.reserve(candidates(n, bounds[b], bounds[b + 1]));
            for (std::size_t r = bounds[b]; r < bounds[b + 1]; ++r) row(r, worker, into);
            parts[b] = Corpus::finish(std::move(into));
        });
        return parts;
    }
    std::vector<collector> parts;
    parts.reserve(threads);
    for (std::size_t t = 0; t < threads; ++t) parts.emplace_back(threshold, limit);
    const bool heaviest_first = limit == 0 && Counts::unlimited == unlimited_rows::heaviest_first;
    for_each(n, threads, [&](std::size_t k, std::size_t worker) { row(heaviest_first ? n - 1 - k : k, worker, parts[worker]); });
    sorted_parts sorted(threads);
    for_each(threads, threads, [&](std::size_t t, std::size_t) { sorted[t] = Corpus::finish(std::move(parts[t])); });
    return sorted;
}

/// The threshold from which pairs() of functions goes through the prefix filter. Below it a
/// prefix holds most of a unit's tokens, and the filter costs more than the comparisons it saves.
inline constexpr double prefix_filter_from = 0.5;

/// The rows a filtered sweep hands a worker at once.
inline constexpr std::size_t filtered_block_rows = 64;

/// sweep() for functions at a fixed threshold through a token_index (core.h): the same pairs (the
/// core proves it, prefix_filter_law), with only the partners that share a rare subtree compared.
/// The index is built once; rows are claimed in blocks, one collector and one array of marks per
/// worker; each collector is sorted in its own thread. With a `limit` each worker keeps only its
/// best `limit`: their union holds the whole ranking's best `limit` (5,000 identical functions kept
/// 12.5M pairs, 455 MiB, to return 1,000 before it did).
template <class Corpus>
[[nodiscard]] sorted_parts filtered_sweep(const Corpus& c, const std::vector<std::uint32_t>& order, double threshold, std::size_t limit = 0) {
    const std::size_t n = order.size();
    const token_index index(c.units(), order, threshold, parallel_build{});
    const std::size_t threads = sweep_workers(n);
    const sweep_rows rows(c);
    std::vector<collector> parts;
    parts.reserve(threads);
    for (std::size_t t = 0; t < threads; ++t) parts.emplace_back(threshold, limit);
    std::vector<std::vector<std::uint32_t>> seen(threads);
    for_each(pieces(n, filtered_block_rows), threads, [&](std::size_t b, std::size_t worker) {
        if (seen[worker].empty()) seen[worker].assign(n, 0);
        for (std::size_t r = b * filtered_block_rows; r < std::min(n, (b + 1) * filtered_block_rows); ++r) rows.filtered_row(index, order, r, seen[worker], parts[worker]);
    });
    sorted_parts sorted(threads);
    for_each(threads, threads, [&](std::size_t t, std::size_t) { sorted[t] = Corpus::finish(std::move(parts[t])); });
    return sorted;
}

/// The functions from which a limit goes down the filter's ladder: below it the sweep, cut by its
/// rising floor, costs less than an index per step (427 functions: 20 ms down the ladder, 2 ms swept).
inline constexpr std::size_t ladder_from = 4096;
/// The thresholds a limit tries, from the top: each a filtered sweep, until one finds the limit.
inline constexpr std::array<double, 7> ladder{1.0, 0.95, 0.9, 0.8, 0.7, 0.6, prefix_filter_from};

/// pairs()' sweep of `kind` by the fastest exact way — the query's plan: for functions at a fixed
/// threshold the prefix filter; for functions with a limit, the filter at falling thresholds
/// (ladder) until one finds `limit` pairs — the best `limit` of the whole ranking are then among
/// them, since every pair at or above a threshold ranks above every pair below it — and the sweep
/// when none does; for classes the sweep.
template <class Corpus>
[[nodiscard]] sorted_parts best_pairs(const Corpus& c, const std::vector<std::uint32_t>& order, double threshold, std::size_t limit, unit_kind kind) {
    if (kind == unit_kind::function && limit == 0 && threshold >= prefix_filter_from) return filtered_sweep(c, order, threshold);
    if (kind == unit_kind::function && limit != 0 && order.size() >= ladder_from) {
        for (const double t : ladder) {
            if (t < threshold) break;
            sorted_parts parts = filtered_sweep(c, order, t, limit);
            std::size_t found = 0;   // a worker holding `limit` found as many; the others hold all they found
            for (const auto& p : parts) found += p.size();
            if (found >= limit || t == threshold) return parts;   // ranked() cuts the union to the limit
        }
    }
    return with_counts(kind, [&]<class Counts>(std::type_identity<Counts>) { return sweep<Counts>(c, order, threshold, limit); });
}

/// How a stream runs. At threshold 0 every pair is kept, so each pair's place in the result is
/// known before it is scored: it is written there, in tiles, with no collector (placed). With a
/// threshold, which pairs are kept is known only once they are scored: rows are swept in blocks
/// into collectors, and the reader joins the blocks into batches (collected).
enum class stream_plan : std::uint8_t { placed, collected };
template <stream_plan P>
using plan_tag = std::integral_constant<stream_plan, P>;

/// f(plan_tag<P>{}, std::type_identity<Counts>{}) for a stream of `kind` at `threshold`: its plan,
/// and its rows' counting — for functions from prefix_filter_from the prefix filter, in sweep order.
/// Counted every token with every partner, Lib/test's functions streamed at 0.8 took 329 ms where
/// the filtered ranking took 37 (2026-10-29).
template <class F>
decltype(auto) with_stream(unit_kind kind, double threshold, F&& f) {
    if (threshold == 0.0) return with_counts(kind, [&](auto counts) { return f(plan_tag<stream_plan::placed>{}, counts); });
    if (kind == unit_kind::function && threshold >= prefix_filter_from) return f(plan_tag<stream_plan::collected>{}, std::type_identity<filtered_counts>{});
    return with_counts(kind, [&](auto counts) { return f(plan_tag<stream_plan::collected>{}, counts); });
}

// ── the ranking ────────────────────────────────────────────────────────────

/// Where global rank `r` falls in sorted `parts`: positions p[i] with
/// Σ p[i] == r such that every pair before some p[i] precedes (before())
/// every pair at or after any p[j]. Exists and is unique because before()
/// is a total order. Bisection on the largest open range; each step counts
/// the pivot's rank with one binary search per part.
/// Cost: O(k² log² n) for k parts — nothing next to the data.
[[nodiscard]] inline std::vector<std::size_t> select(const sorted_parts& parts, std::size_t r) {
    const std::size_t k = parts.size();
    std::vector<std::size_t> lo(k, 0), hi(k);
    for (std::size_t i = 0; i < k; ++i) hi[i] = parts[i].size();
    for (;;) {
        std::size_t j = k;
        for (std::size_t i = 0; i < k; ++i) {
            if (lo[i] < hi[i] && (j == k || hi[i] - lo[i] > hi[j] - lo[j])) j = i;
        }
        if (j == k) return lo;
        const match& pivot = parts[j][lo[j] + (hi[j] - lo[j]) / 2];
        std::vector<std::size_t> rank(k);   // pairs preceding the pivot, per part
        std::size_t total = 0;
        for (std::size_t i = 0; i < k; ++i) {
            rank[i] = static_cast<std::size_t>(std::lower_bound(parts[i].begin(), parts[i].end(), pivot, before) - parts[i].begin());
            total += rank[i];
        }
        if (total < r) {   // the pivot and everything before it are in the prefix
            for (std::size_t i = 0; i < k; ++i) lo[i] = std::max(lo[i], rank[i] + (i == j ? 1 : 0));
        } else {           // the pivot and everything after it are in the suffix
            for (std::size_t i = 0; i < k; ++i) hi[i] = std::min(hi[i], rank[i]);
        }
    }
}

[[nodiscard]] inline std::size_t cut_size(const std::vector<std::size_t>& from, const std::vector<std::size_t>& to) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < from.size(); ++i) n += to[i] - from[i];
    return n;
}

namespace detail {

// One slice's next pair in a k-way merge: its front, and the end of the slice.
class merge_head {
public:
    constexpr merge_head(const match* at, const match* end) noexcept : m_at(at), m_end(end) {}

    [[nodiscard]] constexpr const match& front() const noexcept { return *m_at; }
    // Moves past the front; false once the slice is spent.
    constexpr bool advance() noexcept { return ++m_at != m_end; }

private:
    const match* m_at;
    const match* m_end;
};
static_assert([] {
    const match slice[2]{};
    merge_head h(slice, slice + 2);
    return &h.front() == slice && h.advance() && !h.advance();
}());

}  // namespace detail

/// The pairs a ranked batch is merged into at once before its columns are written from them: 96 KB,
/// in a worker's L2 while it writes; a multiple of 8, so each chunk starts on a byte of the bit
/// column. Merged whole into one copy of the batch first, every column read that copy back from
/// memory: 12 passes over 24 MB a batch.
inline constexpr std::size_t merge_chunk = 4096;

/// The ranking of sorted `parts`, cut to `limit` (0: none), in batches of `batch_size`:
/// build(b, n, chunks) is called for batch b of n pairs, and chunks(write) calls write(at, first, k)
/// for the batch's ranked pairs in turn, k at a time (merge_chunk at most) from place `at`. Batch
/// boundaries are global ranks (select()), so every batch is merged from its own slices of the parts
/// independently — all batches on all threads at once, and no merged copy of the whole is ever
/// made. Returns the batch count. The batches are the same whatever the number of parts.
/// Cost: O(n log k) comparisons for n pairs in k parts, spread over threads.
template <class Build>
std::size_t ranked(const sorted_parts& parts, std::size_t limit, std::size_t batch_size, Build&& build) {
    std::size_t total = 0;
    for (const auto& p : parts) total += p.size();
    if (limit != 0 && total > limit) total = limit;
    const std::size_t batches = pieces(total, batch_size);
    std::vector<std::vector<std::size_t>> cut(batches + 1);
    for_each(batches + 1, workers_for(batches + 1), [&](std::size_t b, std::size_t) {
        cut[b] = select(parts, std::min(b * batch_size, total));
    });
    const std::size_t workers = workers_for(batches);
    std::vector<std::vector<detail::merge_head>> heaps(workers);   // each worker's, batch after batch
    std::vector<std::vector<match>> chunks(workers);
    for_each(batches, workers, [&](std::size_t b, std::size_t worker) {
        // k-way merge of this batch's slices: a binary heap of the heads
        std::vector<detail::merge_head>& heap = heaps[worker];
        heap.clear();
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (cut[b][i] < cut[b + 1][i]) heap.emplace_back(parts[i].data() + cut[b][i], parts[i].data() + cut[b + 1][i]);
        }
        auto later = [](const detail::merge_head& l, const detail::merge_head& r) { return before(r.front(), l.front()); };   // min-heap by before()
        std::make_heap(heap.begin(), heap.end(), later);
        std::vector<match>& chunk = chunks[worker];
        chunk.reserve(merge_chunk);
        build(b, cut_size(cut[b], cut[b + 1]), [&](auto&& write) {
            for (std::size_t at = 0; !heap.empty(); at += chunk.size()) {
                chunk.clear();
                while (!heap.empty() && chunk.size() < merge_chunk) {
                    std::pop_heap(heap.begin(), heap.end(), later);
                    detail::merge_head& h = heap.back();
                    chunk.push_back(h.front());
                    if (h.advance()) std::push_heap(heap.begin(), heap.end(), later);
                    else heap.pop_back();
                }
                write(at, chunk.data(), chunk.size());
            }
        });
    });
    return batches;
}

}  // namespace pygim::similarity::sweeps
