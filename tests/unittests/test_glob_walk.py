# -*- coding: utf-8 -*-
"""glob and rglob find what pathlib's glob finds (D49, 2026-10-30: Debith, "yes"), walking in parallel
when the pattern may list many directories (pathlike/glob_walk.h, parallel_glob.h, D28), and Ctrl-C
stops the walk cleanly."""

import fnmatch
import os
import pathlib
import subprocess
import sys

import pytest

import pygim
from pool_threads import POOL_THREADS

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="symbolic links need privileges on Windows")


def make_tree(root):
    for name in ["a.py", ".hidden.py", "b.txt", "pkg/__init__.py", "pkg/m.py", "pkg/sub/n.py", "pkg/sub/deep/o.py",
                 "pkg/sub/deep/p.txt", ".dotdir/q.py", "other/r.py", "other/x/s.py", "[ab]", "A.PY"]:
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(name)
    (root / "link_to_pkg").symlink_to(root / "pkg")                 # a link to a directory
    (root / "pkg" / "link_to_a.py").symlink_to(root / "a.py")       # a link to a file
    (root / "broken.py").symlink_to(root / "gone.py")               # a broken link
    (root / "pkg" / "sub" / "loop").symlink_to(root / "pkg")        # a link back up the tree
    for name in ("empty", "a", "b"):
        (root / name).mkdir()
    if (root / "A.PY").samefile(root / "a.py"):
        pytest.skip("this file system folds case (macOS): the expectations are pathlib's on one that does not")
    return root


def rel(found, root):
    """A path glob found, relative to `root` as written — "pkg/.." stays, where os.path.relpath folds it."""
    text, top = os.fspath(found), os.fspath(root)
    return "." if text == top else text[len(top) + 1:]


# What pathlib's glob finds on make_tree's tree: Python 3.14.4, recurse_symlinks=False, case-sensitive as on
# POSIX, pinned 2026-10-30 (tests hold it on every Python; on 3.14 and later the live pathlib too). `**`
# yields the directory it starts in, never goes through a link to a directory (though it lists the link),
# and a final `**/` lists real directories only; any other segment follows a link; a trailing `/` keeps
# directories; `.` segments are dropped and `..` is a step; `[seq]` is a class, `[[]` a literal `[`.
PATHLIB = {'*.py': ['.hidden.py', 'a.py', 'broken.py'],
 '**': ['.',
        '.dotdir',
        '.dotdir/q.py',
        '.hidden.py',
        'A.PY',
        '[ab]',
        'a',
        'a.py',
        'b',
        'b.txt',
        'broken.py',
        'empty',
        'link_to_pkg',
        'other',
        'other/r.py',
        'other/x',
        'other/x/s.py',
        'pkg',
        'pkg/__init__.py',
        'pkg/link_to_a.py',
        'pkg/m.py',
        'pkg/sub',
        'pkg/sub/deep',
        'pkg/sub/deep/o.py',
        'pkg/sub/deep/p.txt',
        'pkg/sub/loop',
        'pkg/sub/n.py'],
 '**/*.py': ['.dotdir/q.py',
             '.hidden.py',
             'a.py',
             'broken.py',
             'other/r.py',
             'other/x/s.py',
             'pkg/__init__.py',
             'pkg/link_to_a.py',
             'pkg/m.py',
             'pkg/sub/deep/o.py',
             'pkg/sub/n.py'],
 'pkg/**/*.py': ['pkg/__init__.py', 'pkg/link_to_a.py', 'pkg/m.py', 'pkg/sub/deep/o.py', 'pkg/sub/n.py'],
 '**/sub/*': ['pkg/sub/deep', 'pkg/sub/loop', 'pkg/sub/n.py'],
 '**/deep/**': ['pkg/sub/deep', 'pkg/sub/deep/o.py', 'pkg/sub/deep/p.txt'],
 '*/*.py': ['.dotdir/q.py',
            'link_to_pkg/__init__.py',
            'link_to_pkg/link_to_a.py',
            'link_to_pkg/m.py',
            'other/r.py',
            'pkg/__init__.py',
            'pkg/link_to_a.py',
            'pkg/m.py'],
 '**/**/o.py': ['pkg/sub/deep/o.py'],
 'pkg/**': ['pkg',
            'pkg/__init__.py',
            'pkg/link_to_a.py',
            'pkg/m.py',
            'pkg/sub',
            'pkg/sub/deep',
            'pkg/sub/deep/o.py',
            'pkg/sub/deep/p.txt',
            'pkg/sub/loop',
            'pkg/sub/n.py'],
 'link_to_pkg/*.py': ['link_to_pkg/__init__.py', 'link_to_pkg/link_to_a.py', 'link_to_pkg/m.py'],
 '**/link_to_pkg/*': ['link_to_pkg/__init__.py', 'link_to_pkg/link_to_a.py', 'link_to_pkg/m.py', 'link_to_pkg/sub'],
 'link_to_pkg/**': ['link_to_pkg',
                    'link_to_pkg/__init__.py',
                    'link_to_pkg/link_to_a.py',
                    'link_to_pkg/m.py',
                    'link_to_pkg/sub',
                    'link_to_pkg/sub/deep',
                    'link_to_pkg/sub/deep/o.py',
                    'link_to_pkg/sub/deep/p.txt',
                    'link_to_pkg/sub/loop',
                    'link_to_pkg/sub/n.py'],
 'nothing/**/x': [],
 '*/': ['.dotdir', 'a', 'b', 'empty', 'link_to_pkg', 'other', 'pkg'],
 '**/': ['.', '.dotdir', 'a', 'b', 'empty', 'other', 'other/x', 'pkg', 'pkg/sub', 'pkg/sub/deep'],
 '**/*/': ['.dotdir',
           'a',
           'b',
           'empty',
           'link_to_pkg',
           'other',
           'other/x',
           'pkg',
           'pkg/sub',
           'pkg/sub/deep',
           'pkg/sub/loop'],
 'pkg/**/': ['pkg', 'pkg/sub', 'pkg/sub/deep'],
 './a.py': ['a.py'],
 'pkg/./m.py': ['pkg/m.py'],
 'pkg/../a.py': ['pkg/../a.py'],
 'pkg/..': ['pkg/..'],
 '*/..': ['.dotdir/..', 'a/..', 'b/..', 'empty/..', 'link_to_pkg/..', 'other/..', 'pkg/..'],
 '**/..': ['..',
           '.dotdir/..',
           'a/..',
           'b/..',
           'empty/..',
           'other/..',
           'other/x/..',
           'pkg/..',
           'pkg/sub/..',
           'pkg/sub/deep/..'],
 '**/.': ['.',
          '.dotdir',
          '.dotdir/q.py',
          '.hidden.py',
          'A.PY',
          '[ab]',
          'a',
          'a.py',
          'b',
          'b.txt',
          'broken.py',
          'empty',
          'link_to_pkg',
          'other',
          'other/r.py',
          'other/x',
          'other/x/s.py',
          'pkg',
          'pkg/__init__.py',
          'pkg/link_to_a.py',
          'pkg/m.py',
          'pkg/sub',
          'pkg/sub/deep',
          'pkg/sub/deep/o.py',
          'pkg/sub/deep/p.txt',
          'pkg/sub/loop',
          'pkg/sub/n.py'],
 '[ab]': ['a', 'b'],
 '[!a]*.py': ['.hidden.py', 'broken.py'],
 '[a-c].txt': ['b.txt'],
 '[[]ab]': ['[ab]'],
 '**/[mn].py': ['pkg/m.py', 'pkg/sub/n.py'],
 '*.PY': ['A.PY'],
 '?.py': ['a.py'],
 'pkg/sub/loop/*': ['pkg/sub/loop/__init__.py', 'pkg/sub/loop/link_to_a.py', 'pkg/sub/loop/m.py', 'pkg/sub/loop/sub'],
 'pkg/sub/loop/**': ['pkg/sub/loop',
                     'pkg/sub/loop/__init__.py',
                     'pkg/sub/loop/link_to_a.py',
                     'pkg/sub/loop/m.py',
                     'pkg/sub/loop/sub',
                     'pkg/sub/loop/sub/deep',
                     'pkg/sub/loop/sub/deep/o.py',
                     'pkg/sub/loop/sub/deep/p.txt',
                     'pkg/sub/loop/sub/loop',
                     'pkg/sub/loop/sub/n.py']}
PATHLIB_ANY_CASE = {'*.py': ['.hidden.py', 'A.PY', 'a.py', 'broken.py'],
 'PKG/*.PY': ['pkg/__init__.py', 'pkg/link_to_a.py', 'pkg/m.py'],
 '**/SUB/*': ['pkg/sub/deep', 'pkg/sub/loop', 'pkg/sub/n.py'],
 '[A-B]': ['a', 'b']}
PATHLIB_REFUSES = {'': 'ValueError', '.': 'ValueError', './': 'ValueError', '/abs/*.py': 'NotImplementedError'}


def found(root, pattern, **options):
    return [rel(p, root) for p in pygim.path(root).glob(pattern, **options)]


@pytest.mark.parametrize("pattern", list(PATHLIB))
def test_glob_finds_what_pathlib_finds(temp_dir, pattern):
    root = make_tree(temp_dir / "t")
    assert found(root, pattern) == PATHLIB[pattern]
    if sys.version_info >= (3, 14):
        assert found(root, pattern) == sorted(rel(p, root) for p in pathlib.Path(root).glob(pattern))


@pytest.mark.parametrize("pattern", list(PATHLIB_ANY_CASE))
def test_case_sensitive_false_matches_any_case(temp_dir, pattern):
    """pathlib's case_sensitive: None is the platform's (case-insensitive on Windows), False any case."""
    root = make_tree(temp_dir / "t")
    assert found(root, pattern, case_sensitive=False) == PATHLIB_ANY_CASE[pattern]
    assert found(root, pattern, case_sensitive=None) == found(root, pattern, case_sensitive=True) == PATHLIB.get(pattern, [])


@pytest.mark.parametrize("pattern", list(PATHLIB_REFUSES))
def test_a_pattern_pathlib_refuses_is_refused_as_pathlib_refuses_it(temp_dir, pattern):
    with pytest.raises({"ValueError": ValueError, "NotImplementedError": NotImplementedError}[PATHLIB_REFUSES[pattern]]):
        pygim.path(temp_dir).glob(pattern)


@pytest.mark.skipif(sys.platform == "win32" or os.geteuid() == 0, reason="permission bits bind a non-root POSIX user")
@pytest.mark.parametrize("pattern", ["**/*.py", "*/*.py", "locked/*", "locked/in/z.py"])
def test_an_unreadable_directory_is_passed_over(temp_dir, pattern):
    root = make_tree(temp_dir / "t")
    (root / "locked" / "in").mkdir(parents=True)
    (root / "locked" / "in" / "z.py").write_text("z")
    (root / "locked").chmod(0)
    try:
        listed = found(root, pattern)
    finally:
        (root / "locked").chmod(0o700)
    assert not [f for f in listed if f.startswith("locked")]
    assert listed == PATHLIB.get(pattern, [])


def test_rglob_is_glob_with_a_leading_double_star(temp_dir):
    root = make_tree(temp_dir / "t")
    assert pygim.path(root).rglob("*.py") == pygim.path(root).glob("**/*.py")


def test_pathset_of_a_recursive_pattern_holds_the_same_paths(temp_dir):
    root = make_tree(temp_dir / "t")
    assert sorted(os.fspath(p) for p in pygim.path(root).pathset("**/*.py")) == \
        sorted(os.fspath(p) for p in pygim.path(root).glob("**/*.py"))


def in_a_skipped_folder(root, found, skip):
    """The oracle for pathset's skip: whether *found* (as rel() names it) is, or lies beneath, a folder
    — a directory or a link to one, as pathlib's is_dir() says — whose name one of *skip*'s globs matches."""
    parts = [] if found == "." else found.split("/")
    return any(part != ".." and any(fnmatch.fnmatchcase(part, g) for g in skip) and root.joinpath(*parts[:i + 1]).is_dir()
               for i, part in enumerate(parts))


def in_set(root, pattern, **options):
    return [rel(p, root) for p in pygim.path(root).pathset(pattern, **options)]


@pytest.mark.parametrize("pattern", list(PATHLIB))
def test_pathset_keeps_the_kind_is_file_and_is_dir_say(temp_dir, pattern):
    """kind='file' and kind='dir' keep the matches pathlib's is_file() and is_dir() say true of, a
    link followed: a broken link is neither, a link to a folder is a dir."""
    root = make_tree(temp_dir / "t")
    assert in_set(root, pattern, kind="file") == [p for p in PATHLIB[pattern] if (root / p).is_file()]
    assert in_set(root, pattern, kind="dir") == [p for p in PATHLIB[pattern] if (root / p).is_dir()]
    assert in_set(root, pattern, kind=None) == in_set(root, pattern) == PATHLIB[pattern]


SKIPS = [["sub"], ["pkg"], [".*"], ["link_to_pkg", "other"], ["loop"], ["d*", "x"], ["a.py", "b.txt"], ["*"]]


@pytest.mark.parametrize("pattern", list(PATHLIB))
def test_pathset_passes_over_the_folders_skip_names(temp_dir, pattern):
    """A folder skip names — or a link to one — is neither entered nor matched, nor is anything beneath
    it; a file of that name is kept (["a.py", "b.txt"] skips nothing). The folder the walk starts in
    is never asked."""
    root = make_tree(temp_dir / "t")
    for skip in SKIPS:
        expected = [p for p in PATHLIB[pattern] if not in_a_skipped_folder(root, p, skip)]
        assert in_set(root, pattern, skip=skip) == expected, skip
        assert in_set(root, pattern, skip=skip, kind="file") == [p for p in expected if (root / p).is_file()], skip


def test_skip_matches_as_the_pattern_does(temp_dir):
    """skip's globs take the pattern's case_sensitive; any iterable of names will do (a set, a tuple)."""
    root = make_tree(temp_dir / "t")
    assert in_set(root, "**/*.py", skip={"SUB"}) == PATHLIB["**/*.py"]
    assert in_set(root, "**/*.py", skip=("SUB",), case_sensitive=False) == \
        [p for p in sorted(PATHLIB["**/*.py"] + ["A.PY"]) if not p.startswith("pkg/sub/")]


def test_pathset_refuses_a_kind_or_skip_it_cannot_take(temp_dir):
    root = make_tree(temp_dir / "t")
    with pytest.raises(ValueError, match="'file', 'dir' or None, not 'files'"):
        pygim.path(root).pathset("*", kind="files")
    with pytest.raises(TypeError, match="not one string"):
        pygim.path(root).pathset("*", skip="build")     # iterated, it would skip folders named b, u, i, l and d
    with pytest.raises(TypeError, match="as str, not int"):
        pygim.path(root).pathset("*", skip=[1])


def test_a_wide_tree_matches_os_walk(temp_dir):
    """Many directories, so the walk runs on several threads; the files made here are the oracle (no
    links: what os.walk would list, without walking by hand)."""
    root = temp_dir / "wide"
    expected = []
    for d in range(120):
        sub = root / f"p{d % 7}" / f"m{d}"
        sub.mkdir(parents=True)
        for f in range(15):
            (sub / f"f{f}.py").write_text("x")
            expected.append(os.path.join(sub, f"f{f}.py"))
        (sub / "skip.txt").write_text("x")
    assert [os.fspath(p) for p in pygim.path(root).glob("**/*.py")] == sorted(expected)


def test_a_wide_walk_keeps_kind_and_skip_on_every_thread(temp_dir):
    """The parallel walk runs glob_step's rules on each thread: kind and skip hold there as on one."""
    root = temp_dir / "wide"
    expected = []
    for d in range(120):
        sub = root / f"p{d % 7}" / f"m{d}"
        (sub / "build" / "deep").mkdir(parents=True)
        (sub / "build" / "deep" / "f.py").write_text("x")      # beneath a skipped folder: never found
        for f in range(15):
            (sub / f"f{f}.py").write_text("x")
            expected.append(os.path.join(sub, f"f{f}.py"))
    walked = pygim.path(root).pathset("**/*", kind="file", skip=["build"])
    assert [os.fspath(p) for p in walked] == sorted(expected)
    assert [os.fspath(p) for p in pygim.path(root).pathset("**/build/", kind="dir")] == \
        sorted(os.path.join(root, f"p{d % 7}", f"m{d}", "build") for d in range(120))


CHILD = POOL_THREADS + r"""
import os, pathlib, signal, sys, threading, time
import pygim

root, pattern = pathlib.Path(sys.argv[1]), sys.argv[2]
for d in range(3_000):
    (root / f"p{d % 30}" / f"m{d}").mkdir(parents=True)
t = time.perf_counter()
pygim.path(root).glob(pattern)
full = time.perf_counter() - t
sys.setswitchinterval(1.0)   # the helper gets the GIL only once glob has let it go
go = threading.Event()
helper = threading.Thread(target=lambda: (go.wait(), os.kill(os.getpid(), signal.SIGINT)))
helper.start()
go.set()
t = time.perf_counter()
try:
    pygim.path(root).glob(pattern)
    print("completed")
except KeyboardInterrupt:
    print("KeyboardInterrupt")
took = time.perf_counter() - t
helper.join()
print("stopped early", took < full / 2, f"({took * 1000:.1f} of {full * 1000:.1f} ms)")
print("threads left", pool_threads())
"""


@pytest.mark.parametrize("pattern", ["**/*.py", "*/*/*.py"])   # a wildcard level lists every folder of it, ** or not
def test_ctrl_c_stops_a_glob_that_lists_many_folders(temp_dir, pattern):
    done = subprocess.run([sys.executable, "-c", CHILD, str(temp_dir), pattern], capture_output=True, text=True, timeout=120)
    lines = done.stdout.splitlines()
    assert lines and lines[0] == "KeyboardInterrupt", done.stdout + done.stderr
    assert "stopped early True" in done.stdout, done.stdout
    if os.path.isdir("/proc/self/task"):
        assert "threads left 0" in done.stdout, done.stdout
