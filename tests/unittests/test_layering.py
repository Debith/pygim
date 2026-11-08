# -*- coding: utf-8 -*-
"""The layers point one way (docs/definition_of_done.md): a core header is pybind-free.

Everything under src/_pygim_fast/mapping/ and the named core headers must build
without Python — that is what lets their laws be compile-time proofs. Only
adapter headers and bindings may include pybind11."""

import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2] / "src" / "_pygim_fast"
# CI removes src/ before running the suite against the installed package: this
# is a source-tree check, so it skips there rather than fail.
pytestmark = pytest.mark.skipif(not ROOT.is_dir(), reason="source tree not present (testing the installed package)")
CORE = sorted(ROOT.glob("mapping/*.h")) + sorted(ROOT.glob("platform/**/*.h")) + [
    ROOT / "utils" / "hash.h",
    ROOT / "utils" / "memory.h",
    ROOT / "pathlike" / "core.h",
    ROOT / "pathlike" / "uri.h",
    ROOT / "pathlike" / "path_table.h",
    ROOT / "pathlike" / "engine_list.h",
]
PYBIND = re.compile(r'#\s*include\s*[<"]pybind11/')


def _code(header):
    """The header's text with line comments removed (a comment may quote an include)."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in header.read_text(encoding="utf-8").splitlines())


@pytest.mark.parametrize("header", CORE or [ROOT], ids=lambda p: str(p.relative_to(ROOT.parent)))
def test_core_header_is_pybind_free(header):
    text = _code(header)
    assert not PYBIND.search(text), f"{header.relative_to(ROOT)} includes pybind11: core headers must not"
    assert "Py_" not in text and "PyObject" not in text, f"{header.relative_to(ROOT)} touches the Python C API"


def test_core_headers_include_only_core():
    """A core header may include the standard library and other core headers, nothing from adapter/."""
    for header in CORE:
        for inc in re.findall(r'#\s*include\s*"([^"]+)"', _code(header)):
            assert "adapter/" not in inc and "bindings" not in inc, f"{header.relative_to(ROOT)} includes {inc}"


# Every conditional on the operating system outside src/_pygim_fast/platform/, the boundary the
# system's rules and calls belong behind (docs/design/platform_boundary.md, D77): 14 on main before
# the boundary, in five files — pathlike's path text and native rules, the ODBC headers and driver
# loading, the memory probes. None is left: a new one fails here, and belongs behind the boundary.
# A compiler's own checks (_MSC_VER, __clang__) are build portability, not the system's, and are
# not counted.
OS_MACRO = re.compile(r"^[ \t]*#[ \t]*(?:if|ifdef|ifndef|elif)\b.*\b(?:_WIN32|_WIN64|__APPLE__|__linux__|__unix__|__FreeBSD__)\b", re.M)


def os_conditionals(root):
    """Each conditional on the operating system in a header or source under `root` outside
    platform/ and third_party/, as "file:line"."""
    found = []
    for file in sorted([*root.rglob("*.h"), *root.rglob("*.cpp")]):
        relative = file.relative_to(root).as_posix()
        if "third_party" in file.parts or relative.startswith("platform/"):
            continue
        text = file.read_text(encoding="utf-8", errors="replace")
        found += [f"{relative}:{text.count(chr(10), 0, m.start()) + 1}" for m in OS_MACRO.finditer(text)]
    return found


def test_the_os_check_finds_what_it_is_for(tmp_path):
    (tmp_path / "platform").mkdir()
    (tmp_path / "platform" / "native.h").write_text("#ifdef _WIN32\n#endif\n")
    (tmp_path / "a.h").write_text("int x;\n#if defined(__APPLE__)\n#endif\n#ifdef _MSC_VER\n#endif\n")
    assert os_conditionals(tmp_path) == ["a.h:2"]


def test_the_operating_system_is_asked_only_behind_the_platform_boundary():
    found = os_conditionals(ROOT)
    assert found == [], "a conditional on the operating system outside platform/: put the system's rules and calls behind the boundary:\n" + "\n".join(found)


# A branch on a platform value is the same conditional in another spelling: `if constexpr
# (platform::some_flag)` lets a component pick the system's rule itself, where the one sanctioned form
# is a class per system chosen once by the family (platform/native.h, D106). Five of them sat in
# pathlike's path text and the platform's own files header until 2026-11-08.
PLATFORM_BRANCH = re.compile(r"\bif\s*(?:constexpr\s*)?\(\s*!?\s*platform::")


def platform_branches(root):
    """Each branch on a platform value in a header or source under `root` outside platform/ and
    third_party/, as "file:line"."""
    found = []
    for file in sorted([*root.rglob("*.h"), *root.rglob("*.cpp")]):
        relative = file.relative_to(root).as_posix()
        if "third_party" in file.parts or relative.startswith("platform/"):
            continue
        text = file.read_text(encoding="utf-8", errors="replace")
        found += [f"{relative}:{text.count(chr(10), 0, m.start()) + 1}" for m in PLATFORM_BRANCH.finditer(text)]
    return found


def test_the_platform_branch_check_finds_what_it_is_for(tmp_path):
    (tmp_path / "platform").mkdir()
    (tmp_path / "platform" / "files.h").write_text("if constexpr (platform::x) {}\n")
    (tmp_path / "a.h").write_text("using t = std::conditional_t<platform::native == platform::family::windows, W, P>;\nif constexpr (!platform::x) {}\nif (platform::y) {}\n")
    assert platform_branches(tmp_path) == ["a.h:2", "a.h:3"]


def test_no_behaviour_branches_on_the_platform_outside_the_boundary():
    found = platform_branches(ROOT)
    assert found == [], "a branch on a platform value outside platform/: give each system its class and let the family choose it once:\n" + "\n".join(found)


# Inside the boundary (D106): each system's calls are a class in a folder of its own — platform/posix/,
# linux/, macos/, windows/ — and never ask which system they run on; a header directly in platform/
# asks once, in the one #if chain that picks the running system's class (an #elif continues it).
SYSTEM_FOLDERS = ("posix", "linux", "macos", "windows")
OS_ASK = re.compile(r"^[ \t]*#[ \t]*(?:if|ifdef|ifndef)\b.*\b(?:_WIN32|_WIN64|__APPLE__|__linux__|__unix__|__FreeBSD__)\b", re.M)


def platform_asks(platform):
    """How often each header under `platform` opens a conditional on the system, as {"file": n}."""
    return {f.relative_to(platform).as_posix(): len(OS_ASK.findall(f.read_text(encoding="utf-8")))
            for f in sorted(platform.rglob("*.h"))}


def test_the_platform_check_finds_what_it_is_for(tmp_path):
    (tmp_path / "linux").mkdir()
    (tmp_path / "linux" / "memory.h").write_text("#ifdef __linux__\n#endif\n")
    (tmp_path / "memory.h").write_text("#if defined(_WIN32)\n#elif defined(__APPLE__)\n#else\n#endif\n#ifdef _MSC_VER\n#endif\n")
    assert platform_asks(tmp_path) == {"linux/memory.h": 1, "memory.h": 1}


def test_inside_the_boundary_a_header_asks_once_and_a_systems_own_file_never():
    asks = platform_asks(ROOT / "platform")
    in_a_system = {f: n for f, n in asks.items() if f.split("/")[0] in SYSTEM_FOLDERS and n}
    more_than_once = {f: n for f, n in asks.items() if "/" not in f and n > 1}
    assert any(f.split("/")[0] in SYSTEM_FOLDERS for f in asks), "no system's folder under platform/"
    assert not in_a_system, f"a system's own file asks which system it is: {in_a_system}"
    assert not more_than_once, f"a platform header asks more than once (one #if chain picks the class): {more_than_once}"


def test_every_package_that_ships_holds_something():
    """A package of one empty __init__.py ships in every wheel and imports as if it offered
    something: pygim.ai did from 2025-08-07 until Debith dropped it (D78, 2026-10-31)."""
    src = ROOT.parent
    empty = []
    for package in ("pygim", "_pygim"):
        for init in sorted((src / package).rglob("__init__.py")):
            others = [p for p in init.parent.iterdir() if p.name not in ("__init__.py", "__pycache__")]
            if not init.read_text(encoding="utf-8").strip() and not others:
                empty.append(init.parent.relative_to(src).as_posix())
    assert empty == [], "a package holding nothing: give it its code, or remove it"
