#!/usr/bin/env python
"""Compile every compile-time proof suite with the sanitizers on: a sanitizer build must build.

AddressSanitizer and UndefinedBehaviorSanitizer instrument the code they compile, and GCC's
UBSan instrumentation also reaches constant evaluation: a pointer into a constexpr table compared
with nullptr is no longer a constant expression there (51477db broke every keyword-table proof
that way, unseen by pytest and CI, which never build with sanitizers). This runner compiles each
tests/static/*_proofs.cpp, syntax only, with -fsanitize=address,undefined under every GCC found
(g++, g++-16), so a header that a sanitizer build cannot compile fails here first.

It is not the sanitizer run itself (running instrumented code over inputs); it is the step every
such run starts from.

Run:  python tests/static/run_proofs_under_sanitizers.py
Ends with "every proof compiles under the sanitizers" and exit 0, or names each failure, exit 1.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys

from pygim.pathlike import path

HERE = path(os.path.dirname(os.path.abspath(__file__)))
ROOT = HERE.parent.parent
FLAGS = ["-std=c++26", "-fsyntax-only", "-fsanitize=address,undefined", "-I", str(ROOT / "src" / "_pygim_fast")]


def compilers():
    return [c for c in ("g++", "g++-16") if shutil.which(c)]


def main():
    proofs = list(HERE.glob("*_proofs.cpp"))   # sorted
    failed = []
    for cxx in compilers():
        for proof in proofs:
            result = subprocess.run([cxx, *FLAGS, str(proof)], capture_output=True, text=True)
            status = "ok" if result.returncode == 0 else "FAILED"
            print(f"{cxx:8} {proof.name:32} {status}")
            if result.returncode != 0:
                first = next((line for line in result.stderr.splitlines() if "error:" in line), result.stderr[:200])
                print(f"         {first[:220]}")
                failed.append((cxx, proof.name))
    if failed:
        print(f"{len(failed)} proof suite(s) do not compile under the sanitizers")
        return 1
    print("every proof compiles under the sanitizers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
