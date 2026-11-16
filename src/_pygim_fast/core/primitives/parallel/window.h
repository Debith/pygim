#pragma once
// core/primitives/parallel/window.h — how far a pool's workers may run ahead of its consumer: a count of
// results and a budget of bytes (window_limits), and what a result weighs against it (unweighed).
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cstddef>
#include <limits>



namespace pygim::primitives::parallel {

// ── how far ahead ────────────────────────────────────────────────────────

/// How far workers may run ahead of the consumer: at most `results` results, holding at most
/// `bytes` bytes between them — 0 bytes lets none run ahead of the result the consumer needs next,
/// no_byte_limit leaves the count alone to bound them.
class window_limits {
public:
    static constexpr std::size_t no_byte_limit = std::numeric_limits<std::size_t>::max();

    constexpr window_limits(std::size_t results, std::size_t bytes) noexcept : m_results(std::max<std::size_t>(results, 1)), m_bytes(bytes) {}
    /// A window that holds all of `results` results whatever they weigh: what run_all needs.
    [[nodiscard]] static constexpr window_limits whole(std::size_t results) noexcept { return window_limits(results, no_byte_limit); }

    /// The slots a pool of `tasks` tasks needs: never more than it has tasks.
    [[nodiscard]] constexpr std::size_t slots_for(std::size_t tasks) const noexcept { return std::max<std::size_t>(std::min(m_results, tasks), 1); }
    /// Whether results holding `bytes_ahead` leave room for another.
    [[nodiscard]] constexpr bool admits(std::size_t bytes_ahead) const noexcept { return bytes_ahead < m_bytes; }
    /// Whether the window has drained enough to wake the workers it holds back: by half, in
    /// results (`moved` of `slots`) or in bytes (`freed`). Waking them per result put a system call
    /// on the consumer's path for every one; by too little, every waker woke a herd.
    [[nodiscard]] constexpr bool worth_waking(std::size_t moved, std::size_t freed, std::size_t slots) const noexcept {
        return 2 * moved >= std::max<std::size_t>(slots, 2) || (m_bytes != 0 && freed >= m_bytes / 2);
    }

private:
    std::size_t m_results;
    std::size_t m_bytes;
};
static_assert(window_limits(1'024, 100).slots_for(10) == 10 && window_limits(4, 100).slots_for(10) == 4 && window_limits(4, 100).slots_for(0) == 1);
static_assert(window_limits(4, 100).admits(99) && !window_limits(4, 100).admits(100) && !window_limits(4, 0).admits(0) &&
              window_limits::whole(4).admits(window_limits::no_byte_limit - 1) && window_limits::whole(10).slots_for(10) == 10);
static_assert(window_limits(1'024, 1'000).worth_waking(512, 0, 1'024) && !window_limits(1'024, 1'000).worth_waking(511, 0, 1'024) &&
              window_limits(1'024, 1'000).worth_waking(1, 500, 1'024) && window_limits(1, 1'000).worth_waking(1, 0, 1));

/// What a result weighs against window_limits' bytes: nothing, unless the pool is told.
class unweighed {
public:
    template <class Result>
    [[nodiscard]] constexpr std::size_t operator()(const Result&) const noexcept { return 0; }
};
static_assert(unweighed{}(42) == 0);

}  // namespace pygim::primitives::parallel
