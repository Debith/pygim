# -*- coding: utf-8 -*-
"""The process-memory probes (utils/memory.h), measured the only way that means
anything: a before/after delta on a FRESH heap, in a subprocess. In one process a
freed block is kept by the allocator and reused by the next allocation of the
same size, so a second measurement may not move at all (the examples run before
this suite and allocate the same 64 MiB)."""

import subprocess
import sys

PROBE = """
from pygim import utils
before = utils.rss_bytes()
block = bytearray(64 * 2**20)
block[::4096] = b"x" * len(block[::4096])
after = utils.rss_bytes()
print(before, after, utils.peak_rss_bytes())
"""


def test_rss_grows_with_a_large_allocation_on_a_fresh_heap():
    out = subprocess.run([sys.executable, "-c", PROBE], capture_output=True, text=True, check=True).stdout.split()
    before, after, peak = (int(x) for x in out)
    assert before > 0
    assert after - before > 32 * 2**20, (before, after)     # 64 MiB touched page by page; the OS may round
    assert peak >= after


def test_rss_probes_are_consistent():
    from pygim import utils

    rss = utils.rss_bytes()
    assert rss > 0 and utils.peak_rss_bytes() >= rss
    assert abs(utils.rss_mb() - rss / 2**20) < 1.0
    assert utils.peak_rss_mb() >= utils.rss_mb() - 1.0
