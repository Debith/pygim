# -*- coding: utf-8 -*-
"""pygim.QuickTimer: one timer measures a block and its laps; its clock and its report are given."""

import contextlib
import io
import time as time_module   # `time` is the manual clock's fixture here

import pytest

import pygim


def ms(n):
    return pygim.Duration(ms=n)


@pytest.fixture
def time():
    return pygim.ManualTime()


def recorded():
    """A hooks report and the list of what it heard, in order."""
    events = []
    report = pygim.Report.hooks()
    report.on_timer_started(lambda name: events.append(("timer_started", name)))
    report.on_started(lambda name, lap: events.append(("started", name, lap)))
    report.on_stopped(lambda name, lap, took: events.append(("stopped", name, lap, took)))
    report.on_finished(lambda snap: events.append(("finished", snap.name, snap.total, snap.laps)))
    return report, events


def test_laps_and_total_are_the_clocks_steps(time):
    with pygim.QuickTimer("job", clock=time, report=pygim.Report.silent()) as timer:
        timer.start_lap("read")
        time.advance(ms=2)
        timer.start_lap("parse")
        time.advance(ms=4)
    assert timer.snapshot().laps == {"read": ms(2), "parse": ms(4)}


def test_the_total_runs_from_the_timers_start(time):
    timer = pygim.QuickTimer("job", clock=time, report=pygim.Report.silent())
    time.advance(ms=1)
    with timer:
        timer.start_lap("read")
        time.advance(ms=2)
    assert timer.snapshot().total == ms(3)


def test_a_finished_timer_no_longer_moves(time):
    with pygim.QuickTimer("job", clock=time, report=pygim.Report.silent()) as timer:
        time.advance(ms=2)
    time.advance(s=5)
    assert timer.snapshot().total == ms(2)


def test_the_report_hears_every_moment_in_order(time):
    report, events = recorded()
    with pygim.QuickTimer("job", clock=time, report=report) as timer:
        timer.start_lap("read")
        time.advance(ms=2)
        timer.start_lap("parse")      # stops "read"
        time.advance(ms=4)
    assert events == [
        ("timer_started", "job"),
        ("started", "job", "read"),
        ("stopped", "job", "read", ms(2)),
        ("started", "job", "parse"),
        ("stopped", "job", "parse", ms(4)),  # the finish stops the running lap
        ("finished", "job", ms(6), {"read": ms(2), "parse": ms(4)}),
    ]


def test_finishing_twice_reports_once(time):
    report, events = recorded()
    timer = pygim.QuickTimer("job", clock=time, report=report)
    timer.finish()
    timer.finish()
    assert [e[0] for e in events].count("finished") == 1


def test_a_raising_block_still_finishes_and_the_exception_passes(time):
    report, events = recorded()
    with pytest.raises(KeyError):
        with pygim.QuickTimer("job", clock=time, report=report):
            raise KeyError("x")
    assert events[-1][0] == "finished"


def test_a_lap_cannot_start_after_the_finish(time):
    timer = pygim.QuickTimer("job", clock=time, report=pygim.Report.silent())
    timer.finish()
    with pytest.raises(RuntimeError, match="finished"):
        timer.start_lap("late")


def test_laps_refused_after_the_finish_leave_nothing_behind(time):
    """A lap asked for after the finish is refused before it is made: the snapshot holds no lap of
    it, and every refusal is the same RuntimeError. Found 2026-10-29: the lap was made first — its
    name a view of a str the adapter never kept, read freed by snapshot() — and the ninth late lap
    was an IndexError, the capacity spent on laps that never ran."""
    timer = pygim.QuickTimer("job", clock=time, report=pygim.Report.silent())
    timer.finish()
    for i in range(20):
        with pytest.raises(RuntimeError, match="finished"):
            timer.start_lap(f"late {i}")
    assert timer.snapshot().laps == {}


def test_a_hook_that_adds_a_hook_hears_the_next_event_with_it(time):
    """A callback may add callbacks to its own report: they run from the next event on. Found
    2026-10-29: the report ran its callbacks over the vector a callback grew, past a reallocation."""
    report, heard = pygim.Report.hooks(), []

    def first(name, lap):
        heard.append(("first", lap))
        report.on_started(lambda name, lap: heard.append(("added", lap)))

    report.on_started(first)
    with pygim.QuickTimer("job", clock=time, report=report) as timer:
        for lap in "abcd":
            timer.start_lap(lap)
    assert heard == [("first", "a"), ("first", "b"), ("added", "b"), ("first", "c"), ("added", "c"), ("added", "c"),
                     ("first", "d"), ("added", "d"), ("added", "d"), ("added", "d")]


def test_both_takes_reports_only():
    """Report.both() refuses anything but Reports with the TypeError QuickTimer(report=) raises."""
    with pytest.raises(TypeError, match="Report"):
        pygim.Report.both(pygim.Report.silent(), 1)


def test_a_lap_name_made_on_the_fly_stays_valid(time):
    with pygim.QuickTimer("job", clock=time, report=pygim.Report.silent()) as timer:
        for i in range(3):
            timer.start_lap(f"step {i}")
            time.advance(ms=1)
    assert list(timer.snapshot().laps) == ["step 0", "step 1", "step 2"]


def test_laps_past_the_capacity_are_refused(time):
    timer = pygim.QuickTimer("job", clock=time, report=pygim.Report.silent())
    for i in range(8):
        timer.start_lap(f"lap {i}")
    with pytest.raises(IndexError):
        timer.start_lap("lap 8")


def test_a_lap_past_the_capacity_leaves_the_timer_as_it_was(time):
    """The refused name is let go; the laps there run on and the snapshot reads them."""
    timer = pygim.QuickTimer("job", clock=time, report=pygim.Report.silent())
    for i in range(8):
        timer.start_lap(f"lap {i}")
    for _ in range(3):
        with pytest.raises(IndexError):
            timer.start_lap("lap 8")
    timer.start_lap("lap 0")
    time.advance(ms=1)
    timer.finish()
    assert list(timer.snapshot().laps) == [f"lap {i}" for i in range(8)]
    assert timer.snapshot().laps["lap 0"] == pygim.Duration(ms=1)


def test_the_text_report_writes_through_sys_stderr(time):
    """What redirects sys.stderr — contextlib, pytest's capsys, a notebook — sees the report. It
    wrote to the process's stderr before, past all of them."""
    with contextlib.redirect_stderr(io.StringIO()) as err:
        with pygim.QuickTimer("job", clock=time) as timer:
            timer.start_lap("read")
            time.advance(ms=2)
    assert err.getvalue().splitlines() == ["job: started", "job [read]: started", "job [read]: 0.002000s",
                                           "job total: 0.002000s", "  - read: 0.002000s"]


def test_silent_prints_nothing(time, capfd):
    with pygim.QuickTimer("job", clock=time, report=pygim.Report.silent()) as timer:
        timer.start_lap("read")
    assert capfd.readouterr().err == ""


def test_the_default_report_prints_as_the_timer_always_has(time, capfd):
    with pygim.QuickTimer("job", clock=time) as timer:
        timer.start_lap("read")
        time.advance(ms=2)
    assert capfd.readouterr().err.splitlines() == [
        "job: started",
        "job [read]: started",
        "job [read]: 0.002000s",
        "job total: 0.002000s",
        "  - read: 0.002000s",
    ]


def test_the_stderr_report_can_leave_out_the_started_and_stopped_lines(time, capfd):
    with pygim.QuickTimer("job", clock=time, report=pygim.Report.stderr(started=False, stopped=False)) as timer:
        timer.start_lap("read")
        time.advance(ms=2)
    assert capfd.readouterr().err.splitlines() == ["job total: 0.002000s", "  - read: 0.002000s"]


def test_both_reports_hear_everything(time, capfd):
    report, events = recorded()
    with pygim.QuickTimer("job", clock=time, report=pygim.Report.both(pygim.Report.stderr(), report)):
        pass
    assert "job total: 0.000000s" in capfd.readouterr().err and events[-1][0] == "finished"


def test_only_a_hooks_report_takes_hooks():
    with pytest.raises(TypeError, match="hooks"):
        pygim.Report.silent().on_started(lambda name, lap: None)


def test_the_monotonic_clock_is_the_default():
    """A real clock: a timer given no clock measures a sleep (a manual clock would say nothing passed)."""
    with pygim.QuickTimer("job", report=pygim.Report.silent()) as timer:
        start = time_module.perf_counter()
        while time_module.perf_counter() - start < 0.002:   # not sleep: Windows may wake a 2 ms sleep early
            pass
    assert timer.snapshot().total >= pygim.Duration(ms=2)


def test_stopping_no_lap_or_an_unknown_one_is_refused(time):
    timer = pygim.QuickTimer("job", clock=time, report=pygim.Report.silent())
    with pytest.raises(RuntimeError, match="no lap is running"):
        timer.stop_lap()
    timer.start_lap("a")
    with pytest.raises(IndexError, match="no lap of that name"):
        timer.stop_lap("b")
    timer.stop_lap("a")                                   # the refusals left the lap as it was
    assert list(timer.snapshot().laps) == ["a"]


def test_a_hook_that_raises_as_the_block_ends_keeps_the_blocks_own_error(time):
    """A Python __exit__ that raises chains the error it was handed; the timer's did not, so a hook's
    error hid the block's (found 2026-10-30)."""
    report = pygim.Report.hooks()
    report.on_finished(lambda snapshot: (_ for _ in ()).throw(ValueError("the hook")))
    with pytest.raises(ValueError, match="the hook") as raised:
        with pygim.QuickTimer("job", clock=time, report=report):
            raise KeyError("the block")
    assert isinstance(raised.value.__context__, KeyError)
    with pytest.raises(ValueError, match="the hook") as raised:
        with pygim.QuickTimer("job", clock=time, report=report):
            pass
    assert raised.value.__context__ is None                     # nothing to chain when the block ended well


@pytest.mark.parametrize("given", [{"clock": 5}, {"report": "stderr"}], ids=["clock", "report"])
def test_a_clock_or_report_of_another_type_is_a_type_error(given):
    with pytest.raises(TypeError, match="ManualTime|Report"):
        pygim.QuickTimer("job", **given)


def test_a_timer_dropped_unfinished_still_reports(time):
    report, events = recorded()
    timer = pygim.QuickTimer("job", clock=time, report=report)
    del timer
    assert events[-1][0] == "finished"


def test_the_lap_calls_have_one_name_each():
    """start_sub_timer and stop_sub_timer became start_lap and stop_lap (D62, 2026-10-30), with no
    alias: pygim is below 1.0, and a rename there needs no deprecation (Debith, 2026-10-31)."""
    assert [name for name in dir(pygim.QuickTimer) if "sub_timer" in name] == []


# ── what a with block asks of a timer (Debith on PR #39, 2026-11-01: the benchmarks' stopwatch,
# elapsed and timed helpers "are sign quick timer is not doing its job") ───────────────────────

def test_silent_is_a_timer_that_tells_nobody(time, capfd):
    with pygim.QuickTimer.silent("job", clock=time) as timer:
        time.advance(ms=3)
    assert capfd.readouterr().err == "" and timer.snapshot().name == "job"
    assert pygim.QuickTimer.silent().snapshot().name == "QuickTimer"


def test_total_and_laps_are_read_off_the_timer(time):
    with pygim.QuickTimer.silent("job", clock=time) as timer:
        timer.start_lap("read")
        time.advance(ms=2)
        assert timer.total == ms(2)   # running: up to now
        time.advance(ms=1)
    time.advance(s=5)
    assert (timer.total, timer.laps) == (ms(3), {"read": ms(3)}) == (timer.snapshot().total, timer.snapshot().laps)


def test_a_lap_block_starts_its_lap_and_stops_it_as_it_ends(time):
    with pygim.QuickTimer.silent("job", clock=time) as timer:
        with timer.lap("read") as read:
            time.advance(ms=2)
        time.advance(ms=7)          # between the blocks: no lap runs
        with timer.lap("parse"):
            time.advance(ms=4)
        with timer.lap("read"):        # again: the lap's time adds up
            time.advance(ms=1)
    assert timer.laps == {"read": ms(3), "parse": ms(4)} and read.took == ms(3) and read.name == "read"
    assert timer.total == ms(14)


def test_a_lap_block_that_raises_still_stops_its_lap(time):
    timer = pygim.QuickTimer.silent("job", clock=time)
    with pytest.raises(KeyError):
        with timer.lap("read"):
            time.advance(ms=2)
            raise KeyError("x")
    time.advance(ms=5)
    assert timer.laps == {"read": ms(2)}


def test_a_hook_that_raises_as_a_lap_block_ends_keeps_the_blocks_own_error(time):
    report = pygim.Report.hooks()
    report.on_stopped(lambda name, lap, took: (_ for _ in ()).throw(RuntimeError("hook")))
    timer = pygim.QuickTimer("job", clock=time, report=report)
    with pytest.raises(RuntimeError, match="hook") as raised:
        with timer.lap("read"):
            raise KeyError("block")
    assert isinstance(raised.value.__context__, KeyError)
