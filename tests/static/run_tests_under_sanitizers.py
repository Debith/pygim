#!/usr/bin/env python
"""Build pygim's extensions under a sanitizer and run Python tests against them.

pytest runs the release build, where a read of freed memory usually finds what was there a moment
ago and passes. A placed class stream counted its rows from a units array an add() had moved, and
stats() walked units add_files() was growing, both unseen until 2026-10-29. This copies the working
tree into OUT (tracked and untracked files, so uncommitted work is what runs), builds it there with
PYGIM_SANITIZE (setup.py), and runs pytest on the given tests with the sanitizer's runtime preloaded
into the interpreter, which is not instrumented itself. OUT keeps its built modules: the next run
copies only what changed and rebuilds only the extensions that include it. Put OUT on a disk: a
session's /tmp can be memory.

Run:   python tests/static/run_tests_under_sanitizers.py OUT [--sanitizer address,undefined|thread] [TESTS...]
       with the dnd environment active, as any build needs (conda run -n dnd --no-capture-output ...)
Ends:  pytest's result and exit code; a sanitizer report ends the run with that report on stderr.
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys

from pygim.pathlike import ext, path

HERE = path(os.path.dirname(os.path.abspath(__file__)))
ROOT = HERE.parent.parent
RUNTIME = {"address": "libasan.so", "thread": "libtsan.so", "undefined": "libubsan.so"}
OPTIONS = {
    "ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1",   # the interpreter's own allocations are not ours to judge
    "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
    "TSAN_OPTIONS": "halt_on_error=1:die_after_fork=0",   # a forked child starts its own threads: tests do
}
# Arrow allocates through malloc, which the sanitizers intercept: its own mimalloc is not
# instrumented, so a buffer one thread freed and another's allocation reused raced for TSan
# (2026-10-29), and a use of freed memory would land in a live mimalloc page, unseen by ASan.
ENVIRONMENT = {"ARROW_DEFAULT_MEMORY_POOL": "system"}
# What ThreadSanitizer cannot see inside an uninstrumented library (each found 2026-10-29):
# Arrow's futures signal and free a condition variable under atomics TSan does not see, and Arrow
# fills a data type's fingerprint the first time the type is compared, publishing it through one
# more — in threads whose every frame is Arrow's, on types its own calls made. Narrow on purpose:
# a race of ours reached through an Arrow call still shows. The types a stream's workers share are
# tables::pair_columns', whose fingerprints it fills before the workers start. Arrow's datasets —
# pyarrow.parquet reading a test's file back — are suppressed whole: pygim never calls them.
# Modules whose own native runtime does not start with a sanitizer preloaded: polars dies on import
# (2026-10-29). A sitecustomize on the run's path makes importing them an ImportError, so a test's
# pytest.importorskip skips instead of the run dying with it.
UNLOADABLE = ("polars",)
SITECUSTOMIZE = "import sys\n" + "".join(f"sys.modules[{name!r}] = None   # cannot load here: see run_tests_under_sanitizers.py\n" for name in UNLOADABLE)
TSAN_SUPPRESSIONS = "\n".join([
    "race:arrow::ConcreteFutureImpl",
    "race:arrow::TypeEquals",
    "race:arrow::DataType::ComputeFingerprint",
    "race:arrow::DictionaryType::ComputeFingerprint",
    "race:libarrow_dataset.so",
]) + "\n"


def tree_files():
    """The working tree's files as git sees them: tracked, and untracked but not ignored."""
    out = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=ROOT, capture_output=True, check=True)
    return [f for f in out.stdout.decode().split("\0") if f]


MIRRORED = ".mirrored"   # in OUT: the tree files the last mirror put there, one name after each NUL


def mirror(out):
    """Makes `out` hold the tree's files as they are now: copies each whose bytes differ there, and
    removes each the last mirror put there that the tree no longer has (renamed or deleted), so what
    the build made stays. Returns how many it copied."""
    names = [name for name in tree_files() if (ROOT / name).is_file()]   # not one: deleted, not yet in git
    record = out / MIRRORED
    before = record.read_text().split("\0") if record.exists() else []
    for gone in set(before) - set(names) - {""}:
        (out / gone).unlink(missing_ok=True)
    copied = 0
    for name in names:
        source = ROOT / name
        data = source.read_bytes()
        target = out / name
        if target.exists() and target.read_bytes() == data:
            continue   # unchanged: its build stays valid
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        copied += 1
    record.write_text("\0".join(names))
    return copied


def drop_stale_modules(out):
    """Removes each compiled module in `out`'s src/pygim that no ext.*.toml declares any more — an
    extension renamed, whose old module would be imported before the Python module of its old name
    (an extension module is found first); returns the names it removed."""
    declared = {toml.read()["extension"]["module"] for toml in (out / "src" / "_pygim_fast").pathset("**/ext.*.toml", kind="file")}
    built = (out / "src" / "pygim").pathset("*", kind="file").filter(ext(".so", ".pyd"))
    stale = sorted((module for module in built if module.name.split(".")[0] not in declared), key=lambda m: m.name)
    for module in stale:
        module.unlink()
    return [module.name for module in stale]


# What one instrumented compile of a pybind11 and Arrow source may take at its peak. Sixteen at once
# on a 15 GB WSL (where /tmp is memory too) ran it out of memory and took the machine down
# (2026-10-29): the build runs only as many compiles as the free memory holds.
COMPILE_PEAK_BYTES = 3 << 30


def build_jobs():
    """Compiles to run at once: the cores, but no more than the available memory holds."""
    cores = os.cpu_count() or 1
    try:
        with open("/proc/meminfo") as info:
            available = next(int(line.split()[1]) << 10 for line in info if line.startswith("MemAvailable:"))
    except (OSError, StopIteration):
        return 1   # cannot tell: one at a time
    return max(1, min(cores, available // COMPILE_PEAK_BYTES))


def runtime(sanitizer):
    """The preload path of the sanitizer's runtime, from the compiler that builds."""
    cxx = os.environ.get("CXX", "c++")
    first = sanitizer.split(",")[0]
    return subprocess.run([cxx, f"-print-file-name={RUNTIME[first]}"], capture_output=True, text=True, check=True).stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out", help="where the instrumented copy lives (kept between runs)")
    parser.add_argument("--sanitizer", default="address,undefined", choices=["address,undefined", "address", "thread", "undefined"])
    parser.add_argument("tests", nargs="*", default=["tests/unittests"], help="pytest arguments, relative to the tree")
    args = parser.parse_args()
    out = path(os.path.abspath(args.out))
    out.mkdir(parents=True, exist_ok=True)
    print(f"{mirror(out)} file(s) copied into {out}")
    for name in drop_stale_modules(out):
        print(f"removed {name}: no extension declares it any more")
    env = {**os.environ, "PYGIM_SANITIZE": args.sanitizer}
    env.setdefault("SETUPTOOLS_SCM_PRETEND_VERSION", "0.0.0+sanitized")   # the copy has no git history to version from
    jobs = build_jobs()
    print(f"building with {jobs} compile(s) at once")
    built = subprocess.run([sys.executable, "setup.py", "build_ext", "--inplace", "-j", str(jobs)], cwd=out, env=env,
                           capture_output=True, text=True)
    if built.returncode != 0:
        print(built.stdout[-3000:] + built.stderr[-3000:])
        print("the instrumented build failed")
        return 1
    for objects in (out / "build").pathset("temp*", kind="dir"):   # a gigabyte of objects; build/lib's modules say what is up to date
        shutil.rmtree(objects, ignore_errors=True)
    reports = out / "sanitizer-report"   # a report goes to a file: pytest holds stderr, and a halt loses it
    for stale in out.pathset("sanitizer-report.*", kind="file"):
        stale.unlink()
    options = {name: f"{value}:log_path={reports}" for name, value in OPTIONS.items()}
    suppressions = out / "tsan-suppressions.txt"
    suppressions.write_bytes(TSAN_SUPPRESSIONS.encode())
    options["TSAN_OPTIONS"] += f":suppressions={suppressions}"
    site = out / "sanitizer-site"
    site.mkdir(parents=True, exist_ok=True)
    (site / "sitecustomize.py").write_bytes(SITECUSTOMIZE.encode())
    # the runtime preloaded into the interpreter alone, not into setarch; ThreadSanitizer's fixed
    # memory layout needs the address space unrandomised ("unexpected memory mapping" otherwise)
    command = ["env", f"LD_PRELOAD={runtime(args.sanitizer)}", sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider", *args.tests]
    if shutil.which("setarch"):
        command = ["setarch", platform.machine(), "-R", *command]
    python_path = os.pathsep.join([str(site), str(out / "src")])
    # PYGIM_SANITIZED tells a test which runtime is loaded: ThreadSanitizer holds a signal until the
    # thread's next intercepted call, so a test timing how soon one stops a run measures that instead.
    # PYGIM_TEST_TIME_SCALE=0: no time budget, which an instrumented test's clock would measure the instrumentation against.
    tested = {**os.environ, **options, **ENVIRONMENT, "PYTHONPATH": python_path, "PYGIM_SANITIZED": args.sanitizer,
              "PYGIM_TEST_TIME_SCALE": "0"}
    code = subprocess.run(command, cwd=out, env=tested).returncode
    for report in out.pathset("sanitizer-report.*", kind="file"):   # sorted already
        print(f"--- {report}\n{report.read_bytes().decode(errors='replace')[:20000]}")
    return code


if __name__ == "__main__":
    sys.exit(main())
