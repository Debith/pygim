#pragma once
// core/primitives/parallel/task_pool.h — tasks that make more tasks (a walk that finds directories as it
// lists them), on as many threads as the work rewards.
//
// CORE layer: pybind-free.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "../timestamp.h"
#include "crew.h"
#include "meter.h"
#include "width.h"

namespace pygim::primitives::parallel {

// ── a pool for work found as it is done ──────────────────────────────────

/// Tasks that make more tasks — a walk finds directories as it lists them — run on as many
/// threads as the work rewards: run(task, add) does one, and add(task) queues another. The pool
/// is done when nothing is queued and nothing runs. Its threads grow as ordered_pool's do (the
/// width policy, each task's cost by Clock, the share of it on a CPU by CpuClock), with what is
/// queued as the rest of the work. Tasks are handed out first in, first out through one lock: a
/// walk of 100,000 files in 1,000 directories still scaled 7.8x to 32 threads that way (#274);
/// per-thread queues with stealing are the next step for trees much larger.
///
///     task_pool pool([&](dir_task t, const auto& add) { for (auto& sub : list(t)) add(sub); }, width);
///     pool.submit(root);
///     pool.run_here();                    // the caller works too, until every task is done
///     pool.run_all([&] { scope.finish(); });   // or workers only, telling the caller when done
template <class Task, class Run, width_policy Width = chosen_width, clock_source Clock = monotonic_clock,
          clock_source CpuClock = thread_cpu_clock>
class task_pool {
public:
    /// What a running task is given to add tasks with.
    class adder {
    public:
        explicit adder(task_pool* pool) noexcept : m_pool(pool) {}
        void operator()(Task task) const { m_pool->submit(std::move(task)); }

    private:
        task_pool* m_pool;
    };

    task_pool(Run run, Width width, Clock clock = Clock{}, CpuClock cpu_clock = CpuClock{})
        : m_run(std::move(run)), m_width(std::move(width)), m_meter(clock, cpu_clock) {}
    /// Stops the workers and waits for them: a task already running finishes, no other starts.
    ~task_pool() { stop(); }
    task_pool(const task_pool&) = delete;
    task_pool& operator=(const task_pool&) = delete;

    /// Queues a task: before a run, or from a running task through its adder.
    void submit(Task task) {
        {
            std::lock_guard guard(m_lock);
            m_queue.push_back(std::move(task));
            ++m_pending;
        }
        m_wake.notify_one();
    }

    /// Runs every task, the caller among the workers — the caller alone when no other thread can
    /// start — and returns when none is left; a task's exception is rethrown here once the others
    /// have finished.
    void run_here() {
        try {
            grow_to(m_width.at_start());
        } catch (const std::system_error&) {
        }   // best effort: the caller works, whatever starts beside it
        work();
        rethrow_if_failed();
    }

    /// Runs every task on workers, none in the caller, and calls `finished()` — which must not
    /// throw, and may call rethrow_if_failed() — from the worker that ends the last one (at once
    /// when there is none). Returns at once: the caller waits as it likes, then calls
    /// rethrow_if_failed(), or destroys the pool to stop it. std::system_error when not one worker
    /// can start.
    void run_all(std::function<void()> finished) {
        bool nothing = false;
        {
            std::lock_guard guard(m_lock);
            m_caller_works = false;
            m_finished = std::move(finished);
            nothing = m_pending == 0;
        }
        if (nothing) {
            m_finished();   // outside the lock, which it may take
            return;
        }
        try {
            grow_to(m_width.at_start());
        } catch (const std::system_error&) {
            if (m_workers_started.load(std::memory_order_relaxed) == 0) throw;   // fewer workers do the same work; none, none of it
        }
    }

    /// A task's exception, rethrown: the first one, if any task threw.
    void rethrow_if_failed() const {
        std::lock_guard guard(m_lock);
        if (m_error) std::rethrow_exception(m_error);
    }

private:
    // One worker (or the caller, under run_here): tasks until none is queued or running. Whatever
    // escapes a task's bookkeeping (a lock that cannot be taken, say) becomes the pool's error and
    // ends the run for everyone, so no caller waits on tasks that will never end.
    void work() {
        try {
            for (;;) {
                std::optional<Task> task;
                {
                    std::unique_lock guard(m_lock);
                    m_wake.wait(guard, [&] { return m_stopping || !m_queue.empty() || m_pending == 0; });
                    if (m_stopping || m_queue.empty()) return;   // stopped, or every task done
                    task.emplace(std::move(m_queue.front()));
                    m_queue.pop_front();
                }
                run_one(std::move(*task));
            }
        } catch (...) {
            {
                std::lock_guard guard(m_lock);
                if (!m_error) m_error = std::current_exception();
                m_stopping = true;
            }
            m_wake.notify_all();
            if (m_finished) m_finished();
        }
    }

    // One task, timed; then the count of what is left, and maybe more workers.
    void run_one(Task&& task) {
        try {
            m_meter.timed(m_started.fetch_add(1, std::memory_order_relaxed), [&] { m_run(std::move(task), adder(this)); });
        } catch (...) {
            std::lock_guard guard(m_lock);
            if (!m_error) m_error = std::current_exception();
        }
        m_meter.count_done();
        bool all_done = false;
        std::size_t queued = 0;
        {
            std::lock_guard guard(m_lock);
            all_done = --m_pending == 0;   // the tasks this one added were counted before it ends
            queued = m_queue.size();
        }
        if (all_done) {
            m_wake.notify_all();
            if (m_finished) m_finished();   // run_all's caller wakes
            return;
        }
        m_meter.consider(m_width, queued, [this](std::size_t w) { grow_to(w); });
    }

    // Workers until `threads` threads run, the caller counted as one under run_here; nothing to
    // lock for when as many run already, as after the first few tasks they do.
    void grow_to(std::size_t threads) {
        const std::size_t caller = m_caller_works ? 1 : 0;
        if (threads <= m_workers_started.load(std::memory_order_relaxed) + caller) return;
        std::lock_guard guard(m_spawn);
        class counted {   // those that did start, even when the next cannot
        public:
            explicit counted(task_pool& pool) noexcept : m_pool(pool) {}
            ~counted() { m_pool.m_workers_started.store(m_pool.m_workers.size(), std::memory_order_relaxed); }

        private:
            task_pool& m_pool;
        } after(*this);
        const signals_blocked quiet;   // inherited by the workers started here
        while (!m_stopping_spawn && m_workers.size() + caller < threads) {
            m_workers.emplace_back([this] { name_this_thread("pygim-pool"); work(); });
        }
    }

    void stop() noexcept {
        {
            std::lock_guard spawn(m_spawn);
            m_stopping_spawn = true;
        }
        {
            std::lock_guard guard(m_lock);
            m_stopping = true;
        }
        m_wake.notify_all();
        std::vector<std::jthread> workers;
        {
            std::lock_guard spawn(m_spawn);
            workers = std::move(m_workers);
        }
    }   // joined here

    Run m_run;
    Width m_width;
    task_meter<Clock, CpuClock> m_meter;
    mutable std::mutex m_lock;                    // the queue, the count of what is left, the first error
    std::condition_variable m_wake;
    std::deque<Task> m_queue;
    std::size_t m_pending = 0;                    // queued and running
    bool m_stopping = false;
    bool m_caller_works = true;
    std::function<void()> m_finished;
    std::exception_ptr m_error;
    std::atomic<std::size_t> m_started{0};        // tasks started: which the meter samples
    std::mutex m_spawn;                           // the workers vector
    bool m_stopping_spawn = false;
    std::atomic<std::size_t> m_workers_started{0};
    std::vector<std::jthread> m_workers;
};

}  // namespace pygim::primitives::parallel
