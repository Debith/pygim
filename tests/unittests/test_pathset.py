# -*- coding: utf-8 -*-
"""The set-like surface of pygim.PathSet — what the former pygim.pathset module
offered, now over one table: construction from one path or many, bool/==/in,
+ and - with paths or sets, clone, cwd, read_all_files, sizes, match_pattern, and the
Filter / Query algebra (ext, name, absolute; & | ~; ps & f)."""

import fnmatch
import gc
import os

import pytest

import pygim
from pygim import PathSet
from pygim.pathlike import Filter, Query, absolute, ext, match_pattern, name


@pytest.fixture()
def temp_files(temp_dir):
    test_files = [temp_dir / "readme.txt", temp_dir / "readme.rst", temp_dir / "AUTHORS.rst"]
    assert not any(f.is_file() for f in test_files)
    for f in test_files:
        f.touch()
    assert all(f.is_file() for f in test_files)
    yield test_files


# ── construction and set semantics ─────────────────────────────────────────
def test_basics(temp_files):
    assert len(PathSet([])) == 0 and bool(PathSet([])) is False
    assert len(PathSet(temp_files)) == 3 and bool(PathSet(temp_files)) is True
    assert pygim.PathSet is PathSet   # the top-level name is pathlike's class


def test_one_path_is_one_member_not_a_sequence_of_characters(temp_files):
    single = PathSet(str(temp_files[0]))
    assert len(single) == 1 and single[0].name == "readme.txt"
    assert len(PathSet(temp_files[0])) == 1                       # pathlib.Path
    assert len(PathSet(pygim.path(temp_files[0]))) == 1           # a file
    assert len(PathSet(b"a/b")) == 1                              # bytes
    assert PathSet("a/b") == PathSet(["a/b"]) == PathSet([pygim.path("a/b")])


def test_equality(temp_files):
    a = PathSet(temp_files)
    assert a == a and a == PathSet(temp_files) and a != PathSet([])
    assert PathSet(reversed(temp_files)) == a                     # order does not matter
    assert a != PathSet(temp_files[:2])
    other_table = PathSet([str(p) for p in temp_files])           # its own table: compared by chain
    assert other_table == a and a == other_table
    assert PathSet(["x/y", "x//y/"]) == PathSet("x/y")            # spellings collapse


def test_membership(temp_files):
    a = PathSet(temp_files)
    assert str(temp_files[0]) in a and temp_files[1] in a and pygim.path(temp_files[2]) in a
    assert a[0] in a and "nowhere" not in a


def test_subtraction(temp_dir, temp_files):
    files = PathSet(temp_files)
    files -= str(temp_dir / "readme.txt")                         # one path: a new set, the name rebound
    assert len(files) == 2
    files -= PathSet(temp_dir / "readme.rst")
    assert len(files) == 1 and files[0].name == "AUTHORS.rst"
    assert len(PathSet(temp_files) - [temp_files[0], temp_files[1]]) == 1
    assert len(PathSet(temp_files) - "not/a/member") == 3          # subtracting the absent changes nothing


def test_addition_unions_both_operands(temp_files):
    left, right = PathSet(temp_files[:1]), PathSet(temp_files[1:])
    combined = left + right
    assert len(combined) == 3
    assert len(left) == 1 and len(right) == 2                     # operands untouched
    assert len(left + temp_files[1]) == 2 and len(left + temp_files) == 3


def test_cloning(temp_files):
    a = PathSet(temp_files)
    cloned = a.clone()
    assert cloned == a and cloned is not a


def test_modification_after_cloning(temp_dir, temp_files):
    files = PathSet(temp_files)
    cloned = files.clone()
    files -= str(temp_dir / "readme.txt")
    assert len(files) == 2 and len(cloned) == 3


def test_cwd():
    here = PathSet.cwd()
    assert len(here) == 1 and os.fspath(here[0]) == os.getcwd()


# ── filters and queries ────────────────────────────────────────────────────
def test_extension_filter_selects_matching_files(temp_files):
    filtered = (PathSet(temp_files) & ext(".rst")).eval()
    assert isinstance(filtered, PathSet) and len(filtered) == 2
    assert all(v.suffix == ".rst" for v in filtered)
    assert PathSet(temp_files).filter(ext(".rst")) == filtered


SUFFIXED = ["a.h", "b.CPP", "c.py", "d.Hpp", "e.tar.c", "f", ".h", "g.pyi", "h.PY", "i.c++", "j.ç"]


def test_ext_takes_several_suffixes_and_any_case_on_request():
    names = lambda f: [p.name for p in PathSet(SUFFIXED) & f]
    assert names(ext(".h", ".cpp")) == ["a.h"]
    assert names(ext(".h", ".cpp", ".hpp", case_sensitive=False)) == ["a.h", "b.CPP", "d.Hpp"]
    assert names(ext(".C", case_sensitive=False)) == ["e.tar.c"]            # the final suffix only
    assert names(ext(".Ç", case_sensitive=False)) == []                     # ASCII letters fold, no others
    assert names(ext("")) == ["f", ".h"]                                     # pathlib: ".h" has no suffix
    with pytest.raises(TypeError, match="one suffix or more"):
        ext()
    with pytest.raises(TypeError, match="as str, not bool"):
        ext(".py", True)                                                    # case_sensitive is keyword-only


def test_ext_of_a_languages_suffixes_finds_what_language_of_finds():
    """The Filter similarity's files are chosen by: ext(*LANGUAGES[l]["exts"], case_sensitive=False)."""
    similarity = pytest.importorskip("pygim.similarity")
    for language, record in similarity.LANGUAGES.items():
        chosen = [p.name for p in PathSet(SUFFIXED) & ext(*record["exts"], case_sensitive=False)]
        assert chosen == [n for n in SUFFIXED if similarity.language_of(n) == language], language


def test_query_is_lazy_and_keeps_its_source_alive(temp_files):
    query = PathSet(temp_files) & ext(".rst")                     # the source is a temporary
    gc.collect()
    assert isinstance(query, Query) and len(query) == 2 and len(query.eval()) == 2
    assert sorted(v.name for v in query) == ["AUTHORS.rst", "readme.rst"]


def test_chained_query_filters(temp_files):
    assert len((PathSet(temp_files) & ext(".rst")) | ext(".txt")) == 3
    assert len((PathSet(temp_files) & ext(".rst")) & name("read*")) == 1


def test_filter_algebra(temp_files):
    only_txt = (PathSet(temp_files) & ~ext(".rst")).eval()
    assert [v.suffix for v in only_txt] == [".txt"]
    nothing = (PathSet(temp_files) & (ext(".rst") & ext(".txt"))).eval()
    assert len(nothing) == 0
    both = PathSet(temp_files) & (ext(".rst") | ext(".txt"))
    assert len(both) == 3
    assert isinstance(ext(".a") & ~ext(".b") | absolute(), Filter)


def test_filter_on_empty_pathset():
    assert len((PathSet([]) & ext(".txt")).eval()) == 0


def test_filters_read_the_table_not_the_filesystem():
    import pathlib

    # pathlib's is_absolute() needs a drive on Windows: anchor the path on the current one.
    absp = str(pathlib.PurePath(pathlib.Path.cwd().anchor, "etc", "a.yaml"))
    ps = PathSet([absp, "rel/b.yaml", "rel/c.json"])              # none of these exist on disk
    assert ps.filter(absolute()).to_list() == [absp]
    assert len(ps & ext(".yaml") & ~absolute()) == 1
    assert ps.filter(name("?.json")) == PathSet("rel/c.json")


def test_set_algebra_still_works_between_sets(temp_files):
    a, b = PathSet(temp_files[:2]), PathSet(temp_files[1:])
    assert len(a & b) == 1 and len(a | b) == 3 and len(a - b) == 1


# ── the rest of the old surface ────────────────────────────────────────────
def test_read_all_files(temp_dir, temp_files):
    (temp_dir / "readme.txt").write_text("hello")
    (temp_dir / "readme.rst").write_text("world")
    texts = PathSet([temp_dir / "readme.txt", temp_dir / "readme.rst", temp_dir]).read_all_files()
    assert texts == ["hello", "world"]                            # the directory is skipped


def test_sizes_are_each_members_size_in_member_order(temp_dir, temp_files):
    """What path.size() gives each member, in member order — and the OSError it raises, naming the member."""
    for n, f in enumerate(temp_files):
        f.write_bytes(b"x" * (10 * n + 1))
    members = PathSet(list(reversed(temp_files)))
    assert members.sizes() == [p.size() for p in members] == [21, 11, 1]
    assert PathSet([]).sizes() == []
    with pytest.raises(FileNotFoundError) as missing:
        PathSet([temp_files[0], temp_dir / "gone.txt"]).sizes()
    assert missing.value.filename == os.fspath(temp_dir / "gone.txt")
    with pytest.raises(IsADirectoryError):                       # a folder has no size, on every system
        PathSet([temp_dir]).sizes()
    with pytest.raises(IsADirectoryError):                       # MSVC's std::filesystem answered one
        pygim.path(temp_dir).size()


FNMATCH_PATTERNS = ["*.py", "?.py", "[ab]", "[!a]*", "[a-c]?", "[]a]", "[!]a]", "[a-]", "[-a]", "[c-a]", "[ab", "x[!]", "[[]",
                    "*[!.]*", "[é]", "[à-ê]x", "??", "*?é", "a*b*c"]
FNMATCH_NAMES = ["a.py", "é.py", "ab.py", "a", "b", "-", "]", "[ab", "x[!]", "[", "bx", "éx", "é", "aé", "abc", "a.b.c", ".x", ""]


@pytest.mark.parametrize("pattern", FNMATCH_PATTERNS)
def test_match_pattern_reads_a_segment_as_fnmatch_does(pattern):
    """`[seq]` as fnmatch reads it, `?` one character (D49): pathlib's glob matches a name so."""
    assert [n for n in FNMATCH_NAMES if match_pattern(pattern, n)] == [n for n in FNMATCH_NAMES if fnmatch.fnmatchcase(n, pattern)]


def test_match_pattern():
    assert match_pattern("*.txt", "readme.txt") and not match_pattern("*.txt", "logo.png")
    assert match_pattern("read??.txt", "readme.txt") and match_pattern("*", "anything")


def test_pathset_method_shares_the_table(temp_dir, temp_files):
    ps = pygim.path(temp_dir).pathset("*.rst")
    assert isinstance(ps, PathSet) and len(ps) == 2
    v = ps[0]
    assert v == pygim.path(os.fspath(v)) and v.store == pygim.path(temp_dir).store   # rows of one table
    assert pygim.path(temp_files[1]) in ps
