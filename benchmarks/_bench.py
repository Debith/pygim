"""Shared benchmark helpers: timing that is honest about what is alive, a
synthetic path corpus, and RSS.

Import from a benchmark next to this file (``from _bench import best, corpus``).
"""

import gc
import os
import random
import time

REPS = 3


def best(fn, reps=REPS):
    """Best-of-N wall time in seconds and the last result. The previous pass's
    result stays alive during the next pass (a *warm* measurement when fn caches)."""
    t_best, r = float("inf"), None
    for _ in range(reps):
        gc.collect()
        gc.disable()
        t0 = time.perf_counter()
        r = fn()
        t_best = min(t_best, time.perf_counter() - t0)
        gc.enable()
    return t_best, r


def best_fresh(fn, reps=REPS):
    """best() but the previous pass's objects are dropped before each timing —
    so a cached-but-dead value is measured as such, not as warm."""
    t_best, r = float("inf"), None
    for _ in range(reps):
        r = None
        gc.collect()
        gc.disable()
        t0 = time.perf_counter()
        r = fn()
        t_best = min(t_best, time.perf_counter() - t0)
        gc.enable()
    return t_best, r


def ns(seconds, n):
    return seconds / n * 1e9


def rss_mb():
    """The process's resident set size in MiB: pygim.utils.rss_mb (one syscall,
    every platform), with the procfs read as the fallback for an unbuilt tree."""
    try:
        from pygim.utils import rss_mb as _rss
        return _rss()
    except ImportError:
        pass
    try:
        with open("/proc/self/statm") as f:
            return int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE") / 2**20
    except OSError:
        return float("nan")


def corpus(n, seed=1):
    """n synthetic paths: 8 top-level dirs, depth 1-6, half absolute, common suffixes."""
    rng = random.Random(seed)
    dirs = ["home", "var", "usr", "opt", "srv", "data", "projects", "tmp"]
    exts = [".yaml", ".json", ".toml", ".jsonl", ".txt", ".md", ".yml", ""]
    out = []
    for i in range(n):
        depth = rng.randint(1, 6)
        parts = [rng.choice(dirs)] + [f"d{rng.randrange(1000)}" for _ in range(depth - 1)]
        out.append(("/" if rng.random() < 0.5 else "") + "/".join(parts + [f"file{i}{rng.choice(exts)}"]))
    return out
