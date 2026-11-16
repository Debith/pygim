#!/usr/bin/env python
"""Build tests/static/threads_stress.cpp under each sanitizer and run it: the pool every thread of a
process shares (core/primitives/parallel.h), under ThreadSanitizer, and under AddressSanitizer with
UndefinedBehaviorSanitizer.

pytest cannot see these defects: a race, a helper left inside a finished job or a lock a fork left
held shows only under load, in a build instrumented for it. CI's threads-stress job runs this on
every pull request; run it yourself after changing core/primitives/parallel.h or what it includes.

It needs a C++ compiler and nothing of pygim's build: the program is the headers it includes, so the
job runs without installing pygim.

Run:  python tests/static/run_threads_stress.py [part ...]
Ends with "the threads stress held under every sanitizer" and exit 0, or names each failure, exit 1.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile

SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "threads_stress.cpp")
SANITIZERS = {
    "thread": (["-fsanitize=thread"], {"TSAN_OPTIONS": "halt_on_error=1:die_after_fork=0"}),   # a forked child starts threads: the fork part tests it
    "address,undefined": (["-fsanitize=address,undefined", "-fno-sanitize-recover=all"],
                          {"ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1", "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}),
}
# The standard pygim.utils is built with (setup.py's default; ext.utils.toml sets no other).
FLAGS = ["-std=c++23", "-O1", "-g", "-fno-omit-frame-pointer", "-pthread"]


def compiler():
    return next((c for c in (os.environ.get("CXX"), "g++-14", "g++") if c and shutil.which(c)), None)


def main(parts):
    cxx = compiler()
    if cxx is None:
        print("no g++ found")
        return 1
    failed = []
    with tempfile.TemporaryDirectory() as out:
        for name, (flags, env) in SANITIZERS.items():
            binary = os.path.join(out, "threads_stress_" + name.replace(",", "_"))
            built = subprocess.run([cxx, *FLAGS, *flags, SOURCE, "-o", binary], capture_output=True, text=True)
            if built.returncode != 0:
                print(f"{name:18} does not build:\n{built.stderr[:2000]}")
                failed.append(name)
                continue
            ran = subprocess.run([binary, *parts], capture_output=True, text=True, env={**os.environ, **env}, timeout=900)
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
    sys.exit(main(sys.argv[1:]))
