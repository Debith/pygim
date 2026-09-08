# type: ignore
"""Measuring memory: ``pygim.utils.rss_bytes`` and friends.

Four probes, one syscall each, on Linux, macOS and Windows: the process's
resident set size now and its peak, in bytes and in mebibytes. They answer
"how much physical memory does this process hold", which makes them a
benchmark's before/after probe — never a per-object size. Components that
want to report their own size do so exactly (``PathSet.stats()["bytes"]``,
``PathStore.stats()["bytes"]``).

This example demonstrates:
- Reading the current and peak resident size
- A before/after delta around an allocation, and why the delta is what counts
- Why a second measurement in the same process may not move
"""

import gc

from pygim import utils

# ----------------------------------------------------------------------------
# 1. The probes
# ----------------------------------------------------------------------------
now = utils.rss_bytes()                      # what the OS holds for us right now
peak = utils.peak_rss_bytes()                # the high-water mark so far
assert now > 0 and peak >= now
assert abs(utils.rss_mb() - now / 2**20) < 1.0

# ----------------------------------------------------------------------------
# 2. A before/after delta around an allocation
# ----------------------------------------------------------------------------
#          ┌─ read before
#          ▼
before = utils.rss_bytes()
block = bytearray(64 * 2**20)                # 64 MiB, and touch every page so it is resident
block[::4096] = b"x" * len(block[::4096])
after = utils.rss_bytes()
assert after - before > 32 * 2**20           # the delta is the allocation (the OS may round)
assert utils.peak_rss_bytes() >= after

# ----------------------------------------------------------------------------
# 3. Why only the delta means anything
# ----------------------------------------------------------------------------
# Freed memory is kept by the allocator for reuse, so the resident size does
# not fall back to `before` — and a second allocation of the same size may
# not raise it at all. Measure each variant on a fresh heap (a subprocess),
# as benchmarks/path_store.py does; never compare two measurements taken
# one after the other in one process.
del block
gc.collect()
assert utils.rss_bytes() >= before - 2**20   # not back to `before`; that is expected

print(f"process memory example OK: {utils.rss_mb():.1f} MiB now, {utils.peak_rss_mb():.1f} MiB peak")
