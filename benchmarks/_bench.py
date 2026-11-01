"""Shared benchmark helpers: timing that is honest about what is alive, a
synthetic path corpus, RSS, and the command line's folders.

Import from a benchmark next to this file (``from _bench import best, corpus``).
"""

import argparse
import gc
import random
import sys

from pygim import path
from pygim.utils import QuickTimer
from pygim.utils import rss_mb  # the benchmarks import it from here: one syscall, every platform

REPS = 3


def best(fn, reps=REPS):
    """Best-of-N wall time in seconds and the last result. The previous pass's
    result stays alive during the next pass (a *warm* measurement when fn caches)."""
    t_best, r = float("inf"), None
    for _ in range(reps):
        gc.collect()
        gc.disable()
        with QuickTimer.silent() as timer:   # pygim's own timer, on the monotonic clock (D83)
            r = fn()
        t_best = min(t_best, timer.total.seconds)
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
        with QuickTimer.silent() as timer:
            r = fn()
        t_best = min(t_best, timer.total.seconds)
        gc.enable()
    return t_best, r


def ns(seconds, n):
    return seconds / n * 1e9


def folder(text):
    """A folder named on the command line, as the whole run uses it: a pygim path, ``~`` expanded and
    resolved here, where it is read, and refused here when it is no folder — before anything is
    measured, and never resolved again below. An argparse ``type=``, or ``argument("--root", folder)``."""
    named = path(text).expanduser().resolve()
    if not named.is_dir():
        raise argparse.ArgumentTypeError(f"{text} is not a folder")
    return named


def argument(name, convert=str):
    """The value after *name* on the command line, converted at once (``argument("--root", folder)``);
    None when it is not given. A value *convert* refuses ends the run before anything is measured."""
    if name not in sys.argv[:-1]:
        return None
    try:
        return convert(sys.argv[sys.argv.index(name) + 1])
    except argparse.ArgumentTypeError as e:
        sys.exit(f"{name}: {e}")




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
