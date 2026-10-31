#pragma once
// similarity/adapter/stream.h — the all-pairs sweep as an Arrow stream: batches
// produced while the sweep runs, in bounded memory, deterministic for any
// thread count.
//
// ADAPTER layer (Arrow, std::thread; no Python). A ranked result needs every
// pair before the first can be emitted; a stream does not rank. It emits the
// pairs in SWEEP order (sweep_order()'s rows, each row's partners lightest
// first — the order corpus::pairs() visits them) and cuts them into batches
// of exactly `batch_size` rows (the last one shorter), so the same index and
// arguments give the same batches every time.
//
// Two pipelines, one order (sweeps::with_stream picks one), each instantiated with the counting
// of its kind's rows (sweeps::function_counts, sweeps::class_counts):
//
// placed_pair_stream — threshold 0, every pair kept (the 100M+ row case), of
// functions or classes. Each pair's place in the result is known before it is
// scored (sweep_positions), so the result is cut into tiles (place_tiles) sized
// to a worker's L2 (tile_pairs), each inside one batch and starting on a whole
// byte of a bit column; a class tile also holds no more than 1/64 of a worker's
// share of the work (class_costs), so a heavy class's row is cut into pieces for
// several workers. Workers claim tiles in order, take each piece's shared counts
// from an index — every token for functions; every class's and method's token
// for classes, and each pair's method assignment (Counts::piece) — and
// write them straight into their batch's Arrow columns at their places; a batch
// is handed over the moment its last tile is written — no collector, no copy, no
// join. At most tiles_ahead() tiles run past the batch being read, so it is read
// from the L3.
//
// pair_stream — a threshold: the rows are split into blocks of 512K candidates,
// a class block closed at 1/64 of a worker's share of the work too
// (class_counts::blocks); function rows from 0.5 up are filtered, in sweep order
// (filtered_counts); workers claim blocks in order, sweep them into private
// collectors and build the block's columns; ReadNext() appends blocks in order
// and cuts batches off the front, joining blocks where a batch spans them.
//
//     auto reader = std::make_shared<placed_pair_stream<C, State, sweeps::function_counts>>(...);
//     while (reader->ReadNext(&batch), batch) { ... }      // or hand it to pyarrow
//
// Measured 2026-10-27 (Ryzen 7 5800X, 16 threads), CPython's stdlib, 127M
// function pairs, columns a, b, shared, total, drained through pyarrow: the
// collector pipeline with 512K-pair blocks 1,203-1,227 ms, the placed one
// 221-239 ms (docs/design/similarity.md's appendix); each step between them
// in a C++ harness in docs/design/vectorisation.md ("Caches").
//
// Lifetime and safety: a stream owns the index's shared state (index_state.h),
// so the corpus outlives the stream even if the Index does not. Every block or
// tile is swept through the state's read_at: under its shared lock, and only
// while the corpus is still at the generation the stream opened at — an add()
// after the stream opened fails the stream (Status::Invalid naming the cause)
// instead of mixing two corpora, and no worker reads a corpus an add() is
// growing. The workers are std::jthreads: the destructor asks them all to stop
// and joins them, as does a constructor that fails part way through starting
// them. Workers wait on one condition variable and the reader on another, so a
// finished block wakes the reader alone and a freed slot one worker. A stream
// read in a forked child, whose workers stayed in the parent, fails at once.

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>


#include <arrow/api.h>
#include <arrow/array/concatenate.h>

#include "../../platform/process.h"
#include "../../utils/cpu.h"
#include "../../utils/parallel.h"
#include "../../utils/pieces.h"
#include "../core.h"
#include "../sweeps.h"
#include "tables.h"

namespace pygim::similarity {

/// What a stream reads its corpus through: the corpus under the shared lock while it is still at
/// the generation the stream opened at (index_state::read_at).
template <class State, class Corpus>
concept stream_source = requires(const State& s, std::uint64_t generation) {
    { s.template read_at<Corpus>(generation, [](const Corpus&) {}) } -> std::same_as<bool>;
};

namespace detail {

inline constexpr const char* index_changed =
    "similarity: the index changed while a stream_pairs() stream was open (add() or add_file() after it was opened); open a new stream";
inline constexpr const char* other_process =
    "similarity: this stream_pairs() stream was opened in another process, before a fork, and its workers stayed there; open a new stream";

/// What both streams' workers and reader share besides their work: one mutex, the condition the
/// workers wait on (room to claim, or a stop), the one the reader waits on (its next piece, or a
/// failure), the first failure, and the process the stream was opened in.
class swept_stream : public arrow::RecordBatchReader {
protected:
    /// Runs a worker's body: whatever escapes it becomes the stream's error, which ReadNext()
    /// reports — a thread must never let an exception out.
    template <class Body>
    void work(Body&& body) noexcept {
        try {
            body();
        } catch (const std::exception& e) {
            fail(std::string("similarity: the sweep failed: ") + e.what());
        } catch (...) {
            fail("similarity: the sweep failed");
        }
    }

    /// The stream failed for `why` (the first reason is kept): every waiter wakes to see it.
    void fail(std::string why) {
        {
            std::lock_guard lock(m_mutex);
            if (m_error.empty()) m_error = std::move(why);
        }
        m_ready.notify_all();
        m_room.notify_all();
    }

    /// A worker's next item of `count` — a block, a tile — claimed under the mutex once `room()`
    /// allows: `next` moves on and `claimed(item)` runs, both under the mutex. None once every item
    /// is claimed, the stream failed, or the worker is asked to stop; the last claim wakes every
    /// waiting worker to see that.
    template <class Room, class Claimed>
    [[nodiscard]] std::optional<std::size_t> claim(const std::stop_token& stop, std::size_t& next, std::size_t count, Room&& room, Claimed&& claimed) {
        std::unique_lock lock(m_mutex);
        m_room.wait(lock, stop, [&] { return !m_error.empty() || next >= count || room(); });
        if (stop.stop_requested() || !m_error.empty() || next >= count) return std::nullopt;
        const std::size_t item = next++;
        claimed(item);
        if (next == count) m_room.notify_all();
        return item;
    }

    /// Whether this is not the process that opened the stream: a fork's child, without its workers.
    [[nodiscard]] bool forked() const noexcept { return platform::process_id() != m_opened_in; }

    std::mutex m_mutex;
    std::condition_variable_any m_room;   // workers: room to claim, or they are asked to stop
    std::condition_variable m_ready;      // the reader: its next piece is done, or the stream failed
    std::string m_error;                  // under m_mutex

private:
    const long m_opened_in = platform::process_id();
};

}  // namespace detail

/// `Corpus` is a corpus<L>; `State` the index's shared state, read through stream_source; `Counts`
/// how its kind's rows are counted (sweeps.h). See the file comment.
template <class Corpus, class State, class Counts>
    requires stream_source<State, Corpus>
class pair_stream final : public detail::swept_stream {
public:
    /// Candidate pairs per block: what a block keeps at a threshold stays small whatever its size,
    /// and fewer blocks mean fewer hand-overs and joins.
    static constexpr std::size_t block_pairs = std::size_t{1} << 19;
    /// Blocks a worker may sweep past the one the reader waits for.
    static constexpr std::size_t blocks_ahead_per_worker = 2;

    /// Opened inside the state's read, under its shared lock: `corpus` is the state's corpus at
    /// `generation`, and `order` and `columns` are snapshots of it.
    pair_stream(std::shared_ptr<const State> state, const Corpus& corpus, std::uint64_t generation,
                std::vector<std::uint32_t> order, double threshold, std::size_t batch_size, tables::pair_columns columns)
        : m_state(std::move(state)), m_generation(generation), m_order(std::move(order)), m_threshold(threshold), m_batch_size(batch_size),
          m_columns(std::move(columns)), m_counts(corpus, m_order, threshold), m_bounds(m_counts.blocks(corpus, m_order, block_pairs)) {
        const std::size_t threads = sweeps::workers_for(blocks());
        m_window = blocks_ahead_per_worker * threads;
        m_workers.reserve(threads);
        const parallel::signals_blocked quiet;   // inherited: Ctrl-C reaches the thread reading the stream
        for (std::size_t t = 0; t < threads; ++t) m_workers.emplace_back([this](std::stop_token stop) { work([&] { work_blocks(stop); }); });
    }

    /// Every worker asked to stop at once; each is joined as m_workers goes.
    ~pair_stream() override {
        for (std::jthread& t : m_workers) t.request_stop();
    }

    pair_stream(const pair_stream&) = delete;
    pair_stream& operator=(const pair_stream&) = delete;

    [[nodiscard]] std::shared_ptr<arrow::Schema> schema() const override { return m_columns.schema(); }

    /// The next batch of `batch_size` pairs in sweep order (shorter only at
    /// the end); nullptr once every pair was emitted. Blocks while the
    /// workers catch up.
    arrow::Status ReadNext(std::shared_ptr<arrow::RecordBatch>* out) override {
        if (forked()) return arrow::Status::Invalid(detail::other_process);
        while (m_pending_rows < m_batch_size && m_emit < blocks()) {   // the next blocks' columns, as their workers built them
            std::shared_ptr<arrow::RecordBatch> block;
            {
                std::unique_lock lock(m_mutex);
                m_ready.wait(lock, [&] { return !m_error.empty() || m_done.count(m_emit) != 0; });
                if (!m_error.empty()) return arrow::Status::Invalid(m_error);
                block = std::move(m_done[m_emit]);
                m_done.erase(m_emit);
                ++m_emit;
            }
            m_room.notify_one();   // a window slot is free
            if (block->num_rows() == 0) continue;
            m_pending_rows += static_cast<std::size_t>(block->num_rows());
            m_pending.push_back(std::move(block));
        }
        const std::size_t n = std::min(m_batch_size, m_pending_rows);
        if (n == 0) {
            *out = nullptr;
            return arrow::Status::OK();
        }
        // n rows from the front: one block's slice (no copy), or slices of several joined
        std::vector<std::shared_ptr<arrow::RecordBatch>> parts;
        for (std::size_t need = n; need > 0;) {
            const std::shared_ptr<arrow::RecordBatch>& front = m_pending.front();
            const auto left = static_cast<std::size_t>(front->num_rows()) - m_offset;
            const std::size_t take = std::min(need, left);
            parts.push_back(front->Slice(static_cast<std::int64_t>(m_offset), static_cast<std::int64_t>(take)));
            need -= take;
            m_offset += take;
            if (m_offset == static_cast<std::size_t>(front->num_rows())) {
                m_pending.pop_front();
                m_offset = 0;
            }
        }
        m_pending_rows -= n;
        if (parts.size() == 1) {
            *out = std::move(parts[0]);
            return arrow::Status::OK();
        }
        std::vector<std::shared_ptr<arrow::Array>> columns;
        for (int c = 0; c < m_columns.schema()->num_fields(); ++c) {
            auto joined = join(parts, c);
            if (!joined.ok()) return joined.status();
            columns.push_back(std::move(*joined));
        }
        *out = arrow::RecordBatch::Make(m_columns.schema(), static_cast<std::int64_t>(n), std::move(columns));
        return arrow::Status::OK();
    }

private:
    // The sweep's blocks (Counts::blocks): of block_pairs pairs at most; a class block cut smaller
    // where its rows' method matrices cost more.
    [[nodiscard]] std::size_t blocks() const noexcept { return m_bounds.size() - 1; }

    // Column c of `parts` as one array. A dictionary column joins its indices only: every piece
    // shares the stream's one dictionary, which Concatenate would compare string by string.
    [[nodiscard]] static arrow::Result<std::shared_ptr<arrow::Array>> join(const std::vector<std::shared_ptr<arrow::RecordBatch>>& parts, int c) {
        const auto& first = parts.front()->column(c);
        arrow::ArrayVector pieces;
        if (first->type_id() != arrow::Type::DICTIONARY) {
            for (const auto& p : parts) pieces.push_back(p->column(c));
            return arrow::Concatenate(pieces);
        }
        for (const auto& p : parts) pieces.push_back(std::static_pointer_cast<arrow::DictionaryArray>(p->column(c))->indices());
        ARROW_ASSIGN_OR_RAISE(auto indices, arrow::Concatenate(pieces));
        const auto& dict = std::static_pointer_cast<arrow::DictionaryArray>(first);
        return std::static_pointer_cast<arrow::Array>(std::make_shared<arrow::DictionaryArray>(dict->type(), indices, dict->dictionary()));
    }

    // A worker: claims the next block while the window allows, sweeps it from the corpus as the
    // stream opened it, and builds its columns.
    void work_blocks(const std::stop_token& stop) {
        std::optional<typename Counts::worker> buffers;   // made on this worker's first block
        while (const std::optional<std::size_t> claimed = claim(stop, m_claim, blocks(), [&] { return m_claim < m_emit + m_window; }, [](std::size_t) {})) {
            const std::size_t block = *claimed;
            if (!buffers) buffers.emplace(m_order.size());
            collector into(m_threshold, 0);
            const bool current = m_state->template read_at<Corpus>(m_generation, [&](const Corpus& c) {
                const sweep_rows rows(c);
                for (std::size_t r = m_bounds[block]; r < m_bounds[block + 1]; ++r) m_counts.row(rows, m_order, r, *buffers, into);
            });
            if (!current) {
                fail(detail::index_changed);
                return;
            }
            // the block's pairs as the stream's columns, built here and not on the reader: they read
            // the units' columns, a snapshot, so outside the lock
            std::shared_ptr<arrow::RecordBatch> columns = m_columns.batch(into.kept().data(), into.kept().size());
            {
                std::lock_guard lock(m_mutex);
                m_done.emplace(block, std::move(columns));
            }
            m_ready.notify_one();
        }
    }

    std::shared_ptr<const State> m_state;
    std::uint64_t m_generation;
    std::vector<std::uint32_t> m_order;
    double m_threshold;
    std::size_t m_batch_size;
    tables::pair_columns m_columns;
    Counts m_counts;   // the rows' indexes: shared counts without merges
    std::vector<std::size_t> m_bounds;
    std::size_t m_window = 1;

    std::map<std::size_t, std::shared_ptr<arrow::RecordBatch>> m_done;   // finished blocks' columns, not yet emitted (m_mutex)
    std::size_t m_claim = 0;                                             // next block a worker takes (m_mutex)
    std::size_t m_emit = 0;                                              // next block ReadNext appends (m_mutex)

    std::deque<std::shared_ptr<arrow::RecordBatch>> m_pending;   // the reader's: blocks taken, in order; the first from m_offset
    std::size_t m_offset = 0;
    std::size_t m_pending_rows = 0;                              // rows in m_pending not yet emitted
    std::vector<std::jthread> m_workers;                         // last: joined before anything they read goes
};

/// The all-pairs stream at threshold 0, written by places: see the file comment. The same batches,
/// in the same order, as pair_stream gives them (placed_sweep_law and placed_class_law; tested by
/// test_stream_is_the_sweep_in_order and test_the_class_stream_is_the_class_sweep_in_order).
template <class Corpus, class State, class Counts>
    requires stream_source<State, Corpus>
class placed_pair_stream final : public detail::swept_stream {
public:
    /// Where a tile may start within its batch: a multiple of this many places, so no two tiles
    /// write one byte of the `exact` bit column, nor one cache line of a 4- or 8-byte column.
    static constexpr std::size_t tile_alignment = 64;

    /// Opened inside the state's read, under its shared lock: `corpus` is the state's corpus at
    /// `generation`, `order` and `columns` snapshots of it, and the index the pieces count from is
    /// built here — every token for functions, every class's and method's for classes.
    placed_pair_stream(std::shared_ptr<const State> state, const Corpus& corpus, std::uint64_t generation,
                       std::vector<std::uint32_t> order, std::size_t batch_size, tables::pair_columns columns)
        : m_state(std::move(state)), m_generation(generation), m_order(std::move(order)), m_places(m_order.size()), m_counts(corpus, m_order, 0.0),
          m_batch_size(batch_size), m_columns(std::move(columns)), m_tile(sweeps::tile_pairs(this_cpu().caches(), m_columns.row_bytes())) {
        m_tile_first = Counts::tiles(corpus, m_order, m_places, m_batch_size, m_tile, tile_alignment);
        m_batches = pieces(m_places.pairs(), m_batch_size);
        m_batch_first_tile.assign(m_batches + 1, tiles());   // batch b's tiles: [m_batch_first_tile[b], m_batch_first_tile[b + 1])
        for (std::size_t k = tiles(); k-- > 0;) m_batch_first_tile[m_tile_first[k] / m_batch_size] = k;
        const std::size_t threads = sweeps::workers_for(tiles());
        m_ahead = sweeps::tiles_ahead(this_cpu().caches(), m_tile * m_columns.row_bytes(), threads);
        m_workers.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) m_workers.emplace_back([this](std::stop_token stop) { work([&] { work_tiles(stop); }); });
    }

    /// Every worker asked to stop at once; each is joined as m_workers goes.
    ~placed_pair_stream() override {
        for (std::jthread& t : m_workers) t.request_stop();
    }

    placed_pair_stream(const placed_pair_stream&) = delete;
    placed_pair_stream& operator=(const placed_pair_stream&) = delete;

    [[nodiscard]] std::shared_ptr<arrow::Schema> schema() const override { return m_columns.schema(); }

    /// The next batch of `batch_size` pairs in sweep order (shorter only at the end); nullptr once
    /// every pair was emitted. Blocks until the batch's last tile is written.
    arrow::Status ReadNext(std::shared_ptr<arrow::RecordBatch>* out) override {
        if (forked()) return arrow::Status::Invalid(detail::other_process);
        if (m_emit >= m_batches) {
            *out = nullptr;
            return arrow::Status::OK();
        }
        std::unique_ptr<tables::pair_batch> written;
        {
            std::unique_lock lock(m_mutex);
            m_ready.wait(lock, [&] {
                const auto it = m_open.find(m_emit);
                return !m_error.empty() || (it != m_open.end() && it->second.done());
            });
            if (!m_error.empty()) return arrow::Status::Invalid(m_error);
            const auto it = m_open.find(m_emit);
            written = std::move(it->second).release();
            m_open.erase(it);
            ++m_emit;
        }
        m_room.notify_all();   // a batch's worth of tiles may be claimed now
        *out = std::move(*written).finish();
        return arrow::Status::OK();
    }

private:
    // A batch while its tiles are written: its columns once its first tile's worker has made them
    // (outside the mutex), and how many of its tiles are still out.
    class open_batch {
    public:
        explicit open_batch(std::size_t tiles) noexcept : m_left(tiles) {}

        void open(std::unique_ptr<tables::pair_batch> columns) noexcept { m_columns = std::move(columns); }
        [[nodiscard]] bool ready() const noexcept { return m_columns != nullptr; }
        [[nodiscard]] tables::pair_batch& columns() noexcept { return *m_columns; }
        /// One of its tiles is written; whether that was the last.
        bool tile_written() noexcept { return --m_left == 0; }
        [[nodiscard]] bool done() const noexcept { return m_left == 0; }
        [[nodiscard]] std::unique_ptr<tables::pair_batch> release() && noexcept { return std::move(m_columns); }

    private:
        std::unique_ptr<tables::pair_batch> m_columns;
        std::size_t m_left;
    };

    // The tiles (Counts::tiles): L2-sized and inside one batch; a class tile also a piece of a
    // worker's share of the work, so a heavy row's pieces spread over the workers.
    [[nodiscard]] std::size_t tiles() const noexcept { return m_tile_first.size() - 1; }

    // Batch b's pairs: [b · batch_size, its end).
    [[nodiscard]] std::size_t batch_rows(std::size_t b) const noexcept {
        return std::min(m_batch_size, m_places.pairs() - b * m_batch_size);
    }
    // The tiles a worker may claim: the batch being read's, and m_ahead past them.
    [[nodiscard]] std::size_t claimable() const noexcept {
        return std::min(tiles(), m_batch_first_tile[std::min(m_emit + 1, m_batches)] + m_ahead);
    }

    // A worker: claims the next tile while the window allows and writes it from the corpus as the
    // stream opened it.
    void work_tiles(const std::stop_token& stop) {
        typename Counts::worker buffers(m_order.size());
        open_batch* batch = nullptr;
        std::size_t rows = 0;      // the batch's, when this tile opens it
        bool ready = false;        // whether its columns were made already
        auto note = [&](std::size_t tile) {   // under the mutex: the tile's batch, entered by its first tile
            const std::size_t b = m_tile_first[tile] / m_batch_size;
            auto it = m_open.find(b);
            rows = 0;
            if (it == m_open.end()) {
                const std::size_t tiles_of_b = m_batch_first_tile[b + 1] - m_batch_first_tile[b];
                it = m_open.emplace(b, open_batch(tiles_of_b)).first;
                rows = batch_rows(b);
            }
            batch = &it->second;
            ready = batch->ready();
        };
        while (const std::optional<std::size_t> claimed = claim(stop, m_claim, tiles(), [&] { return m_claim < claimable(); }, note)) {
            const std::size_t tile = *claimed;
            if (rows != 0) {   // its first tile: the batch's columns made outside the mutex, then shown to its other tiles
                auto columns = std::make_unique<tables::pair_batch>(m_columns, rows);
                {
                    std::lock_guard lock(m_mutex);
                    batch->open(std::move(columns));
                }
                m_room.notify_all();
            } else if (!ready) {   // another worker is making them
                std::unique_lock lock(m_mutex);
                m_room.wait(lock, stop, [&] { return batch->ready() || !m_error.empty(); });
                if (stop.stop_requested() || !m_error.empty()) return;
            }
            const bool current = m_state->template read_at<Corpus>(m_generation, [&](const Corpus& c) { write(c, tile, batch->columns(), buffers); });
            if (!current) {
                fail(detail::index_changed);
                return;
            }
            bool last;
            {
                std::lock_guard lock(m_mutex);
                last = batch->tile_written();
            }
            if (last) m_ready.notify_one();
        }
    }

    // Tile `tile`: its places, each piece of a row counted for its own partners and written at its
    // place in the batch.
    void write(const Corpus& corpus, std::size_t tile, tables::pair_batch& out, typename Counts::worker& buffers) const {
        const std::size_t p = m_tile_first[tile], q = m_tile_first[tile + 1], first = p / m_batch_size * m_batch_size;
        const std::vector<std::uint64_t>& w = m_counts.weights();
        const sweep_rows rows(corpus);
        m_places.for_each_piece(p, q, [&](std::size_t r, std::size_t from, std::size_t end, std::size_t at) {
            m_counts.piece(rows, m_order, r, from, end, buffers, [&](const auto* shared) {
                out.write(at - first, m_order[r], w[r], m_order.data() + from, w.data() + from, shared, end - from);
            });
        });
    }

    std::shared_ptr<const State> m_state;
    std::uint64_t m_generation;
    std::vector<std::uint32_t> m_order;
    sweep_positions m_places;
    Counts m_counts;   // the rows' indexes: each piece's shared counts
    std::size_t m_batch_size;
    tables::pair_columns m_columns;
    std::size_t m_tile;                     // the pairs a tile holds at most
    std::vector<std::size_t> m_tile_first;  // tile k: places [m_tile_first[k], m_tile_first[k + 1])
    std::size_t m_batches = 0;
    std::vector<std::size_t> m_batch_first_tile;
    std::size_t m_ahead = 4;

    std::map<std::size_t, open_batch> m_open;   // batches with a tile claimed, not yet read (m_mutex)
    std::size_t m_claim = 0;                    // next tile a worker takes (m_mutex)
    std::size_t m_emit = 0;                     // next batch ReadNext hands over (m_mutex)
    std::vector<std::jthread> m_workers;        // last: joined before anything they read goes
};

}  // namespace pygim::similarity
