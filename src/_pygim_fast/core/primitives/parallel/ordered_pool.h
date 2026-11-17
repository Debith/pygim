#pragma once
// core/primitives/parallel/ordered_pool.h — n indexed tasks on as many threads as they reward, their results
// handed to one consumer in index order with at most a window of them waiting.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "../scope_exit.h"
#include "../timestamp.h"
#include "crew.h"
#include "meter.h"
#include "width.h"
#include "window.h"
#include "../../../errors/core.h"

namespace pygim::primitives::parallel {

// ── the pool ─────────────────────────────────────────────────────────────

/// n tasks, task(i) for each i below n, their results handed to one consumer in index order. Clock
/// times the tasks; CpuClock, read on every 16th, says how much of that time was spent computing.
template <class Task, width_policy Width = chosen_width, class Weigh = unweighed, clock_source Clock = monotonic_clock,
          clock_source CpuClock = thread_cpu_clock>
class ordered_pool {
public:
    using result_type = std::invoke_result_t<const Task&, std::size_t>;

    ordered_pool(std::size_t n, Task task, Width width, window_limits limits, Weigh weigh = Weigh{}, Clock clock = Clock{},
                 CpuClock cpu_clock = CpuClock{})
        : m_n(n), m_task(std::move(task)), m_width(std::move(width)), m_limits(limits), m_weigh(std::move(weigh)),
          m_meter(clock, cpu_clock), m_slots(limits.slots_for(n)) {
        for (std::size_t k = 0; k < m_slots.size(); ++k) m_slots[k].open_for(k);
    }
    /// Stops the workers and waits for them: a task already running finishes, no other starts.
    ~ordered_pool() { stop(); }
    ordered_pool(const ordered_pool&) = delete;
    ordered_pool& operator=(const ordered_pool&) = delete;

    /// Runs every task on workers, none in the caller, and calls `finished()` — which must not
    /// throw — from the worker that stores the last result, a task that threw counted too (at once
    /// when there are none). Returns at once: the caller sleeps as it likes, then takes the results
    /// with next(), each ready, or destroys the pool to stop it.
    /// The window must hold every result, since nobody takes one until the end, and so holds them
    /// whatever they weigh. std::system_error when not one worker can start. Call it once, before
    /// any next().
    void run_all(std::function<void()> finished) {
        if (m_slots.size() < m_n) throw pygim::errors::invalid_state("run_all needs a window that holds every result");
        m_limits = window_limits::whole(m_n);   // a byte limit would hold back the results nobody takes yet
        m_caller_works = false;
        m_finished = std::move(finished);   // before any worker exists: none can finish unheard
        m_started = true;
        if (m_n == 0) {
            m_finished();
            return;
        }
        grow_to(m_width.at_start());
        m_crew.rethrow_if_none_run();   // fewer workers do the same work; none, none of it
    }

    /// Whether every result has been handed out.
    [[nodiscard]] bool exhausted() const noexcept { return m_out.load(std::memory_order_relaxed) >= m_n; }
    /// Whether next() would hand a result over at once: a worker has it ready, nothing to wait for
    /// or to run — what lets a caller holding a lock (Python's GIL) keep it.
    [[nodiscard]] bool ready() const noexcept {
        const std::size_t j = m_out.load(std::memory_order_relaxed);
        return j < m_n && m_slots[j % m_slots.size()].is_full_for(j);
    }

    /// The next result in index order: run here when nobody has started it, else waited for. A
    /// task's exception is rethrown here, at its place in the order; std::out_of_range once every
    /// result has been handed out.
    [[nodiscard]] result_type next() {
        const std::size_t j = m_out.load(std::memory_order_relaxed);
        if (j >= m_n) throw pygim::errors::invalid_state("ordered_pool: every result was handed out");
        if (!m_started) {   // the workers start with the first result asked for, not with the pool
            m_started = true;
            grow_to(m_width.at_start());   // the consumer works, whatever starts beside it
        }
        std::size_t unclaimed = j;
        if (m_next_claim.compare_exchange_strong(unclaimed, j + 1)) return run_here(j);
        return take(j);
    }

private:
    // A task's place in the window: free for task j, full once a worker has put task j's result or
    // error in it, then emptied by the consumer and freed for task j + slots. Its state, modulo
    // 2^32, is what a waiter compares with: 2j free for task j, 2j + 1 full with it.
    class slot {
    public:
        // What a full slot held: its task's result, or what the task threw; its weight in the window.
        class outcome {
        public:
            outcome(std::optional<result_type> result, std::exception_ptr error, std::size_t weight) noexcept
                : m_result(std::move(result)), m_error(std::move(error)), m_weight(weight) {}
            [[nodiscard]] std::size_t weight() const noexcept { return m_weight; }
            /// The result; what the task threw, rethrown.
            [[nodiscard]] result_type get() && {
                if (m_error) std::rethrow_exception(m_error);
                return std::move(*m_result);
            }

        private:
            std::optional<result_type> m_result;
            std::exception_ptr m_error;
            std::size_t m_weight;
        };

        static constexpr std::uint32_t free_for(std::size_t j) noexcept { return static_cast<std::uint32_t>(2 * j); }
        static constexpr std::uint32_t full_for(std::size_t j) noexcept { return static_cast<std::uint32_t>(2 * j + 1); }

        [[nodiscard]] bool is_free_for(std::size_t j) const noexcept { return m_state.load(std::memory_order_acquire) == free_for(j); }
        [[nodiscard]] bool is_full_for(std::size_t j) const noexcept { return m_state.load(std::memory_order_acquire) == full_for(j); }
        /// What a waiter waits on.
        [[nodiscard]] std::atomic<std::uint32_t>& state() noexcept { return m_state; }

        /// Free for task j, nothing in it.
        void open_for(std::size_t j) noexcept { m_state.store(free_for(j), std::memory_order_release); }
        /// Task j's result in, `weight` bytes of the window; whoever waits for it is woken.
        void fill(std::size_t j, result_type result, std::size_t weight) {
            m_result.emplace(std::move(result));
            m_weight = weight;
            publish(j);
        }
        /// What task j threw, in its result's place.
        void fail(std::size_t j, std::exception_ptr error) noexcept {
            m_error = std::move(error);
            publish(j);
        }
        /// What it holds, taken out: it holds nothing after.
        [[nodiscard]] outcome empty() noexcept {
            return outcome(std::exchange(m_result, std::nullopt), std::exchange(m_error, nullptr), std::exchange(m_weight, 0));
        }

    private:
        void publish(std::size_t j) noexcept {
            m_state.store(full_for(j), std::memory_order_release);
            m_state.notify_all();
        }

        std::atomic<std::uint32_t> m_state{0};
        std::optional<result_type> m_result;
        std::exception_ptr m_error;
        std::size_t m_weight = 0;
    };
    static_assert(slot::free_for(3) == 6 && slot::full_for(3) == 7 && slot::free_for(std::size_t{1} << 31) == 0);   // modulo 2^32

    [[nodiscard]] slot& at(std::size_t j) noexcept { return m_slots[j % m_slots.size()]; }

    // Waits until `a` holds `want`; false when the pool stops first.
    [[nodiscard]] bool wait_for(std::atomic<std::uint32_t>& a, std::uint32_t want) const noexcept {
        for (std::uint32_t s = a.load(std::memory_order_acquire); s != want; s = a.load(std::memory_order_acquire)) {
            if (m_stopping.load(std::memory_order_acquire)) return false;
            a.wait(s, std::memory_order_acquire);
        }
        return true;
    }

    // Task j may run once its slot is free and the window has room; the task the consumer needs next
    // always may, so the window never stalls the consumer. A worker the window holds back sleeps
    // until the window has drained by half (announce()): waking one per result handed out put a
    // system call on the consumer's path for every result — 20,000 small files took 144 ms on 16
    // threads instead of 11 once the workers had filled the window.
    [[nodiscard]] bool admitted(std::size_t j) {
        for (;;) {
            const std::uint32_t seen = m_moved.load(std::memory_order_acquire);   // before the checks: no move is missed
            if (m_stopping.load(std::memory_order_acquire)) return false;
            const bool slot_free = at(j).is_free_for(j);
            const bool has_room = j == m_out.load(std::memory_order_acquire) || m_limits.admits(m_bytes_ahead.load(std::memory_order_acquire));
            if (slot_free && has_room) return true;
            m_moved.wait(seen, std::memory_order_acquire);
        }
    }

    // One worker: claims tasks in order until there are none, or the pool stops.
    void work() {
        for (std::size_t j; (j = m_next_claim.fetch_add(1)) < m_n;) {
            if (!admitted(j)) return;
            slot& s = at(j);
            try {
                result_type result = m_meter.timed(j, [&] { return m_task(j); });
                const std::size_t weight = m_weigh(result);
                m_bytes_ahead.fetch_add(weight, std::memory_order_acq_rel);   // before it is full: the consumer subtracts it after
                s.fill(j, std::move(result), weight);
            } catch (...) {
                s.fail(j, std::current_exception());
            }
            finished_one();   // after its result is in: run_all's finished() finds every result ready
        }
    }

    // A task is done, returned or thrown: its cost may start more workers, and the last one wakes
    // run_all's caller.
    void finished_one() noexcept {
        const std::size_t done = m_meter.count_done();
        const std::size_t remaining = m_n - std::min(m_next_claim.load(std::memory_order_relaxed), m_n);
        m_meter.consider(m_width, remaining, [this](std::size_t w) { grow_to(w); });
        if (done == m_n && m_finished) m_finished();
    }

    // Workers until `wanted` threads run, as far as the system lets them start.
    void grow_to(std::size_t wanted) {
        m_crew.grow_to(workers_for(wanted), [this](std::size_t) { work(); });
    }

    // The workers `wanted` threads mean: the consumer counted as one while it works too (next()),
    // not under run_all; never more threads than tasks, nor than half the window's slots, since no
    // more can be let in at once when it drains by half: a 16-slot window on 16 threads took 215 ms
    // for 20,000 small files, on 8 threads 18 ms. A window that holds every result (run_all's) has
    // no such limit.
    [[nodiscard]] std::size_t workers_for(std::size_t wanted) const noexcept {
        const std::size_t room = m_slots.size() >= m_n ? wanted : std::max<std::size_t>(m_slots.size() / 2, 1);
        const std::size_t threads = std::min({wanted, room, std::max<std::size_t>(m_n, 1)});
        return threads - std::min<std::size_t>(threads, m_caller_works ? 1 : 0);
    }

    // Task j, in the consumer: handed out and counted done, returned or thrown.
    [[nodiscard]] result_type run_here(std::size_t j) {
        const on_scope_exit moves_on([this, j] {
            release(j, 0);
            finished_one();
        });
        return m_meter.timed(j, [&] { return m_task(j); });
    }

    // Task j's result, from its slot.
    [[nodiscard]] result_type take(std::size_t j) {
        slot& s = at(j);
        if (!s.is_full_for(j)) announce();   // about to wait: no worker may sleep on a move not told
        (void)wait_for(s.state(), slot::full_for(j));   // the consumer is never waiting when the pool stops
        typename slot::outcome got = s.empty();   // before the slot is freed for a later task
        m_bytes_ahead.fetch_sub(got.weight(), std::memory_order_acq_rel);
        release(j, got.weight());
        return std::move(got).get();
    }

    // Task j is handed out, freeing `freed` bytes: its slot is task j + slots' to fill, and the window
    // has moved — told to the workers it holds back once it has drained by half.
    void release(std::size_t j, std::size_t freed) noexcept {
        at(j).open_for(j + m_slots.size());
        m_out.store(j + 1, std::memory_order_release);
        m_unannounced_results += 1;
        m_unannounced_bytes += freed;
        if (m_limits.worth_waking(m_unannounced_results, m_unannounced_bytes, m_slots.size())) announce();
    }

    // Wakes the workers the window holds back, to look again.
    void announce() noexcept {
        m_unannounced_results = 0;
        m_unannounced_bytes = 0;
        m_moved.fetch_add(1, std::memory_order_acq_rel);
        m_moved.notify_all();
    }

    // Every waiter woken to find the pool stopping, then every worker joined.
    void stop() noexcept {
        m_stopping.store(true, std::memory_order_release);
        m_moved.fetch_add(1, std::memory_order_acq_rel);
        m_moved.notify_all();
        m_crew.stop();
    }

    std::size_t m_n;
    Task m_task;
    Width m_width;
    window_limits m_limits;
    [[no_unique_address]] Weigh m_weigh;
    task_meter<Clock, CpuClock> m_meter;
    std::vector<slot> m_slots;
    std::atomic<std::size_t> m_next_claim{0};     // the next task nobody has started
    std::atomic<std::size_t> m_out{0};            // the next result the consumer takes
    std::atomic<std::size_t> m_bytes_ahead{0};    // what the results waiting in slots weigh
    std::size_t m_unannounced_results = 0;        // the consumer's: what has drained since the last wake-up
    std::size_t m_unannounced_bytes = 0;
    std::atomic<std::uint32_t> m_moved{0};        // moves at each wake-up: what those workers wait on
    std::atomic<bool> m_stopping{false};
    bool m_started = false;                       // the consumer's: workers start at run_all or the first next()
    bool m_caller_works = true;                   // false under run_all: every task on workers
    std::function<void()> m_finished;             // run_all's: called once the last task is done
    worker_crew m_crew{"pygim-pool"};             // last: its threads stop before what they use goes
};

}  // namespace pygim::primitives::parallel
