#pragma once
// pathlike/parallel_glob.h — glob on several threads: the steps of glob_walk.h run on a
// parallel::task_pool, each directory a task, so the listings that make up a walk overlap.
//
//     parallel_glob walk(file("src"), "**/*.py", width);
//     walk.run_all([&] { scope.finish(); });   // or walk.run_here(): the caller works too
//     std::vector<file> found = walk.results(); // sorted, deduplicated: what file::glob returns
//
// A glob that may run long starts on its caller's thread (glob_walk) and comes here only if it is
// still going after glob_head_start directories, or fans out to more than that waiting: a pool, and
// owning the signals for its run, cost about 0.1 ms, five times a small glob's whole walk; a wide
// tree's first directories walked alone cost it 9% (2026-10-30).
//
// The rules are glob_step's, the same that file::glob runs on one thread, so the two give the same
// paths. Measured on CPython's Lib (192 directories): one thread 2.5 ms, 8-16 threads 1.1 ms; on
// the 9p mount /mnt/c 256 ms against 61-64 ms; glob before it went to readdir and threads, 10.4 ms
// and 522 ms (#274).
//
// CORE layer: pybind-free. A walk touches no path table; its results become files at the end.

#include <cstddef>
#include <functional>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "../utils/parallel.h"
#include "core.h"
#include "glob_walk.h"

namespace pygim::pathlike {

/// How many directories a glob lists on its caller's thread, and lets wait, before a walk still going
/// moves to a pool.
inline constexpr std::size_t glob_head_start = 8;

/// One glob of `pattern` under `root`, on as many threads as `Width` gives it.
template <parallel::width_policy Width = parallel::chosen_width>
class basic_parallel_glob {
public:
    basic_parallel_glob(const file& root, std::string_view pattern, Width width) : basic_parallel_glob(root, glob_pattern(pattern), std::move(width)) {}
    /// The same with the pattern parsed already.
    basic_parallel_glob(const file& root, glob_pattern pattern, Width width)
        : m_root(root), m_pattern(std::move(pattern)), m_pool(stepper(this), std::move(width)) {
        m_pool.submit(glob_task::first(m_pattern, m_root.to_fs()));
    }
    /// The rest of a walk `started` on the caller's thread with `pattern`: what it found, and its
    /// directories left as the first tasks.
    basic_parallel_glob(const file& root, glob_pattern pattern, Width width, glob_walk&& started)
        : m_root(root), m_pattern(std::move(pattern)), m_found(std::move(started).found()), m_pool(stepper(this), std::move(width)) {
        for (glob_task& task : std::move(started).left()) m_pool.submit(std::move(task));
    }

    /// The walk with the caller among the workers; returns when it is done.
    void run_here() { m_pool.run_here(); }
    /// The walk on workers only; `finished()` is called from the one that ends it.
    void run_all(std::function<void()> finished) { m_pool.run_all(std::move(finished)); }

    /// What the walk found, once it is done: sorted and deduplicated, as file::glob returns it.
    [[nodiscard]] std::vector<file> results() const {
        m_pool.rethrow_if_failed();
        return m_root.found_by_glob(m_found);
    }

private:
    // One directory listed and matched; its matches added to the results at once, under one lock.
    class stepper {
    public:
        explicit stepper(basic_parallel_glob* walk) noexcept : m_walk(walk) {}
        template <class Add>
        void operator()(glob_task&& task, const Add& add) const {
            std::vector<fs::path> mine;
            glob_step(m_walk->m_pattern, task, [&](fs::path p) { mine.push_back(std::move(p)); }, [&](glob_task t) { add(std::move(t)); });
            if (mine.empty()) return;
            std::lock_guard guard(m_walk->m_found_lock);
            for (fs::path& p : mine) m_walk->m_found.push_back(std::move(p));
        }

    private:
        basic_parallel_glob* m_walk;
    };

    file m_root;
    glob_pattern m_pattern;
    std::mutex m_found_lock;
    std::vector<fs::path> m_found;
    parallel::task_pool<glob_task, stepper, Width> m_pool;   // last: its workers stop before what they use goes
};
using parallel_glob = basic_parallel_glob<>;

}  // namespace pygim::pathlike
