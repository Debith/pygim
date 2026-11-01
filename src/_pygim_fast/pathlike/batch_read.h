#pragma once
// pathlike/batch_read.h — read many files as one job, on as many threads as it rewards, each
// result handed out in member order.
//
//     batch_read job(ps.paths(), elastic_width(cores, elastic_cap(cores)), reading_ahead());
//     while (!job.exhausted()) use(job.next());       // file 0's result, then file 1's, ...
//     std::vector<read_result> all = job.collect();    // or every result at once
//
// A worker reads a file (file_reads.h) and puts the read through the Stage: keep_bytes hands it on
// as it is; a parser would turn it into its tree there, off the GIL, while other workers wait on the
// disk. The workers, their number and the window they keep to are parallel::ordered_pool's
// (utils/parallel.h, with the measurements behind the shape): the caller reads too while it is
// alone, threads are added while the rest of the job is worth them, and memory stays bounded.
//
// CORE layer: pybind-free. A job owns what it reads — or shares it with its caller, who names a file
// from it after the job is gone — and never touches a path table, so its caller may release the GIL
// around next() and collect(): the table has no lock; the GIL serialises its writers.

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "../utils/parallel.h"
#include "../utils/timestamp.h"
#include "core.h"
#include "file_reads.h"

namespace pygim::pathlike {

using parallel::chosen_width;
using parallel::elastic_cap;
using parallel::elastic_width;
using parallel::fixed_width;
using parallel::window_limits;

/// How far readers run ahead of their consumer unless told: this many files, holding about
/// `bytes` between them (64 MiB by default) — enough to keep a disk busy while the consumer works.
inline constexpr std::size_t files_ahead = 1'024;
inline constexpr std::size_t default_bytes_ahead = std::size_t{64} << 20;
[[nodiscard]] constexpr window_limits reading_ahead(std::size_t bytes = default_bytes_ahead) noexcept { return window_limits(files_ahead, bytes); }
static_assert(reading_ahead().slots_for(5'000) == files_ahead && reading_ahead().admits(default_bytes_ahead - 1) && !reading_ahead(0).admits(0));

/// What a worker does with each read: anything that takes the path and its read_result.
template <class S>
concept read_stage = requires(const S& stage, os_path path, read_result&& got) { stage(path, std::move(got)); };

/// Hands each read on as it is.
class keep_bytes {
public:
    [[nodiscard]] constexpr read_result operator()(os_path, read_result&& got) const noexcept { return std::move(got); }
};
static_assert(read_stage<keep_bytes> && [] {
    const std::string x = "x";
    return std::holds_alternative<not_a_file>(keep_bytes{}(x, read_result(not_a_file(ENOENT))));
}());

/// A path_list shared by a job and its caller: the caller names a failing file from it once the job is gone.
using shared_paths = std::shared_ptr<const path_list>;

/// The job: which files, read how, put through what, on how many threads.
template <file_reads Reads = native_reads, read_stage Stage = keep_bytes, parallel::width_policy Width = chosen_width,
          clock_source Clock = monotonic_clock>
class basic_batch_read {
public:
    using result_type = std::invoke_result_t<const Stage&, os_path, read_result&&>;

    basic_batch_read(shared_paths paths, Width width, window_limits limits, Reads reads = Reads{}, Stage stage = Stage{},
                     Clock clock = Clock{})
        : m_paths(std::move(paths)), m_reads(std::move(reads)), m_stage(std::move(stage)),
          m_pool(m_paths->size(), task{this}, std::move(width), limits, weight{}, clock) {}
    basic_batch_read(path_list paths, Width width, window_limits limits, Reads reads = Reads{}, Stage stage = Stage{}, Clock clock = Clock{})
        : basic_batch_read(std::make_shared<const path_list>(std::move(paths)), std::move(width), limits, std::move(reads), std::move(stage),
                           clock) {}

    [[nodiscard]] std::size_t size() const noexcept { return m_paths->size(); }
    [[nodiscard]] os_path path_at(std::size_t i) const { return m_paths->at(i); }

    /// Reads every file on worker threads, none in the caller, and calls `finished()` once the
    /// last one is read; then collect() hands the results over at once. Needs a window that holds
    /// every result (window_limits(size(), ...)). Destroying the job stops it: a read in hand
    /// finishes, no other starts.
    template <class Finished>
    void run_all(Finished finished) { m_pool.run_all(std::move(finished)); }

    /// Whether every file's result has been handed out.
    [[nodiscard]] bool exhausted() const noexcept { return m_pool.exhausted(); }
    /// Whether next() would hand a result over at once, with nothing to read or wait for.
    [[nodiscard]] bool ready() const noexcept { return m_pool.ready(); }
    /// The next file's result, in member order.
    [[nodiscard]] result_type next() { return m_pool.next(); }
    /// Every remaining result, in member order.
    [[nodiscard]] std::vector<result_type> collect() {
        std::vector<result_type> out;
        out.reserve(size());
        while (!exhausted()) out.push_back(next());
        return out;
    }

private:
    // File j read, then put through the stage: one task of the pool.
    class task {
    public:
        explicit task(const basic_batch_read* job) noexcept : m_job(job) {}
        [[nodiscard]] result_type operator()(std::size_t j) const {
            const os_path path = m_job->path_at(j);
            read_result got = m_job->m_reads.read(path);
            return m_job->m_stage(path, std::move(got));
        }

    private:
        const basic_batch_read* m_job;
    };
    // Bytes weigh what they hold; a stage's own results count only against the window's length.
    using weight = std::conditional_t<std::is_same_v<result_type, read_result>, read_weight, parallel::unweighed>;

    shared_paths m_paths;   // never empty
    Reads m_reads;
    Stage m_stage;
    parallel::ordered_pool<task, Width, weight, Clock> m_pool;   // last: its workers stop before what they read goes
};
using batch_read = basic_batch_read<>;

}  // namespace pygim::pathlike
