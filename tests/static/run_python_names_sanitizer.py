#!/usr/bin/env python
"""Run utils/python_names.h under AddressSanitizer and UndefinedBehaviorSanitizer over a seed corpus
with bounded, deterministic mutations.

What this is, and is not. It is sanitizer-backed adversarial testing: the naming functions are
driven over a corpus while ASan and UBSan watch for out-of-bounds reads, bad string spans and
undefined behaviour, and the run aborts on any. It is NOT a coverage-guided fuzzer: no libFuzzer or
AFL is installed here (no clang, g++ has no -fsanitize=fuzzer), so there is no feedback-driven input
search — this tool reports that, and does not call its bounded mutation loop a coverage-guided run.

Steps (artifacts under build/callspace/python_names-sanitizer/run-<ns>/):
1. discover the toolchain: which compiler accepts -fsanitize=address,undefined, and whether a
   fuzzing engine exists; report both;
2. build a seed corpus from the model's declared name atoms (python_names_callspace.py) and add
   bounded deterministic mutations (byte flips, NUL insertion, truncation, doubling, prefix growth);
3. compile and run the instrumented harness over the corpus; a clean exit is the pass;
4. negative control: a copy of the header with an out-of-bounds read must make the sanitizer abort,
   so the instrumentation is shown to be live.

Run: python tests/static/run_python_names_sanitizer.py [--compiler g++-16]
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests" / "unittests"))

import python_names_callspace as model  # noqa: E402

HEADER = ROOT / "src" / "_pygim_fast" / "utils" / "python_names.h"
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def run(argv, cwd, env=None, timeout=300):
    return subprocess.run([str(a) for a in argv], cwd=str(cwd), env=env, capture_output=True, text=True,
                          timeout=timeout)


def escape(text):
    """A corpus line: every byte as \\xHH so NULs and non-UTF-8 survive a text file."""
    return "".join(f"\\x{byte:02x}" for byte in text.encode("utf-8", "surrogatepass"))


def seed_corpus():
    """Declared name atoms, plus the class names and some hostile extras — the seeds before mutation."""
    seeds = set()
    for domain in (model.CLASS_NAMES, model.STEMS):
        seeds.update(domain.values)
    seeds.update(("__x", "_C__x", "__x__", "x", "", "_", "__", "___", "_Trait__x", "a.b", "__a.b"))
    seeds.update(("greet\x00x", "\udc80", "_" * 40, "x" * 64, "ɡ", "á"))
    return sorted(seeds)


def mutate(seeds):
    """Bounded, deterministic mutations of each seed: these are a fixed set, not a guided search."""
    out = set(seeds)
    for seed in seeds:
        raw = seed.encode("utf-8", "surrogatepass")
        out.add((seed + "\x00tail"))                 # a NUL in the middle
        out.add(seed[:1])                            # truncation
        out.add(seed + seed)                         # doubling
        out.add("_" + seed)                          # a grown leading underscore run
        out.add(seed + "_")                          # a grown trailing run
        for i in range(min(len(raw), 4)):            # a few single-byte flips, decoded leniently
            flipped = bytes(b ^ 0x20 if j == i else b for j, b in enumerate(raw))
            out.add(flipped.decode("utf-8", "replace"))
    return sorted(out)


def write_corpus(path, entries):
    path.write_text("\n".join(escape(e) for e in entries) + "\n", encoding="utf-8")


def discover(compiler):
    report = {"compiler": compiler, "address_undefined": False, "fuzzer": False, "notes": []}
    # A trivial translation unit on stdin: does the compiler accept the sanitizer flags?
    check = subprocess.run([compiler, "-x", "c++", "-std=c++26", *SANITIZE, "-fsyntax-only", "-"],
                           input="int main(){}", text=True, capture_output=True)
    report["address_undefined"] = check.returncode == 0
    if not report["address_undefined"]:
        report["notes"].append("compiler does not accept -fsanitize=address,undefined: " + check.stderr.strip()[:200])
    fuzz = subprocess.run([compiler, "-x", "c++", "-fsanitize=fuzzer", "-fsyntax-only", "-"],
                          input="extern \"C\" int LLVMFuzzerTestOneInput(const unsigned char*,unsigned long){return 0;}",
                          text=True, capture_output=True)
    report["fuzzer"] = fuzz.returncode == 0
    report["notes"].append("coverage-guided fuzzing engine: " + ("available" if report["fuzzer"]
                           else "NOT available (no -fsanitize=fuzzer; no clang/libFuzzer/AFL on PATH)"))
    return report


def build_and_run(compiler, out, header_dir, corpus, label):
    binary = out / f"harness-{label}"
    argv = [compiler, "-std=c++26", "-O1", "-g", *SANITIZE, f"-I{header_dir}", f"-I{HEADER.parent}", HERE / "python_names_sanitizer.cpp",
            "-o", binary]
    build = run(argv, out)
    (out / f"build-{label}.log").write_text(build.stdout + build.stderr, encoding="utf-8")
    if build.returncode:
        raise SystemExit(f"{label}: harness did not compile — see {out / f'build-{label}.log'}")
    env = dict(os.environ, ASAN_OPTIONS="abort_on_error=1:detect_leaks=0", UBSAN_OPTIONS="abort_on_error=1:print_stacktrace=1")
    execution = run([binary, corpus], out, env=env)
    (out / f"run-{label}.log").write_text(execution.stdout + execution.stderr, encoding="utf-8")
    return execution


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default="/usr/bin/g++-16")
    parser.add_argument("--output", default=str(ROOT / "build" / "callspace" / "python_names-sanitizer"))
    args = parser.parse_args(argv)
    out = Path(args.output) / f"run-{time.time_ns()}"
    out.mkdir(parents=True)

    report = discover(args.compiler)
    if not report["address_undefined"]:
        print("\n".join(report["notes"]))
        raise SystemExit(f"{args.compiler} cannot build the sanitizer harness; nothing run.")

    corpus = mutate(seed_corpus())
    corpus_file = out / "corpus.txt"
    write_corpus(corpus_file, corpus)
    report["corpus_size"] = len(corpus)

    clean = build_and_run(args.compiler, out, HEADER.parent, corpus_file, "clean")
    report["clean_run_ok"] = clean.returncode == 0 and "harness:" in clean.stderr

    # Negative control: an out-of-bounds read planted in a copy of the header must abort the run.
    header_text = HEADER.read_text(encoding="utf-8")
    old = "const auto first = class_name.find_first_not_of('_');"
    new = (old + "\n    if (first == std::string_view::npos && !class_name.empty())"
                 " { volatile char c = class_name.data()[class_name.size() + 100000]; (void)c; }")
    assert header_text.count(old) == 1, "the negative-control anchor is gone; update it"
    control_dir = out / "control"
    (control_dir).mkdir()
    (control_dir / "python_names.h").write_text(header_text.replace(old, new, 1), encoding="utf-8")
    control = build_and_run(args.compiler, control_dir, control_dir, corpus_file, "control")
    report["negative_control_aborts"] = control.returncode != 0 and (
        "AddressSanitizer" in control.stderr or "runtime error" in control.stderr or control.returncode < 0)

    lines = ["utils/python_names.h under AddressSanitizer + UndefinedBehaviorSanitizer", "",
             f"compiler: {report['compiler']}", f"corpus: {report['corpus_size']} inputs (seeds + bounded mutations)",
             f"artifacts: {out}", ""]
    lines += report["notes"]
    lines += ["", f"instrumented run clean: {report['clean_run_ok']}",
              f"negative control (planted OOB read) aborts: {report['negative_control_aborts']}"]
    summary = "\n".join(lines)
    (out / "report.txt").write_text(summary + "\n", encoding="utf-8")
    (Path(args.output) / "latest.json").write_text(f'{{"artifacts": "{out}"}}\n', encoding="utf-8")
    print(summary)
    ok = report["clean_run_ok"] and report["negative_control_aborts"]
    print("\nRESULT:", "sanitizers clean and live" if ok else "A CHECK FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
