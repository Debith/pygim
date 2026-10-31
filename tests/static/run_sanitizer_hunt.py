#!/usr/bin/env python
"""Run both source readers under ASan and UBSan over real files and their mutants, until a deadline.

What this is, and is not. It is sanitizer-backed adversarial testing: pygim.code's Python parser
(source checks, lexer, parser, tree, dump) and pygim.similarity's two frontends (Python, C/C++)
with every fingerprint are driven over real corpora and deterministic mutants of them while
AddressSanitizer and UndefinedBehaviorSanitizer watch; an input that takes over ten seconds is a
hang. It is NOT a coverage-guided fuzzer: no libFuzzer or AFL here (no clang; g++ has no
-fsanitize=fuzzer), so inputs are mutated blindly (tokens deleted, doubled, replaced, swapped,
grammar pieces inserted, bytes changed, cut short, deep nesting, spans repeated).

Units. Unit u of a target is one process of tests/static/sanitizer_hunt.cpp: the first units read
the corpus as it is (200 files each), the rest read UNIT_SIZE mutants each, input i of unit u made
again from (target, u, i) alone. Each finished unit writes <out>/units/<target>-<u>.json; a
restart skips them. A finding — the process dies on a sanitizer report, or hangs — is written to
<out>/findings/<target>-u<u>-i<i>.json with the report, its input stays in <out>/crashes/, and the
unit goes on from the next input (at most MAX_FINDINGS a unit). No model, no network. Files are
walked, read and written through pygim's own paths.

Run:   python tests/static/run_sanitizer_hunt.py OUT --python-roots DIR... --cpp-roots DIR...
       [--hours 8] [--workers 10]
Ends:  "hunt done: N units, M inputs, K findings" — and exit 1 when K > 0.
"""

from __future__ import annotations

import argparse
import concurrent.futures as futures
import json
import os
import re
import subprocess
import sys
import time

from pygim.pathlike import path

HERE = path(os.path.dirname(os.path.abspath(__file__)))
UNIT_SIZE = {"python": 2000, "cpp": 1000}
MAX_SEED_BYTES = 32 * 1024
MAX_FINDINGS = 20
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-g", "-O2"]
# Both end a report in abort(), where the harness writes the input it died on and names it.
SANITIZER_OPTIONS = {"ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1",
                     "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1:abort_on_error=1"}
DIED_ON = re.compile(r"sanitizer_hunt: died on input (\d+)")
EXTENSIONS = {"python": {".py"}, "cpp": {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"}}


def build(out, name="sanitizer_hunt", defines=()):
    binary = out / name
    if not binary.exists():
        subprocess.run(["g++", "-std=c++26", *SANITIZE, *defines, str(HERE / "sanitizer_hunt.cpp"), "-o", str(binary)], check=True)
    return binary


def died_on(stderr):
    """The input a dying process had in hand, as it named it last on stderr; None before any."""
    found = DIED_ON.findall(stderr)
    return int(found[-1]) if found else None


def negative_control(out):
    """A build with two planted defects must die on each, its input written and named: the
    instrumentation is live."""
    binary = build(out, "sanitizer_hunt_planted", ["-DHUNT_PLANTED_DEFECTS"])
    probe = out / "control"
    (probe / "crashes").mkdir(parents=True, exist_ok=True)
    caught = []
    for tag, text in (("A", "#A heap read past the end\nx = 1\n"), ("U", "#U signed overflow\nx = 1\n")):
        source = probe / f"{tag}.py"
        source.write_bytes(text.encode())
        listing = probe / f"{tag}.txt"
        listing.write_bytes(f"{source}\n".encode())
        crash = probe / "crashes" / "python-u0-i0.bin"
        crash.unlink(missing_ok=True)
        proc = subprocess.run([str(binary), "python", str(listing), "0", "0", "1", str(probe / "crashes")],
                              capture_output=True, text=True, env=dict(os.environ, **SANITIZER_OPTIONS))
        report = "heap-buffer-overflow" if tag == "A" else "signed integer overflow"
        caught.append(proc.returncode != 0 and report in proc.stderr and died_on(proc.stderr) == 0
                      and crash.exists() and crash.read_bytes() == text.encode())
    if not all(caught):
        raise SystemExit(f"negative control failed (address, undefined caught: {caught}): the hunt would find nothing")
    print("negative control: a planted heap read and a planted signed overflow each caught, their inputs written", flush=True)


def seed_list(out, target, roots):
    """The files mutants are made from: the target's sources under `roots`, up to 32 KB, sorted, and
    copied into the run's own directory, so a run outlives its sources and can be made again."""
    listing = out / f"{target}-files.txt"
    if not listing.exists():
        found = sorted((p for root in roots for p in path(root).rglob("*")
                        if p.suffix in EXTENSIONS[target] and p.is_file() and 0 < p.size() <= MAX_SEED_BYTES), key=str)
        seeds = out / "seeds" / target
        seeds.mkdir(parents=True, exist_ok=True)
        copies = []
        for i, p in enumerate(found):
            copy = seeds / f"{i}{p.suffix}"
            copy.write_bytes(p.read_bytes())
            copies.append(str(copy))
        write_atomically(out / f"{target}-sources.txt", "\n".join(str(p) for p in found) + "\n")   # where each copy came from
        write_atomically(listing, "\n".join(copies) + "\n")
    return listing, listing.read_bytes().count(b"\n")


def write_atomically(where, text):
    where.write_bytes(text.encode(), atomic=True)


def run_unit(binary, out, target, listing, unit, real_units):
    """One unit to its end, findings recorded on the way; returns its summary."""
    count = 200 if unit < real_units else UNIT_SIZE[target]
    first, inputs, refused, slowest_ms, slowest_i, findings = 0, 0, 0, 0.0, None, 0
    env = dict(os.environ, **SANITIZER_OPTIONS)
    while first < count and findings < MAX_FINDINGS:
        proc = subprocess.run([str(binary), target, str(listing), str(unit), str(first), str(count - first), str(out / "crashes")],
                              capture_output=True, text=True, env=env, timeout=3600)
        if proc.returncode == 0:
            r = json.loads(proc.stdout.strip().splitlines()[-1])
            inputs += r["inputs"]
            refused += r["refused"]
            if r["slowest_ms"] > slowest_ms:   # its index, so the input can be timed again alone: under load the time is noise
                slowest_ms, slowest_i = r["slowest_ms"], r["slowest_i"]
            break
        at = died_on(proc.stderr)   # the input it died on, as this process named it
        kind = "hang" if proc.returncode == 124 else "sanitizer"
        record = {"target": target, "unit": unit, "input": at, "kind": kind, "returncode": proc.returncode,
                  "report": proc.stderr[-6000:]}
        write_atomically(out / "findings" / f"{target}-u{unit}-i{at}.json", json.dumps(record, indent=1))
        findings += 1
        if at is None:   # died before an input was in hand: nothing to skip past
            break
        inputs += at - first + 1
        first = at + 1
    summary = {"target": target, "unit": unit, "inputs": inputs, "refused": refused, "slowest_ms": slowest_ms,
               "slowest_i": slowest_i, "findings": findings, "real": unit < real_units}
    write_atomically(out / "units" / f"{target}-{unit}.json", json.dumps(summary))
    return summary


def plan(out, targets):
    """Every target's units in turn, unfinished ones only: the real corpus first, then mutants."""
    next_unit = {t: 0 for t in targets}
    while True:
        for t in targets:
            u = next_unit[t]
            next_unit[t] += 1
            if not (out / "units" / f"{t}-{u}.json").exists():
                yield t, u


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--python-roots", nargs="*", default=[])
    ap.add_argument("--cpp-roots", nargs="*", default=[])
    ap.add_argument("--hours", type=float, default=8.0)
    ap.add_argument("--workers", type=int, default=10)
    a = ap.parse_args()
    if not a.python_roots and not a.cpp_roots:
        ap.error("give --python-roots, --cpp-roots or both: with neither there is nothing to hunt over")   # plan() would spin for ever
    out = path(os.path.abspath(a.out))
    for d in ("units", "findings", "crashes"):
        (out / d).mkdir(parents=True, exist_ok=True)
    binary = build(out)
    negative_control(out)
    targets = {}
    for target, roots in (("python", a.python_roots), ("cpp", a.cpp_roots)):
        if roots:
            listing, n = seed_list(out, target, roots)
            targets[target] = (listing, n, (n + 199) // 200)
    deadline = time.time() + a.hours * 3600
    print(f"hunt: {', '.join(f'{t} {n} seed files' for t, (_, n, _) in targets.items())}; "
          f"{a.workers} workers until {time.strftime('%Y-%m-%dT%H:%M:%S', time.localtime(deadline))}", flush=True)
    work = plan(out, targets)
    done = 0
    with futures.ThreadPoolExecutor(a.workers) as pool:
        running = set()
        while True:
            while len(running) < a.workers and time.time() < deadline:
                t, u = next(work)
                listing, _, real_units = targets[t]
                running.add(pool.submit(run_unit, binary, out, t, listing, u, real_units))
            if not running:
                break
            finished, running = futures.wait(running, return_when=futures.FIRST_COMPLETED)
            for f in finished:
                s = f.result()
                done += 1
                if s["findings"] or done % 50 == 0:
                    print(f"{time.strftime('%H:%M:%S')} {s['target']} unit {s['unit']}: {s['inputs']} inputs, "
                          f"{s['findings']} finding(s); {done} units this run", flush=True)
    units = [json.loads(p.read_bytes()) for p in (out / "units").glob("*.json")]
    findings = (out / "findings").glob("*.json")
    print(f"hunt done: {len(units)} units, {sum(u['inputs'] for u in units)} inputs, {len(findings)} findings")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
