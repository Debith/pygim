// Compile-time proofs for core/primitives/duration.h, core/primitives/timestamp.h and
// core/primitives/quick_timer.h, and the pools of core/primitives/parallel.h compiled on every
// system.
//
// This TU is part of the utils extension's SOURCES (ext.utils.toml): it is compiled by every
// build, so a violated law fails the build instead of a test run. A manual_clock makes time
// move only when told, so every measurement below is exact.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../src/_pygim_fast/core/primitives/duration.h"
#include "../../src/_pygim_fast/core/primitives/parallel.h"
#include "../../src/_pygim_fast/core/primitives/quick_timer.h"
#include "../../src/_pygim_fast/core/primitives/timestamp.h"

namespace {

using pygim::primitives::basic_timestamp;
using pygim::primitives::manual_clock;
using pygim::primitives::manual_time;
using pygim::primitives::duration;
using manual_timer = pygim::primitives::QuickTimerT<void, 8, manual_clock, pygim::primitives::silent>;
using manual_moment = basic_timestamp<manual_clock>;

// A moment read from a clock is that clock's count; the difference of two is the time between.
consteval bool timestamps_read_their_clock() {
    manual_time time(1'000);
    const manual_moment a = manual_moment::now(time.clock());
    time.advance(pygim::primitives::duration(2'500));
    const manual_moment b = manual_moment::now(time.clock());
    return a.nanoseconds() == 1'000 && b.nanoseconds() == 3'500 && b - a == pygim::primitives::duration(2'500) && a < b &&
           b.seconds_since(a) == 2.5e-6;
}
static_assert(timestamps_read_their_clock());

// A clock given to timing code is a view: every advance of the time it views is seen through it,
// and the difference of two readings adds the advances exactly, units mixed.
consteval bool a_clock_sees_every_advance() {
    manual_time time;
    const manual_clock clock = time.clock();
    const manual_moment start = manual_moment::now(clock);
    time.advance(std::chrono::minutes(1));
    time.advance(std::chrono::seconds(30));
    time.advance(duration(-500));
    return manual_moment::now(clock) - start == std::chrono::seconds(90) - duration(500);
}
static_assert(a_clock_sees_every_advance());

// The total runs from construction; each lap only while it runs; starting one stops the other.
consteval bool quick_timer_measures_with_its_clock() {
    manual_time time;
    manual_timer timer("job", time.clock());
    time.advance(pygim::primitives::duration(1'000));
    timer.start_lap("read");
    time.advance(pygim::primitives::duration(2'000));
    timer.start_lap("parse");   // stops "read"
    time.advance(pygim::primitives::duration(4'000));
    timer.stop_lap("parse");
    time.advance(pygim::primitives::duration(8'000));
    return timer.total() == duration(15'000) && timer.lap("read") == duration(2'000) &&
           timer.lap("parse") == duration(4'000) && timer.total_seconds() == 15e-6;
}
static_assert(quick_timer_measures_with_its_clock());

// A running lap reports up to now; stopping it again adds nothing.
consteval bool a_running_lap_reports_up_to_now() {
    manual_time time;
    manual_timer timer("job", time.clock());
    timer.start_lap("wait");
    time.advance(pygim::primitives::duration(3'000));
    const bool running = timer.lap("wait") == duration(3'000);
    timer.stop_lap("wait");
    time.advance(pygim::primitives::duration(5'000));
    return running && timer.stop_lap("wait") == duration(3'000);
}
static_assert(a_running_lap_reports_up_to_now());

// "Not running" is never(), not the clock's zero: a lap started at time 0 is running.
consteval bool a_lap_started_at_zero_runs() {
    manual_time time(0);
    manual_timer timer("job", time.clock());
    timer.start_lap("first");
    time.advance(pygim::primitives::duration(1'000));
    return timer.stop_lap("first") == duration(1'000);
}
static_assert(a_lap_started_at_zero_runs());

// Units are std::chrono's; whole amounts add exactly; fractions round to the nearest nanosecond,
// ties to even, from the float's exact value (the cases a float product gets wrong included).
consteval bool units_convert_exactly() {
    using pygim::primitives::duration;
    using pygim::primitives::time_unit;
    const auto unit = [](std::string_view name) { return *time_unit::named(name); };
    return unit("y").length() == std::chrono::years(1) && unit("w").length() == std::chrono::weeks(1) &&
           unit("d").length() == std::chrono::days(1) && unit("us").length() == std::chrono::microseconds(1) &&
           unit("m").of(1) + unit("s").of(90) == unit("m").of(2) + unit("s").of(30) &&
           unit("s").of(1.5) == duration(1'500'000'000) && unit("s").of(-5) == duration(-5'000'000'000) &&
           unit("ns").of(0.5) == duration(0) && unit("ns").of(1.5) == duration(2) &&
           unit("ns").of(2.5) == duration(2) && unit("ns").of(-0.5) == duration(0) &&
           unit("y").of(0.286) == duration(9'025'288'271'999'999) &&   // a float product gives ...272000000
           unit("s").of(5e-324) == duration(0);                           // the smallest subnormal
}
static_assert(units_convert_exactly());

// A duration is spelt in whole units, longest first; the parts add back to it.
consteval bool spelling_adds_back() {
    using pygim::primitives::duration;
    const duration year_and_7ns = std::chrono::years(1) + duration(7);
    const pygim::primitives::spelt_in_units s(year_and_7ns);
    const pygim::primitives::spelt_in_units negative(-std::chrono::seconds(150));
    duration sum(0);
    for (const auto& part : s) sum += pygim::primitives::time_unit::named(part.unit_name())->of(part.amount());
    return s.size() == 2 && s.begin()[0].unit_name() == "y" && s.begin()[1].unit_name() == "ns" &&
           sum == year_and_7ns && negative.size() == 2 && negative.begin()[0].amount() == -2 &&
           negative.begin()[1].amount() == -30;
}
static_assert(spelling_adds_back());

// What a report hears, kept outside it: a report is copied into its timer, so it writes through a pointer.
class moment_log {
public:
    enum class kind : unsigned char { timer_started, lap_started, lap_stopped, timer_finished };
    constexpr void note(kind k, std::string_view lap, duration took) {
        m_kinds[m_size] = k;
        m_laps[m_size] = lap;
        m_took[m_size] = took;
        ++m_size;
    }
    [[nodiscard]] constexpr std::size_t size() const { return m_size; }
    [[nodiscard]] constexpr kind kind_at(std::size_t i) const { return m_kinds[i]; }
    [[nodiscard]] constexpr std::string_view lap_at(std::size_t i) const { return m_laps[i]; }
    [[nodiscard]] constexpr duration took_at(std::size_t i) const { return m_took[i]; }

private:
    std::array<kind, 16> m_kinds{};
    std::array<std::string_view, 16> m_laps{};
    std::array<duration, 16> m_took{};
    std::size_t m_size = 0;
};
static_assert([] {
    moment_log log;
    log.note(moment_log::kind::lap_stopped, "read", duration(5));
    return log.size() == 1 && log.lap_at(0) == "read" && log.took_at(0) == duration(5);
}());

// A report for proofs: it notes each moment in a moment_log (a timer_report, constexpr throughout).
class logging_report {
public:
    constexpr explicit logging_report(moment_log& log) noexcept : m_log(&log) {}
    template <class E, class... Args>
    constexpr void run(const Args&... args) const { note(E{}, args...); }
    template <class E>
    [[nodiscard]] constexpr bool has() const noexcept { return true; }

private:
    using kind = moment_log::kind;
    moment_log* m_log;
    constexpr void note(pygim::primitives::timer_started, std::string_view) const { m_log->note(kind::timer_started, {}, duration(0)); }
    constexpr void note(pygim::primitives::lap_started, std::string_view, std::string_view lap) const { m_log->note(kind::lap_started, lap, duration(0)); }
    constexpr void note(pygim::primitives::lap_stopped, std::string_view, std::string_view lap, duration took) const { m_log->note(kind::lap_stopped, lap, took); }
    constexpr void note(pygim::primitives::timer_finished, const pygim::primitives::timer_snapshot& s) const { m_log->note(kind::timer_finished, {}, s.total()); }
};
static_assert(pygim::primitives::timer_report<logging_report>);

using logged_timer = pygim::primitives::QuickTimerT<void, 8, manual_clock, logging_report>;

// The report hears the timer start, each lap start and stop (the next lap's start stops the one
// running; the finish stops the last), and the finish with the total — the destructor finishing it.
consteval bool the_report_hears_every_moment_in_order() {
    using kind = moment_log::kind;
    manual_time time;
    moment_log log;
    {
        logged_timer timer("job", time.clock(), logging_report(log));
        timer.start_lap("read");
        time.advance(duration(2'000));
        timer.start_lap("parse");
        time.advance(duration(4'000));
    }
    return log.size() == 6 && log.kind_at(0) == kind::timer_started &&
           log.kind_at(1) == kind::lap_started && log.lap_at(1) == "read" &&
           log.kind_at(2) == kind::lap_stopped && log.lap_at(2) == "read" && log.took_at(2) == duration(2'000) &&
           log.kind_at(3) == kind::lap_started && log.lap_at(3) == "parse" &&
           log.kind_at(4) == kind::lap_stopped && log.took_at(4) == duration(4'000) &&
           log.kind_at(5) == kind::timer_finished && log.took_at(5) == duration(6'000);
}
static_assert(the_report_hears_every_moment_in_order());

// A finish is once: the second does nothing, the total no longer moves, the snapshot keeps the laps.
consteval bool a_finish_is_once_and_freezes_the_total() {
    using kind = moment_log::kind;
    manual_time time;
    moment_log log;
    logged_timer timer("job", time.clock(), logging_report(log));
    timer.start_lap("read");
    time.advance(duration(2'000));
    timer.finish();
    timer.finish();
    time.advance(duration(9'000));
    const auto snapshot = timer.snapshot();
    std::size_t finishes = 0;
    for (std::size_t i = 0; i < log.size(); ++i) finishes += log.kind_at(i) == kind::timer_finished ? 1 : 0;
    return finishes == 1 && timer.finished() && timer.total() == duration(2'000) &&
           snapshot.lap("read") == duration(2'000) && snapshot.laps().size() == 1;
}
static_assert(a_finish_is_once_and_freezes_the_total());

// A moved timer reports once: the one moved from is left finished.
consteval bool a_moved_timer_reports_once() {
    using kind = moment_log::kind;
    manual_time time;
    moment_log log;
    {
        logged_timer first("job", time.clock(), logging_report(log));
        logged_timer second(std::move(first));
        time.advance(duration(1'000));
    }
    std::size_t finishes = 0;
    for (std::size_t i = 0; i < log.size(); ++i) finishes += log.kind_at(i) == kind::timer_finished ? 1 : 0;
    return finishes == 1;
}
static_assert(a_moved_timer_reports_once());

// The OS clocks and the silent report are empty: neither adds a byte to the timer.
static_assert(sizeof(pygim::primitives::QuickTimerT<>) == sizeof(pygim::primitives::QuickTimerT<void, 8, pygim::primitives::wall_clock>));
static_assert(sizeof(pygim::primitives::SilentTimer<>) < sizeof(pygim::primitives::QuickTimer));

// The thread's CPU clock is a clock like the others: the pools take it as their CpuClock.
static_assert(pygim::primitives::clock_source<pygim::primitives::thread_cpu_clock>);

// What the pools are instantiated with below: a task, a task that adds tasks, and for_each's body.
struct square {
    [[nodiscard]] std::size_t operator()(std::size_t j) const noexcept { return j * j; }
};
struct count_down {
    template <class Add>
    void operator()(int n, const Add& add) const {
        if (n > 0) add(n - 1);
    }
};
struct touch_each {
    void operator()(std::size_t, std::size_t) const noexcept {}
};

}  // namespace

// Every member of the pools and for_each, compiled on each system a build runs on: a call that system
// does not have fails its build here, before anything calls the pools. What they do under load is
// tests/static/threads_stress.cpp's, run under the sanitizers by CI's threads-stress job.
template class pygim::primitives::parallel::ordered_pool<square>;
template class pygim::primitives::parallel::task_pool<int, count_down>;
template void pygim::primitives::parallel::for_each<const touch_each&>(std::size_t, std::size_t, const touch_each&);
