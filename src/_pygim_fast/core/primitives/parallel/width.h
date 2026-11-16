#pragma once
// core/primitives/parallel/width.h — how many threads a pool's tasks are worth: what one more thread
// costs (item_cost), how much of a task's time is spent computing (compute_share), and the width
// policies a pool is wired with (fixed_width, elastic_width, chosen_width; width_for reads Python's
// threads=). The measurements behind its constants: parallel.h.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

#include "../duration.h"

namespace pygim::primitives::parallel {

// ── how wide ─────────────────────────────────────────────────────────────

/// What the tasks done so far cost: how many threads the rest of them is worth.
class item_cost {
public:
    /// What one more thread costs to start and join: 8 threads over 8 files took 179 µs where one
    /// took 33 (Linux, 2026-10-25).
    static constexpr duration thread_start = 20_us;

    constexpr item_cost(duration spent, std::size_t items) noexcept : m_spent(spent), m_items(items) {}

    /// The threads for `remaining` more tasks at this cost, between one and `cap`: w threads take
    /// about (w - 1)·thread_start + work / w, so widen while the next thread saves more than it
    /// costs, work / (w·(w + 1)) > thread_start.
    [[nodiscard]] constexpr std::size_t width_for(std::size_t remaining, std::size_t cap) const noexcept {
        const duration work = work_of(remaining);
        std::size_t w = 1;
        while (w < cap && thread_start * static_cast<duration::rep>(w * (w + 1)) < work) ++w;
        return w;
    }

private:
    [[nodiscard]] constexpr duration work_of(std::size_t remaining) const noexcept {
        if (m_items == 0) return 0_ns;
        return m_spent / static_cast<duration::rep>(m_items) * static_cast<duration::rep>(remaining);
    }

    duration m_spent;
    std::size_t m_items;
};
static_assert(item_cost(32_us, 32).width_for(30, 16) == 1);     // 30 µs of work: not worth a thread
static_assert(item_cost(320_us, 32).width_for(32, 16) == 4);    // 320 µs: four
static_assert(item_cost(320_us, 32).width_for(1'000, 16) == 16); // 10 ms: the cap
static_assert(item_cost(0_ns, 0).width_for(1'000, 16) == 1);

/// How much of a task's time its thread spent on a CPU rather than waiting — on a disk, a network.
/// Threads past the cores pay only for the waiting: a warm read copies pages and slows down past
/// the cores (16 threads 2.2 ms, 32 threads 8.4), a cold one waits and keeps gaining (16: 27.8 ms,
/// 32: 17.0; 2026-10-25).
class compute_share {
public:
    constexpr compute_share(duration on_cpu, duration wall) noexcept : m_on_cpu(on_cpu), m_wall(wall) {}

    /// The most threads worth running: the cores when tasks only compute, more as they wait —
    /// cores x wall / on-CPU time — but never more than `cap`, nor fewer than the cores.
    [[nodiscard]] constexpr std::size_t threads_worth(std::size_t cores, std::size_t cap) const noexcept {
        const std::size_t floor = std::min(cores, cap);
        if (m_on_cpu <= 0_ns) return floor;   // unmeasured
        const auto scaled = static_cast<std::size_t>(m_wall / std::max(m_on_cpu / static_cast<duration::rep>(cores), 1_ns));
        return std::clamp(scaled, floor, cap);
    }

private:
    duration m_on_cpu;
    duration m_wall;
};
static_assert(compute_share(10_ms, 10_ms).threads_worth(16, 32) == 16);   // computes: the cores
static_assert(compute_share(1_ms, 10_ms).threads_worth(16, 32) == 32);    // waits 90%: the cap
static_assert(compute_share(8_ms, 10_ms).threads_worth(16, 32) == 20);    // waits 20%: 16 / 0.8
static_assert(compute_share(0_ns, 0_ns).threads_worth(16, 32) == 16);

/// How many threads a pool runs, the consumer counted: before any task is measured, and after.
template <class W>
concept width_policy = requires(const W& width, const item_cost& cost, const compute_share& share, std::size_t remaining) {
    { width.at_start() } -> std::same_as<std::size_t>;
    { width.for_the_rest(cost, share, remaining) } -> std::same_as<std::size_t>;
};

/// `threads` threads from the start (at least one).
class fixed_width {
public:
    constexpr explicit fixed_width(std::size_t threads) noexcept : m_threads(std::max<std::size_t>(threads, 1)) {}

    [[nodiscard]] constexpr std::size_t at_start() const noexcept { return m_threads; }
    [[nodiscard]] constexpr std::size_t for_the_rest(const item_cost&, const compute_share&, std::size_t) const noexcept { return m_threads; }
    [[nodiscard]] constexpr bool operator==(const fixed_width&) const noexcept = default;

private:
    std::size_t m_threads;
};
static_assert(width_policy<fixed_width> && fixed_width(0) == fixed_width(1) && fixed_width(4).at_start() == 4);

/// The consumer alone at first; then as many threads as the rest is worth: the cores for work that
/// computes, more for work that waits, up to `cap`.
class elastic_width {
public:
    constexpr elastic_width(std::size_t cores, std::size_t cap) noexcept
        : m_cores(std::max<std::size_t>(cores, 1)), m_cap(std::max<std::size_t>(cap, 1)) {}

    [[nodiscard]] constexpr std::size_t at_start() const noexcept { return 1; }
    [[nodiscard]] constexpr std::size_t for_the_rest(const item_cost& cost, const compute_share& share, std::size_t remaining) const noexcept {
        return cost.width_for(remaining, share.threads_worth(m_cores, m_cap));
    }

private:
    std::size_t m_cores;
    std::size_t m_cap;
};
static_assert([] {
    const item_cost ten_us_each(320_us, 32);
    const compute_share computes(1_ms, 1_ms);
    const compute_share waits(1_ms, 10_ms);
    const elastic_width width(16, 32);
    return width_policy<elastic_width> && width.at_start() == 1 && width.for_the_rest(ten_us_each, computes, 1'000) == 16 &&
           width.for_the_rest(ten_us_each, waits, 1'000) == 22 && width.for_the_rest(ten_us_each, waits, 10) == 2;
}());

/// The cap an elastic pool is wired with: threads that wait on a disk pay well past the cores (cold:
/// 48 threads beat 16, on 4 cores too), so at least 32, and twice the cores where there are more.
[[nodiscard]] constexpr std::size_t elastic_cap(std::size_t cores) noexcept { return std::max<std::size_t>(32, 2 * cores); }
static_assert(elastic_cap(4) == 32 && elastic_cap(64) == 128);

/// The cores this machine offers; one when it cannot tell. Machine state: read where a pool is wired.
[[nodiscard]] inline std::size_t this_machines_cores() noexcept {
    return std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
}

/// A width chosen at run time (Python's threads=): fixed_width or elastic_width.
class chosen_width {
public:
    constexpr chosen_width(fixed_width width) noexcept : m_width(width) {}
    constexpr chosen_width(elastic_width width) noexcept : m_width(width) {}

    [[nodiscard]] constexpr std::size_t at_start() const noexcept {
        return std::visit([](const auto& w) { return w.at_start(); }, m_width);
    }
    [[nodiscard]] constexpr std::size_t for_the_rest(const item_cost& cost, const compute_share& share, std::size_t remaining) const noexcept {
        return std::visit([&](const auto& w) { return w.for_the_rest(cost, share, remaining); }, m_width);
    }

private:
    std::variant<fixed_width, elastic_width> m_width;
};
static_assert(width_policy<chosen_width> && chosen_width(fixed_width(3)).at_start() == 3 && chosen_width(elastic_width(8, 32)).at_start() == 1);

/// What `threads=None` means for an operation, as a policy type: its width on `cores`, and how
/// the refusal of 0 names it. Work that may wait on reads grows its width while more threads help
/// (elastic); work that computes takes one thread per core from the start (Debith, Q39: on 16
/// cores code.parse read CPython's Lib in 75 ms fixed, 145 ms elastic at 20-24 workers).
class grown_while_it_helps {
public:
    static constexpr std::string_view meaning = "a width grown while more threads help";
    [[nodiscard]] static constexpr chosen_width width(std::size_t cores) noexcept { return chosen_width(elastic_width(cores, elastic_cap(cores))); }
};
static_assert(grown_while_it_helps::width(16).at_start() == 1);
class one_per_core {
public:
    static constexpr std::string_view meaning = "one per core";
    [[nodiscard]] static constexpr chosen_width width(std::size_t cores) noexcept { return chosen_width(fixed_width(cores)); }
};
static_assert(one_per_core::width(16).at_start() == 16);
template <class U>
concept unset_policy = requires(std::size_t cores) {
    { U::width(cores) } -> std::same_as<chosen_width>;
    { U::meaning } -> std::convertible_to<std::string_view>;
};

/// The width a `threads=` argument names on a machine of `cores`: none, what `Unset` says; a
/// number, that many threads. 0 is std::invalid_argument naming `who` (in Python, a ValueError).
/// One reading of the argument for every operation that takes it.
///
///     width_for(std::nullopt, "read_each", 16).at_start()            -> 1, growing up to elastic_cap(16)
///     width_for<one_per_core>(std::nullopt, "parse", 16).at_start()  -> 16
///     width_for<one_per_core>(4, "parse", 16).at_start()             -> 4
template <unset_policy Unset = grown_while_it_helps>
[[nodiscard]] constexpr chosen_width width_for(std::optional<std::size_t> threads, std::string_view who, std::size_t cores) {
    if (!threads) return Unset::width(cores);
    if (*threads == 0) throw std::invalid_argument(std::string(who) + ": threads must be at least 1, or None for " + std::string(Unset::meaning));
    return chosen_width(fixed_width(*threads));
}
static_assert(width_for<one_per_core>(std::nullopt, "parse", 16).at_start() == 16 && width_for(std::nullopt, "read_each", 16).at_start() == 1 &&
              width_for<one_per_core>(4, "parse", 16).at_start() == 4);

/// The same on this machine's cores.
template <unset_policy Unset = grown_while_it_helps>
[[nodiscard]] inline chosen_width width_for(std::optional<std::size_t> threads, std::string_view who) {
    return width_for<Unset>(threads, who, this_machines_cores());
}

}  // namespace pygim::primitives::parallel
