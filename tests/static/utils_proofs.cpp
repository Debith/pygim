// Compile-time proofs for utils/duration.h and utils/timestamp.h.
//
// This TU is part of the utils extension's SOURCES (ext.utils.toml): it is compiled by every
// build, so a violated law fails the build instead of a test run. A manual_clock makes time
// move only when told, so every measurement below is exact. Each header's own typical uses are
// asserted beneath its functions; this file holds what takes a clock to show.

#include <chrono>

#include "../../src/_pygim_fast/utils/duration.h"
#include "../../src/_pygim_fast/utils/timestamp.h"

namespace {

using pygim::basic_timestamp;
using pygim::duration;
using pygim::manual_clock;
using pygim::manual_time;
using manual_moment = basic_timestamp<manual_clock>;

// A moment read from a clock is that clock's count; the difference of two is the time between.
consteval bool timestamps_read_their_clock() {
    manual_time time(1'000);
    const manual_moment a = manual_moment::now(time.clock());
    time.advance(duration(2'500));
    const manual_moment b = manual_moment::now(time.clock());
    return a.nanoseconds() == 1'000 && b.nanoseconds() == 3'500 && b - a == duration(2'500) && a < b &&
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

}  // namespace
