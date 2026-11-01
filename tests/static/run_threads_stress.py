#!/usr/bin/env python
"""Build tests/static/threads_stress.cpp under each sanitizer and run it: the parked pool, the kept
blocks and the interrupt scopes, the machinery every thread of a process shares, under
ThreadSanitizer, and under AddressSanitizer with UndefinedBehaviorSanitizer.

pytest cannot see these defects: a race, a block lent twice or a lock a fork left held shows only
under load, in a build instrumented for it. Run this after changing utils/parallel.h,
utils/kept_blocks.h or utils/interrupt.h (and what they include).

Run:  python tests/static/run_threads_stress.py
Ends with "the threads stress held under every sanitizer" and exit 0, or names each failure, exit 1.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile

from pygim.pathlike import path

HERE = path(os.path.dirname(os.path.abspath(__file__)))
SOURCE = HERE / "threads_stress.cpp"
SANITIZERS = {
    "thread": (["-fsanitize=thread"], {"TSAN_OPTIONS": "halt_on_error=1:die_after_fork=0"}),   # a forked child starts threads: the fork part tests it
    "address,undefined": (["-fsanitize=address,undefined", "-fno-sanitize-recover=all"],
                          {"ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1", "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}),
}
FLAGS = ["-std=c++26", "-O1", "-g", "-fno-omit-frame-pointer", "-pthread"]


def compiler():
    return next((c for c in ("g++-16", "g++") if shutil.which(c)), None)


def main():
    cxx = compiler()
    if cxx is None:
        print("no g++ found")
        return 1
    failed = []
    with tempfile.TemporaryDirectory() as out:
        for name, (flags, env) in SANITIZERS.items():
            binary = os.path.join(out, "threads_stress_" + name.replace(",", "_"))
            built = subprocess.run([cxx, *FLAGS, *flags, str(SOURCE), "-o", binary], capture_output=True, text=True)
            if built.returncode != 0:
                print(f"{name:18} does not build:\n{built.stderr[:2000]}")
                failed.append(name)
                continue
            ran = subprocess.run([binary], capture_output=True, text=True, env={**os.environ, **env}, timeout=600)
            print(f"-- {name}\n{ran.stdout.rstrip()}")
            if ran.returncode != 0:
                print(ran.stderr[-4000:])
                failed.append(name)
    if failed:
        print(f"the threads stress failed under: {', '.join(failed)}")
        return 1
    print("the threads stress held under every sanitizer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
