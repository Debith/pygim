#pragma once
// core/primitives/parallel/meter.h — what a pool learns from its finished tasks (task_meter): their
// time, their count and the share of it spent on a CPU, and the width it decides from them.
//
// CORE layer: pybind-free.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../duration.h"
#include "../timestamp.h"
#include "width.h"

namespace pygim::primitives::parallel {

// ── what a pool learns from its tasks ───────────────────────────────────

/// What a pool learns from its finished tasks — their time, their count, and on every 16th the
/// share its thread spent on a CPU — and the one decision it takes from them: how many threads the
/// rest of its tasks is worth, by its width policy. ordered_pool and task_pool each hold one.
template <clock_source Clock, clock_source CpuClock>
class task_meter {
public:
    static constexpr std::size_t sampled_every = 16;

    task_meter(Clock clock, CpuClock cpu_clock) noexcept : m_clock(clock), m_cpu_clock(cpu_clock) {}

    /// f(), its time added to what the tasks cost whether it returns or throws; the `k`-th task a
    /// pool started is sampled on the CPU clock when k is a multiple of sampled_every.
    template <class F>
    decltype(auto) timed(std::size_t k, F&& f) {
        class spent {
        public:
            spent(task_meter& meter, bool sampled) noexcept
                : m_meter(meter), m_sampled(sampled), m_cpu_before(sampled ? cpu_moment::now(meter.m_cpu_clock) : cpu_moment::never()),
                  m_before(moment::now(meter.m_clock)) {}
            ~spent() {
                const duration took = moment::now(m_meter.m_clock) - m_before;
                if (m_sampled) {
                    m_meter.m_sampled_cpu_ns.fetch_add((cpu_moment::now(m_meter.m_cpu_clock) - m_cpu_before).nanoseconds(), std::memory_order_relaxed);
                    m_meter.m_sampled_wall_ns.fetch_add(took.nanoseconds(), std::memory_order_relaxed);
                }
                m_meter.m_spent_ns.fetch_add(took.nanoseconds(), std::memory_order_relaxed);
            }

        private:
            using moment = basic_timestamp<Clock>;
            using cpu_moment = basic_timestamp<CpuClock>;
            task_meter& m_meter;
            bool m_sampled;
            cpu_moment m_cpu_before;
            moment m_before;
        } after(*this, k % sampled_every == 0);
        return f();
    }

    /// One more task is done — returned or thrown; how many are now.
    std::size_t count_done() noexcept { return m_done.fetch_add(1, std::memory_order_acq_rel) + 1; }

    /// grow(the threads `width` says `remaining` more tasks are worth), from one thread at a time —
    /// others go on with their tasks. Best effort: when a thread cannot start, those running finish
    /// the work, so whatever grow throws is dropped here.
    template <width_policy Width, class Grow>
    void consider(const Width& width, std::size_t remaining, Grow&& grow) noexcept {
        if (m_growing.exchange(true, std::memory_order_acquire)) return;
        const item_cost cost(duration(m_spent_ns.load(std::memory_order_relaxed)), m_done.load(std::memory_order_relaxed));
        const compute_share share(duration(m_sampled_cpu_ns.load(std::memory_order_relaxed)), duration(m_sampled_wall_ns.load(std::memory_order_relaxed)));
        try {
            grow(width.for_the_rest(cost, share, remaining));
        } catch (...) {
        }
        m_growing.store(false, std::memory_order_release);
    }

private:
    [[no_unique_address]] Clock m_clock;
    [[no_unique_address]] CpuClock m_cpu_clock;
    std::atomic<std::int64_t> m_spent_ns{0};          // the finished tasks' time
    std::atomic<std::size_t> m_done{0};               // how many finished
    std::atomic<std::int64_t> m_sampled_wall_ns{0};   // the sampled tasks' time ...
    std::atomic<std::int64_t> m_sampled_cpu_ns{0};    // ... and how much of it their threads spent on a CPU
    std::atomic<bool> m_growing{false};               // a thread is deciding
};

}  // namespace pygim::primitives::parallel
