# -*- coding: utf-8 -*-
"""PathSet.read_each: the members' contents as views, in member order, while threads read ahead with
the GIL released. A view owns its file's bytes in C++; a memoryview of it copies nothing."""

import gc
import os
import sys
import threading

import pytest

from pygim import PathSet, path

WIDTHS = [None, 1, 2, 8]


@pytest.fixture()
def members(temp_dir):
    """200 files of different lengths, with a folder and a missing path among them."""
    found, expected = [], []
    for i in range(200):
        f = temp_dir / f"f{i:03d}.txt"
        f.write_bytes(b"x" * (i * 37 % 5000) + f"#{i}".encode())
        found.append(f)
        expected.append(f.read_bytes())
        if i == 10:
            found.append(temp_dir)
        if i == 50:
            found.append(temp_dir / "missing.txt")
    return found, expected


@pytest.mark.parametrize("threads", WIDTHS)
def test_views_come_in_member_order_at_every_width(members, threads):
    found, expected = members
    files = [f for f in found if f.is_file()]
    views = list(PathSet(found).read_each(threads=threads))
    assert [bytes(v) for v in views] == expected
    assert [str(v.path) for v in views] == [str(f) for f in files]


def test_a_view_is_its_bytes_without_a_copy(temp_dir):
    (temp_dir / "a.txt").write_bytes(b"hello")
    view = next(PathSet([temp_dir / "a.txt"]).read_each())
    seen = memoryview(view)
    assert seen.obj is view and seen.readonly and seen.tobytes() == b"hello" and len(view) == 5


def test_a_view_kept_past_its_turn_keeps_its_bytes(members):
    found, expected = members
    views = list(PathSet(found).read_each(threads=8))
    gc.collect()
    assert [memoryview(v).tobytes() for v in views] == expected


def test_text_decodes_utf8_and_names_a_file_that_is_not(temp_dir):
    (temp_dir / "good.txt").write_text("päivää", encoding="utf-8")
    (temp_dir / "bad.txt").write_bytes(b"\xff\xfe")
    good, bad = PathSet([temp_dir / "good.txt", temp_dir / "bad.txt"]).read_each()
    assert good.text() == "päivää"
    with pytest.raises(ValueError, match=r"bad\.txt.*not valid UTF-8"):
        bad.text()


def test_an_empty_set_reads_nothing():
    assert list(PathSet([]).read_each()) == []


@pytest.mark.parametrize("threads", WIDTHS)
def test_a_small_window_still_hands_out_every_file_in_order(members, threads):
    found, expected = members
    assert [bytes(v) for v in PathSet(found).read_each(threads=threads, ahead_bytes=1_000)] == expected


@pytest.mark.parametrize("threads", WIDTHS)
def test_no_bytes_ahead_reads_each_file_only_when_it_is_asked_for(members, threads):
    found, expected = members
    assert [bytes(v) for v in PathSet(found).read_each(threads=threads, ahead_bytes=0)] == expected


def test_more_threads_than_members_reads_each_once(temp_dir):
    for name in ("a.txt", "b.txt", "c.txt"):
        (temp_dir / name).write_text(name)
    assert [bytes(v) for v in PathSet([temp_dir / n for n in ("a.txt", "b.txt", "c.txt")]).read_each(threads=64)] == \
        [b"a.txt", b"b.txt", b"c.txt"]


@pytest.mark.skipif(not os.path.exists("/dev/null") or sys.platform == "win32", reason="a POSIX device and a symbolic link")
def test_a_device_and_a_link_loop_are_left_out(temp_dir):
    (temp_dir / "loop").symlink_to(temp_dir / "loop")
    (temp_dir / "a.txt").write_text("a")
    assert [bytes(v) for v in PathSet(["/dev/zero", temp_dir / "loop", temp_dir / "a.txt"]).read_each()] == [b"a"]


def test_threads_must_be_at_least_one(members):
    with pytest.raises(ValueError, match="threads"):
        PathSet(members[0]).read_each(threads=0)


@pytest.mark.skipif(sys.platform == "win32" or os.geteuid() == 0, reason="permission bits bind a non-root POSIX user")
def test_a_refused_file_raises_in_its_place_and_ends_the_reading(temp_dir):
    for name in ("a.txt", "secret.txt", "b.txt"):
        (temp_dir / name).write_text(name)
    (temp_dir / "secret.txt").chmod(0)
    try:
        reading = PathSet([temp_dir / n for n in ("a.txt", "secret.txt", "b.txt")]).read_each()
        assert bytes(next(reading)) == b"a.txt"
        with pytest.raises(PermissionError):
            next(reading)
        with pytest.raises(StopIteration):
            next(reading)
    finally:
        (temp_dir / "secret.txt").chmod(0o600)


def threads_running():
    """The process's threads, native ones included: one entry each in /proc/self/task."""
    return len(path("/proc/self/task").pathset())


@pytest.mark.skipif(not os.path.isdir("/proc/self/task"), reason="counting threads needs /proc")
def test_stopping_early_leaves_no_thread_behind(temp_dir):
    for i in range(500):
        (temp_dir / f"f{i}.txt").write_bytes(b"y" * 4096)
    before = threads_running()
    reading = path(temp_dir).pathset("*.txt").read_each(threads=8, ahead_bytes=4096)   # the readers wait for room, so they are there to count
    next(reading), next(reading)
    assert threads_running() > before        # the readers are running ...
    del reading
    gc.collect()
    assert threads_running() == before       # ... and gone with the reading


def test_two_threads_on_one_iterator_share_it_or_are_told_it_is_busy(temp_dir):
    """A read_each iterator has one reader at a time, as a generator has: a thread that asks for the
    next file while another thread's __next__ is still running — the GIL let go to wait for the
    readers — gets ValueError, and the files are each handed out once. Found 2026-10-29: both took
    the same turn, and one waited for ever for a result the other had taken."""
    for i in range(400):
        (temp_dir / f"f{i:03d}.txt").write_bytes(b"z" * 20000)
    reading = path(temp_dir).pathset("*.txt").read_each(threads=2, ahead_bytes=1)
    taken, told = [], []

    def drain():
        try:
            for content in reading:
                taken.append(str(content.path))
        except ValueError as busy:
            told.append(str(busy))

    readers = [threading.Thread(target=drain, daemon=True) for _ in range(4)]
    for r in readers:
        r.start()
    for r in readers:
        r.join(timeout=60)
    assert not any(r.is_alive() for r in readers)
    assert len(taken) == len(set(taken)) and set(told) <= {"read_each iterator already executing"}


def test_a_view_names_its_member_as_a_path(temp_dir):
    (temp_dir / "conf.json").write_text("{}")
    view = next(PathSet([temp_dir / "conf.json"]).read_each())
    assert view.path == path(temp_dir / "conf.json") and view.path.read() == {}
    assert "conf.json" in repr(view) and "2 bytes" in repr(view)


# The race of a reader against a writer to its table is test_pathset_read_all_files.py's, for both reads.


@pytest.mark.skipif(not os.path.isfile("/proc/version"), reason="a /proc file: fstat says its size is 0")
def test_a_file_whose_size_the_os_does_not_say_is_read_whole():
    """fstat gives a /proc file size 0: it is read until read() says the end, from a page."""
    with open("/proc/version", "rb") as f:
        expected = f.read()
    (content,) = PathSet([path("/proc/version")]).read_each()
    assert bytes(content) == expected
