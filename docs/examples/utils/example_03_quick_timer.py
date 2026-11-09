# type: ignore
"""Timing code: ``pygim.utils.QuickTimer``.

One timer measures a block and the laps inside it — several stop points on
the same timer. Two things are given to it rather than built in: the CLOCK it
reads (the monotonic clock unless you pass one) and the REPORT that tells you
what it measured (text on sys.stderr unless you pass one). Given a manual clock,
every number is exact; given a report, the timer prints, stays silent, or
calls your hooks.

This example demonstrates:
- Timing a block and its laps with a manual clock, so every number is exact
- Silent timing for metrics: the values come out, nothing is printed
- Laps as with blocks on QuickTimer.silent(): started and stopped by the block
- Hooks: your own callbacks when a lap starts, when it stops, when the timer finishes
- The default report: today's text on sys.stderr
"""

import contextlib
import io

from pygim import utils

# ----------------------------------------------------------------------------
# 1. A manual clock: time moves only when told
# ----------------------------------------------------------------------------
clock = utils.ManualTime()                   # starts at 0 ns
clock.advance(us=1)                          # units add up into one 64-bit count of nanoseconds
assert clock.now() == 1_000


def tick_tock(ms):
    """The work being timed: the manual clock moves on by `ms` milliseconds."""
    clock.advance(ms=ms)


tick_tock(2)
assert clock.now() == 2_001_000

# ----------------------------------------------------------------------------
# 2. Timing a block and its laps, silently
# ----------------------------------------------------------------------------
#                                 ┌─ clock: where "now" comes from (default: the monotonic clock)
#                                 │            ┌─ report: how it tells you (default: sys.stderr)
#                                 ▼            ▼
with utils.QuickTimer("load csv", clock=clock, report=utils.Report.silent()) as timer:
    tick_tock(8)                             # before any lap: in the total, in no lap
    timer.start_lap("read")
    tick_tock(120)
    timer.start_lap("parse")                 # starting a lap stops the one running
    tick_tock(292)
# leaving the block finishes the timer: a running lap stops, the report hears "finished"

snapshot = timer.snapshot()                  # the measured values, frozen at the finish
assert snapshot.name == "load csv"
assert snapshot.laps == {"read": utils.Duration(ms=120), "parse": utils.Duration(ms=292)}
# from the timer's start: more than its laps' 412
assert snapshot.total == utils.Duration(ms=420)
assert snapshot.total.seconds == 0.42        # a view, for printing

tick_tock(5_000)                             # later readings of a finished timer do not move
assert timer.snapshot().total == utils.Duration(ms=420)

# ----------------------------------------------------------------------------
# 3. Laps as with blocks, on a timer that tells nobody
# ----------------------------------------------------------------------------
with utils.QuickTimer.silent("load csv", clock=clock) as timer:
    with timer.lap("read") as read:          # started here, stopped as the block ends
        tick_tock(120)
    tick_tock(8)                             # between the blocks: in the total, in no lap
    with timer.lap("parse"):
        tick_tock(292)
assert read.took == utils.Duration(ms=120)
assert timer.laps == {"read": utils.Duration(ms=120), "parse": utils.Duration(ms=292)}
assert timer.total.seconds == 0.42           # read off the timer: no snapshot needed

# ----------------------------------------------------------------------------
# 4. Hooks: your callbacks at each moment
# ----------------------------------------------------------------------------
lines = []
report = utils.Report.hooks()
report.on_started(lambda timer_name, lap: lines.append(f"{timer_name} [{lap}] started"))
report.on_stopped(
    lambda timer_name, lap, took: lines.append(f"{timer_name} [{lap}] {took.seconds:.3f}s"))
report.on_finished(lambda snap: lines.append(f"{snap.name} total {snap.total.seconds:.3f}s"))

with utils.QuickTimer("index", clock=clock, report=report) as timer:
    timer.start_lap("scan")
    tick_tock(5)
    timer.stop_lap("scan")

assert lines == ["index [scan] started", "index [scan] 0.005s", "index total 0.005s"]

# A hook decides what a call alone used to decide with a flag:
# here, which laps announce themselves.
quiet = []
report = utils.Report.hooks()
report.on_started(lambda timer_name, lap: quiet.append(lap) if lap != "read" else None)
with utils.QuickTimer("load csv", clock=clock, report=report) as timer:
    timer.start_lap("read")
    timer.start_lap("parse")
assert quiet == ["parse"]

# ----------------------------------------------------------------------------
# 5. The default report: text on sys.stderr, as QuickTimer has always printed it
# ----------------------------------------------------------------------------
# It writes through sys.stderr, so whatever redirects that (here, contextlib; pytest's capsys;
# a notebook) sees the lines.
with contextlib.redirect_stderr(io.StringIO()) as err:
    with utils.QuickTimer("load csv") as timer:      # the monotonic clock, the text report
        timer.start_lap("read")
text = err.getvalue()
# "started" lines are kept (an option of the text report)
assert "load csv [read]: started" in text
assert "load csv total:" in text
assert "  - read:" in text

print(f"quick timer example OK: laps {snapshot.laps}, total {snapshot.total}, "
      f"{len(lines)} hook calls")
