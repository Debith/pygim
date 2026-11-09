# type: ignore
"""Time as one number: ``pygim.Duration``, ``Timestamp`` and ``ManualTime``.

A length of time is one signed 64-bit count of nanoseconds, and every unit is
a conversion into it. A moment is one too: nanoseconds since its clock's
epoch. So two moments subtract to a duration, and units, seconds as a float
and the repr are views of the one number, never a second store.

This example demonstrates:
- Spelling a duration in units, and reading it back in nanoseconds or seconds
- A fraction of a unit rounded exactly, and the refusals past 64 bits
- A moment from the wall clock, and the duration between two moments
- Time that moves only when told, for testing code that keeps time
"""

import time

from pygim import Duration, ManualTime, Timestamp

# ----------------------------------------------------------------------------
# 1. A duration is spelt in units and held as nanoseconds
# ----------------------------------------------------------------------------
#               ┌─ **units: yr, wk, day, hr, min, s, ms, us, ns; they add up
#               ▼
wait = Duration(min=1, s=30)
assert wait.nanoseconds == 90_000_000_000
assert wait.seconds == 90.0                      # a view as a float, not a store
assert repr(wait) == "Duration(min=1, s=30)"

# The repr spells it in the longest units that fit, so another spelling of the
# same length reads back the same.
assert Duration(s=150) == Duration(min=2, s=30)
assert repr(Duration(s=150)) == "Duration(min=2, s=30)"

# ----------------------------------------------------------------------------
# 2. A fraction rounds exactly, to the nearest nanosecond, ties to even
# ----------------------------------------------------------------------------
assert Duration(ns=2.5).nanoseconds == 2
assert Duration(ns=3.5).nanoseconds == 4
# A year is 365.2425 days, as C++ counts it: 55 bits of nanoseconds, where a
# float product would be one nanosecond off.
assert Duration(yr=0.286).nanoseconds == 9_025_288_271_999_999

# Past 64 bits (about 292 years) is an error, never a wrapped value.
try:
    Duration(yr=300)
except OverflowError as e:
    assert "64 bits" in str(e)
else:
    raise AssertionError("Expected 300 years to be past 64 bits")

try:
    Duration(months=1)
except TypeError as e:
    assert "unknown unit 'months'" in str(e)
else:
    raise AssertionError("Expected months to be an unknown unit")

# ----------------------------------------------------------------------------
# 3. A moment is nanoseconds since the Unix epoch
# ----------------------------------------------------------------------------
#                 ┌─ nanoseconds: after 1970-01-01T00:00:00Z
#                 ▼
start = Timestamp(1_000)
end = Timestamp(3_500)
assert end - start == Duration(ns=2_500)            # two moments: the time between
assert start < end

now = Timestamp.now()                               # the wall clock
assert abs(now.nanoseconds - time.time_ns()) < 1_000_000_000

# ----------------------------------------------------------------------------
# 4. Time that moves only when told
# ----------------------------------------------------------------------------
# Code that keeps time takes its clock, so a test can give it this one and
# know every reading exactly.
clock = ManualTime()
assert clock.now() == 0
#             ┌─ units, as Duration takes them
#             ▼
clock.advance(min=1, s=30)
assert clock.now() == 90_000_000_000
#             ┌─ by: a Duration instead of units
#             ▼
clock.advance(Duration(s=-30))
assert clock.now() == 60_000_000_000

print("durations and timestamps example OK:", wait)
