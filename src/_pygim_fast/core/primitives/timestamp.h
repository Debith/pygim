#pragma once
// core/primitives/timestamp.h — a moment as nanoseconds since its clock's epoch, read from a clock
// that is GIVEN, never fetched: code that keeps time takes its clock, so a test can hand it a
// manual one.
//
//     timestamp t = timestamp::now();                       // the wall clock: ns since the Unix epoch
//     timestamp(12'333).nanoseconds()           -> 12333
//     monotonic_timestamp a = monotonic_timestamp::now();   // for measuring: never jumps
//     monotonic_timestamp::now().seconds_since(a)            -> seconds, as a timer reports them
//
//     manual_time time;                                     // a clock that moves only when told
//     const auto t0 = basic_timestamp<manual_clock>::now(time.clock());
//     time.advance(1_min + 30_s);                           // the units' literals (duration.h)
//     basic_timestamp<manual_clock>::now(time.clock()) - t0 -> 90s
//
// Every moment and every length of time is ONE signed 64-bit count of nanoseconds: a timestamp is
// one since its clock's epoch, a difference of two is a duration (duration.h). Units,
// seconds as a float and calendar forms are conversions of that number, never a second store.
//
// CORE layer: pybind-free, and constexpr except for the operating-system clocks. The OS is read in
// exactly three places, wall_clock::now_ns(), monotonic_clock::now_ns() and
// thread_cpu_clock::now_ns() (the last through platform/clocks.h); timing code elsewhere in pygim
// takes one of these as its clock and never reads a clock itself (#264; utils/logging.h's ScopeLog,
// the BCP profiler and the fetch benchmark too, which test_layering's
// test_time_is_read_in_one_header holds). A timestamp's clock is part of its type, so a wall-clock
// moment cannot be subtracted from a monotonic one.

#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <thread>

#include "../../platform/clocks.h"
#include "duration.h"

namespace pygim::primitives {

/// Where `now` comes from: a copyable value whose now_ns() is the count of nanoseconds since its
/// epoch.
template <class C>
concept clock_source = std::copyable<C> && requires(const C& c) {
    { c.now_ns() } -> std::same_as<std::int64_t>;
};

/// The calendar clock: nanoseconds since 1970-01-01T00:00:00Z. It jumps when the system time is
/// set, so it is for naming moments, not for measuring between them.
class wall_clock {
public:
    [[nodiscard]] std::int64_t now_ns() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
};

/// The measuring clock: never goes backwards; its epoch is unspecified (typically the boot).
class monotonic_clock {
public:
    [[nodiscard]] std::int64_t now_ns() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

/// The calling thread's CPU time: nanoseconds it has run, its waiting left out — what tells work
/// that waits on a disk from work that computes. Its epoch is the thread's start, so only a
/// difference read on one thread means anything. Windows reads the measuring clock instead (no
/// waiting is seen there; platform/windows/clocks.h). About 500 ns a read on Linux: sample it, do
/// not read it per item.
class thread_cpu_clock {
public:
    [[nodiscard]] std::int64_t now_ns() const noexcept {
        return platform::native_clocks::thread_cpu_ns();
    }
};

/// The calling thread waits `d` on the OS's clock: the one sleep pygim's code makes, here beside the
/// clocks so no other file names std::chrono. Code that a test must time exactly takes a clock
/// instead; a sleep is for waiting on something outside the process (a server, a retry).
inline void sleep_for(duration d) { std::this_thread::sleep_for(d.as_chrono()); }

class manual_clock;

/// Time that moves only when told: what makes timing code testable, and provable at compile time.
/// Its clock() is a view of it, so a timer given that clock sees every advance().
class manual_time {
public:
    constexpr explicit manual_time(std::int64_t start_ns = 0) noexcept : m_ns(start_ns) {}
    /// Moves time by `by` (negative moves it back); std::overflow_error past 64 bits, and then
    /// time has not moved.
    constexpr void advance(duration by) { m_ns = sum_exactly(duration(m_ns), by).nanoseconds(); }
    [[nodiscard]] constexpr std::int64_t now_ns() const noexcept { return m_ns; }
    [[nodiscard]] constexpr manual_clock clock() const noexcept;

private:
    std::int64_t m_ns;
};

/// A view of a manual_time: the clock_source a test gives to the code it times.
class manual_clock {
public:
    constexpr explicit manual_clock(const manual_time& time) noexcept : m_time(&time) {}
    [[nodiscard]] constexpr std::int64_t now_ns() const noexcept { return m_time->now_ns(); }

private:
    const manual_time* m_time;
};

constexpr manual_clock manual_time::clock() const noexcept { return manual_clock(*this); }

static_assert(clock_source<wall_clock> && clock_source<monotonic_clock> && clock_source<thread_cpu_clock> &&
              clock_source<manual_clock>);
static_assert([] {
    manual_time time(5);
    const manual_clock clock = time.clock();
    time.advance(duration(10));
    return clock.now_ns() == 15;
}());

/// A moment: nanoseconds since Clock's epoch. basic_timestamp(12'333) is 12,333 ns after it.
template <clock_source Clock>
class basic_timestamp {
public:
    using clock_type = Clock;

    constexpr explicit basic_timestamp(std::int64_t nanoseconds) noexcept : m_ns(nanoseconds) {}

    /// The clock's current moment.
    [[nodiscard]] static constexpr basic_timestamp now(const Clock& clock = Clock{}) noexcept {
        return basic_timestamp(clock.now_ns());
    }
    /// Before every reading of any clock: the moment a timer that has not started holds.
    [[nodiscard]] static constexpr basic_timestamp never() noexcept {
        return basic_timestamp(std::numeric_limits<std::int64_t>::min());
    }

    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return m_ns; }
    /// The time from `earlier` to this moment.
    [[nodiscard]] constexpr duration operator-(const basic_timestamp& earlier) const noexcept { return duration(m_ns - earlier.m_ns); }
    /// The same in seconds, as a timer reports them.
    [[nodiscard]] constexpr double seconds_since(const basic_timestamp& earlier) const noexcept {
        return seconds_of(*this - earlier);
    }
    [[nodiscard]] constexpr auto operator<=>(const basic_timestamp&) const noexcept = default;

private:
    std::int64_t m_ns;
};

/// A calendar moment: nanoseconds since the Unix epoch. Python's pygim.Timestamp.
using timestamp = basic_timestamp<wall_clock>;
/// A moment to measure from: what a timer, a profiler or a scope log starts at.
using monotonic_timestamp = basic_timestamp<monotonic_clock>;

static_assert(timestamp(12'333).nanoseconds() == 12'333);
static_assert(timestamp(3'500) - timestamp(1'000) == duration(2'500) && timestamp(3'500).seconds_since(timestamp(1'000)) == 2.5e-6);
static_assert(timestamp(1'000) < timestamp(3'500) && timestamp(7) == timestamp(7));
static_assert(timestamp::never() < timestamp(std::numeric_limits<std::int64_t>::min() + 1));

}  // namespace pygim::primitives