# -*- coding: utf-8 -*-
"""PathSet.read_all_files: one job in the C++ core (pathlike/batch_read.h) — every member read once,
in member order, with the GIL released, on as many threads as it is told or as the reads reward."""

import os
import re
import subprocess
import sys
import threading
import time

import pytest

from pygim import PathSet, path

WIDTHS = [None, 1, 2, 8]


@pytest.fixture()
def many(temp_dir):
    """100 text files, with a folder and a missing path among them: the members and the texts expected."""
    members, texts = [], []
    for i in range(100):
        f = temp_dir / f"f{i:03d}.txt"
        f.write_text(f"file {i}")
        members.append(f)
        texts.append(f"file {i}")
        if i == 10:
            members.append(temp_dir)                     # a folder: left out
        if i == 50:
            members.append(temp_dir / "missing.txt")     # nothing there: left out
    return members, texts


@pytest.mark.parametrize("threads", WIDTHS)
def test_texts_come_in_member_order_at_every_width(many, threads):
    members, texts = many
    assert PathSet(members).read_all_files(threads=threads) == texts


def test_an_empty_set_reads_nothing():
    assert PathSet([]).read_all_files() == [] and PathSet([]).read_all_files(threads=4) == []


def test_threads_must_be_at_least_one(many):
    with pytest.raises(ValueError, match="threads"):
        PathSet(many[0]).read_all_files(threads=0)


@pytest.mark.parametrize("threads", WIDTHS)
def test_bytes_that_are_not_utf8_name_the_first_such_member(temp_dir, threads):
    members = []
    for i in range(64):
        f = temp_dir / f"f{i:02d}.txt"
        f.write_bytes(b"\xff\xfe" if i in (10, 50) else b"ok")
        members.append(f)
    for _ in range(5):   # whichever thread reads what, the error is the first bad member's
        with pytest.raises(ValueError, match=r"f10\.txt is not valid UTF-8"):
            PathSet(members).read_all_files(threads=threads)


@pytest.mark.skipif(not os.path.isdir("/proc/self/task"), reason="counting threads needs /proc")
def test_more_threads_than_members_starts_one_a_member(temp_dir):
    """threads= is a most, not a count: two members are read by two threads at most. Found
    2026-10-29: read_all_files(threads=1000) of two files started a thousand.

    Counted only while a call runs, with a pause between calls: a joined thread can stay listed in
    /proc for a moment after its join returns (the kernel wakes the joiner before it removes the
    entry), and a counter fast enough caught the last call's reader beside this call's two
    (2026-11-01, a release wheel's run: 26 against 25)."""
    members = [temp_dir / "a.txt", temp_dir / "b.txt"]
    for m in members:
        m.write_text("x")
    seen, stop, reading = [], threading.Event(), threading.Event()
    threads_now = lambda: len(path("/proc/self/task").pathset())   # native threads too

    def count():
        while not stop.is_set():
            n = threads_now()
            if reading.is_set():
                seen.append(n)

    before = threads_now()
    counter = threading.Thread(target=count)
    counter.start()
    for _ in range(20):
        reading.set()
        assert PathSet(members).read_all_files(threads=1000) == ["x", "x"]
        reading.clear()
        time.sleep(0.005)   # the last call's readers gone from /proc before the next call starts
    stop.set()
    counter.join()
    assert seen, "no count landed while a call ran"
    assert max(seen) <= before + 1 + 2   # the counter, and a reader a member


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX names are bytes")
def test_a_member_named_past_utf8_is_named_in_the_value_error(temp_dir, write_named_past_utf8):
    """A file whose name is no UTF-8 (POSIX names are bytes) and whose bytes are no UTF-8 either is
    the ValueError naming it, not a UnicodeDecodeError raised while building that message."""
    bad = write_named_past_utf8(os.path.join(os.fsencode(temp_dir), b"\xff.txt"), b"\xff\xfe")
    with pytest.raises(ValueError, match="is not valid UTF-8") as raised:
        PathSet([os.fsdecode(bad)]).read_all_files()
    assert type(raised.value) is ValueError


def test_a_file_larger_than_one_read_comes_whole(temp_dir):
    big = temp_dir / "big.txt"
    big.write_text("0123456789" * 300_000)
    assert PathSet([big]).read_all_files() == ["0123456789" * 300_000]


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="/proc is Linux's")
def test_a_file_that_reports_no_size_is_read_to_its_end():
    text = PathSet(["/proc/self/status"]).read_all_files()[0]   # st_size is 0; the text is not
    assert text.startswith("Name:") and "Pid:" in text


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="/proc is Linux's")
def test_a_file_that_reports_no_size_is_read_past_its_first_page():
    text = PathSet(["/proc/self/maps"]).read_all_files()[0]   # a Python process maps more than a page's worth
    assert len(text) > 4096 and text.endswith("\n")
    assert all(re.match(r"[0-9a-f]+-[0-9a-f]+ [-r][-w][-x][ps] ", line) for line in text.splitlines()), text   # no line cut or repeated where the buffer grew


@pytest.mark.skipif(not hasattr(os, "mkfifo"), reason="no FIFOs")
def test_a_fifo_is_left_out_without_waiting_for_a_writer(temp_dir):
    fifo = temp_dir / "pipe"
    os.mkfifo(fifo)
    (temp_dir / "a.txt").write_text("a")
    assert PathSet([fifo, temp_dir / "a.txt"]).read_all_files() == ["a"]


@pytest.mark.skipif(sys.platform == "win32", reason="symbolic links need privileges on Windows")
def test_a_link_is_read_through_and_a_broken_one_left_out(temp_dir):
    (temp_dir / "target.txt").write_text("through")
    (temp_dir / "link.txt").symlink_to(temp_dir / "target.txt")
    (temp_dir / "broken.txt").symlink_to(temp_dir / "gone.txt")
    assert PathSet([temp_dir / "link.txt", temp_dir / "broken.txt"]).read_all_files() == ["through"]


@pytest.mark.skipif(sys.platform == "win32", reason="symbolic links need privileges on Windows")
def test_a_link_loop_is_left_out_as_pathlib_says_it_is_no_file(temp_dir):
    (temp_dir / "loop").symlink_to(temp_dir / "loop")       # open() and stat() say ELOOP
    (temp_dir / "a.txt").write_text("a")
    assert not (temp_dir / "loop").is_file()                  # pathlib's answer: no file, not an error
    assert PathSet([temp_dir / "loop", temp_dir / "a.txt"]).read_all_files() == ["a"]


@pytest.mark.skipif(not os.path.exists("/dev/null"), reason="a POSIX device")
def test_a_device_is_left_out_without_reading_it():
    assert PathSet(["/dev/null", "/dev/zero"]).read_all_files() == []   # /dev/zero would never end


@pytest.mark.skipif(sys.platform == "win32" or os.geteuid() == 0, reason="permission bits bind a non-root POSIX user")
def test_a_file_the_os_refuses_raises_as_pathlib_does(temp_dir):
    secret = temp_dir / "secret.txt"
    secret.write_text("x")
    secret.chmod(0)
    try:
        with pytest.raises(PermissionError) as raised:
            PathSet([secret]).read_all_files()
        with pytest.raises(PermissionError):
            secret.read_text()                       # pathlib's own answer for the same file
        assert raised.value.filename == str(secret)
    finally:
        secret.chmod(0o600)


@pytest.mark.skipif(sys.platform == "win32" or os.geteuid() == 0, reason="permission bits bind a non-root POSIX user")
def test_a_folder_the_os_refuses_is_still_left_out(temp_dir):
    locked = temp_dir / "locked"
    locked.mkdir()
    locked.chmod(0)
    try:
        assert PathSet([locked]).read_all_files() == []
    finally:
        locked.chmod(0o700)


def test_other_threads_run_while_it_reads(temp_dir):
    """The reads run with the GIL released (#126): a thread that counts keeps counting meanwhile.
    The switch interval is long, so the GIL changes hands only when it is released, never by force;
    the counter naps between counts, so it gives the GIL back at once when the reads are done (it
    spun until forced, and the test waited 0.25 s for the GIL twice)."""
    members = []
    for i in range(1000):
        f = temp_dir / f"f{i:04d}.txt"
        f.write_text("x" * 100)
        members.append(f)
    ps = PathSet(members)
    counted, started, stop = [0], threading.Event(), threading.Event()

    def count():
        started.set()
        while not stop.is_set():
            counted[0] += 1
            time.sleep(0.0001)

    interval = sys.getswitchinterval()
    sys.setswitchinterval(0.25)
    ticker = threading.Thread(target=count)
    try:
        ticker.start()
        started.wait()
        before = counted[0]
        ps.read_all_files(threads=1)
        during = counted[0] - before
    finally:
        stop.set()
        ticker.join()
        sys.setswitchinterval(interval)
    assert during > 0


RACE = r"""
import pathlib, sys, threading
from pygim.pathlike import PathSet, PathStore, path

sys.setswitchinterval(0.0001)   # the writer gives the GIL back to a finished read at once, and interleaves more
root = pathlib.Path(sys.argv[1])
for i in range(200):
    (root / f"f{i}.txt").write_text(str(i))
store = PathStore()
ps = PathSet([root / f"f{i}.txt" for i in range(200)], store=store)
stop = threading.Event()

def grow():   # writes to the table the set's members are rows of
    i = 0
    while not stop.is_set():
        path(str(root / "new" / f"n{i}" / "deeper"), store=store)
        i += 1

writer = threading.Thread(target=grow)
writer.start()
expected = [str(i) for i in range(200)]
reads = {"read_all_files": lambda: ps.read_all_files(threads=4), "read_each": lambda: [v.text() for v in ps.read_each(threads=4)]}
try:
    for _ in range(50):   # each read 50 times against the growing table
        for name, read in reads.items():
            assert read() == expected, name
finally:
    stop.set()
    writer.join()
"""


def test_reading_while_another_thread_grows_its_table(temp_dir):
    """#126: the released reads — read_all_files and read_each, in one child — touch only values copied
    while the GIL was held, so a writer to the same table in another thread cannot race them. A race
    would kill the process, hence the subprocess."""
    done = subprocess.run([sys.executable, "-c", RACE, str(temp_dir)], capture_output=True, text=True, timeout=120)
    assert done.returncode == 0, done.stderr
