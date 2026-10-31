# -*- coding: utf-8 -*-
"""Ctrl-C and SIGTERM during a native run (utils/interrupt.h, #272): the run owns them while it lasts,
its loops stop at an item's edge, the previous handlers come back, and the signal reaches them then.
Each case runs in a child process, since it sends that process a real signal."""

import os
import signal
import subprocess
import sys

import pytest

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="sends POSIX signals to a child process")

# The child: 3,000 files read on one thread, interrupted by a helper thread. The switch interval is
# long, so the helper gets the GIL only once read_all_files has let it go — the signal always lands
# while the run is in progress.
CHILD = r"""
import os, pathlib, signal, sys, threading, time
from pygim import PathSet

root, mode = pathlib.Path(sys.argv[1]), sys.argv[2]
files = []
for i in range(3_000):
    f = root / f"f{i:04d}.txt"
    f.write_text("x" * 200)
    files.append(f)
ps = PathSet(files)
tasks = lambda: len(os.listdir("/proc/self/task")) if os.path.isdir("/proc/self/task") else 0
if mode == "ignored":
    signal.signal(signal.SIGINT, signal.SIG_IGN)
heard = []
if mode == "own handler":
    signal.signal(signal.SIGINT, lambda sig, frame: heard.append(sig))
handler_before = signal.getsignal(signal.SIGINT)
sent = signal.SIGTERM if mode == "terminate" else signal.SIGINT
t = time.perf_counter()
ps.read_all_files(threads=1)
full = time.perf_counter() - t                      # the whole run, uninterrupted
before = tasks()
sys.setswitchinterval(1.0)
go = threading.Event()
helper = threading.Thread(target=lambda: (go.wait(), os.kill(os.getpid(), sent)))
helper.start()
go.set()
t = time.perf_counter()
try:
    texts = ps.read_all_files(threads=1)
    print("completed", len(texts))
except KeyboardInterrupt:
    print("KeyboardInterrupt")
except InterruptedError as e:
    print("InterruptedError", heard)
took = time.perf_counter() - t
helper.join()
print("stopped early", took < full / 2, f"({took * 1000:.1f} of {full * 1000:.1f} ms)")
print("handler given back", signal.getsignal(signal.SIGINT) is handler_before)
print("threads left", tasks() - before)
"""


def run_child(temp_dir, mode):
    return subprocess.run([sys.executable, "-c", CHILD, str(temp_dir), mode], capture_output=True, text=True, timeout=120)


@pytest.fixture(scope="module")
def interrupted(tmp_path_factory):
    """One Ctrl-C'd child for every test that reads what it said: each child writes 3,000 files."""
    return run_child(tmp_path_factory.mktemp("interrupt"), "interrupt")


def test_ctrl_c_stops_the_run_and_raises_keyboard_interrupt(interrupted):
    assert interrupted.stdout.splitlines()[0] == "KeyboardInterrupt", interrupted.stdout + interrupted.stderr
    assert "stopped early True" in interrupted.stdout, interrupted.stdout       # not raised after a finished run
    assert "handler given back True" in interrupted.stdout


@pytest.mark.skipif(not os.path.isdir("/proc/self/task"), reason="counting threads needs /proc")
def test_an_interrupted_run_leaves_no_thread_behind(interrupted):
    assert "threads left 0" in interrupted.stdout, interrupted.stdout + interrupted.stderr


def test_sigterm_ends_the_process_after_the_clean_stop(temp_dir):
    done = run_child(temp_dir, "terminate")
    assert done.returncode == -signal.SIGTERM, (done.returncode, done.stdout, done.stderr)
    assert "completed" not in done.stdout


def test_a_signal_the_program_ignores_does_not_stop_the_run(temp_dir):
    done = run_child(temp_dir, "ignored")
    assert done.stdout.splitlines()[0] == "completed 3000", done.stdout + done.stderr


def test_a_handler_that_does_not_raise_hears_it_and_the_call_raises_interrupted_error(temp_dir):
    done = run_child(temp_dir, "own handler")
    assert done.stdout.splitlines()[0] == f"InterruptedError [{int(signal.SIGINT)}]", done.stdout + done.stderr
    assert "stopped early True" in done.stdout, done.stdout
    assert "handler given back True" in done.stdout


@pytest.mark.skipif(not os.path.isdir("/proc/self/task"), reason="reads each thread's signal mask from /proc")
def test_the_threads_a_run_starts_block_the_signals_a_program_is_sent(temp_dir):
    """A process's signal goes to a thread that does not block it. The pools' threads block every
    asynchronous one (utils/parallel.h, signals_blocked), so Ctrl-C reaches the thread that can act
    on it and cut short its blocking call, never a parked worker; a fault (SIGSEGV) stays theirs."""
    child = r"""
import os, signal, sys
from pygim import code
for i in range(64):
    open(os.path.join(sys.argv[1], f"m{i}.py"), "w").write("def f(a):\n    return a\n" * 50)
main = os.getpid()
before = {int(t) for t in os.listdir("/proc/self/task")} - {main}   # threads other libraries started
code.parse(sys.argv[1], threads=4)
for tid in sorted({int(t) for t in os.listdir("/proc/self/task")} - before):
    status = open(f"/proc/self/task/{tid}/status").read()
    blocked = int(next(l for l in status.splitlines() if l.startswith("SigBlk:")).split()[1], 16)
    bit = lambda s: (blocked >> (s - 1)) & 1
    print("main" if tid == main else "worker", bit(signal.SIGINT), bit(signal.SIGTERM), bit(signal.SIGSEGV))
"""
    done = subprocess.run([sys.executable, "-c", child, str(temp_dir)], capture_output=True, text=True, timeout=60)
    rows = done.stdout.split("\n")[:-1]
    assert rows[0] == "main 0 0 0", done.stdout + done.stderr
    workers = rows[1:]
    assert workers and set(workers) == {"worker 1 1 0"}, done.stdout
