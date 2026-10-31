#!/usr/bin/env python
"""Build tests/static/byte_scan_paths.cpp and run it: every path of utils/byte_scan.h — the SSE2 or
NEON loops, the AVX2 ones where the processor has them, the dispatch — against the scalar
definition, at every offset around their steps. The dispatch runs one path a machine, so a test
through it (test_code_parse.py's) checks only that one; this calls each directly. Built plain,
with -DPYGIM_BYTE_SCAN_SCALAR (the definition alone), and under ASan and UBSan.

Run:  python tests/static/run_byte_scan_paths.py   (after changing utils/byte_scan.h or utils/cpu.h)
Ends with "every byte_scan path agrees with the definition" and exit 0, or names each failure, exit 1.
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "byte_scan_paths.cpp")
BUILDS = {
    "plain": ["-O2"],
    "scalar": ["-O2", "-DPYGIM_BYTE_SCAN_SCALAR"],
    "address,undefined": ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"],
}


def main():
    cxx = next((c for c in ("g++-16", "g++") if shutil.which(c)), None)
    if cxx is None:
        print("no g++ found")
        return 1
    failed = []
    with tempfile.TemporaryDirectory() as out:
        for name, flags in BUILDS.items():
            binary = os.path.join(out, "byte_scan_paths_" + name.replace(",", "_"))
            built = subprocess.run([cxx, "-std=c++26", *flags, SOURCE, "-o", binary], capture_output=True, text=True)
            if built.returncode != 0:
                print(f"{name:18} does not build:\n{built.stderr[:2000]}")
                failed.append(name)
                continue
            ran = subprocess.run([binary], capture_output=True, text=True, timeout=600,
                                 env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1"})
            print(f"-- {name}\n{ran.stdout.rstrip()}")
            if ran.returncode != 0:
                print(ran.stderr[-4000:])
                failed.append(name)
    if failed:
        print(f"byte_scan paths disagree or fail under: {', '.join(failed)}")
        return 1
    print("every byte_scan path agrees with the definition")
    return 0


if __name__ == "__main__":
    sys.exit(main())
