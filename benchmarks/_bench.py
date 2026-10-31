"""Shared benchmark helpers: timing that is honest about what is alive, a
synthetic path corpus, and RSS.

Import from a benchmark next to this file (``from _bench import best, corpus``).
"""

import gc
import random

from pygim import utils
from pygim.utils import rss_mb  # the benchmarks import it from here: one syscall, every platform

REPS = 3


def stopwatch(name="benchmark"):
    """pygim's own timer, silent: utils.QuickTimer on the monotonic clock (D83). Time a block with
    ``with stopwatch() as t:``, then read ``elapsed(t)``."""
    return utils.QuickTimer(name, report=utils.Report.silent())


def elapsed(timer):
    """How long a stopwatch's block took, in seconds."""
    return timer.snapshot().total.seconds


def timed(fn, *args, **kwargs):
    """(seconds, fn(*args, **kwargs)), timed by a stopwatch."""
    with stopwatch() as t:
        result = fn(*args, **kwargs)
    return elapsed(t), result


def best(fn, reps=REPS):
    """Best-of-N wall time in seconds and the last result. The previous pass's
    result stays alive during the next pass (a *warm* measurement when fn caches)."""
    t_best, r = float("inf"), None
    for _ in range(reps):
        gc.collect()
        gc.disable()
        t, r = timed(fn)
        t_best = min(t_best, t)
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
        t, r = timed(fn)
        t_best = min(t_best, t)
        gc.enable()
    return t_best, r


def ns(seconds, n):
    return seconds / n * 1e9




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
