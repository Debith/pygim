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
# 2. A before/after delta around an allocation — on a fresh heap
# ----------------------------------------------------------------------------
# Freed memory is kept by the allocator for reuse, so in a process that has
# already allocated and freed a block, a new block of the same size may not
# raise the resident size at all. The honest measurement is a subprocess:
# read before, allocate, touch every page, read after.
import subprocess
import sys

PROBE = """
from pygim import utils
before = utils.rss_bytes()
block = bytearray(64 * 2**20)                # 64 MiB
block[::4096] = b"x" * len(block[::4096])    # touched, so it is resident
print(before, utils.rss_bytes(), utils.peak_rss_bytes())
"""
before, after, peak_after = (int(x) for x in subprocess.run(
    [sys.executable, "-c", PROBE], capture_output=True, text=True, check=True).stdout.split())
assert after - before > 32 * 2**20           # the delta is the allocation (the OS may round)
assert peak_after >= after

# ----------------------------------------------------------------------------
# 3. Why only the delta means anything
# ----------------------------------------------------------------------------
# Two measurements taken one after the other in one process compare a heap
# that remembers everything freed before; benchmarks/path_store.py measures
# every variant in its own subprocess for exactly this reason.

print(f"process memory example OK: {utils.rss_mb():.1f} MiB now, {utils.peak_rss_mb():.1f} MiB peak; "
      f"a fresh 64 MiB block cost {(after - before) / 2**20:.0f} MiB")
