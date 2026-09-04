# type: ignore
import sys
import os
import pprint
from pathlib import Path
from setuptools_scm import get_version

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup, find_namespace_packages

# We only read TOML build metadata. Prefer the stdlib parser on Python 3.11+;
# supported older builders use the compatible read-only backport instead of the
# heavier third-party `toml` package, whose encoder/comment variants add no value here.
try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11 build fallback
    import tomli as tomllib

scm_version = get_version(root=".", relative_to=__file__)
ROOT = Path(__file__).parent
sys.path.append(str(ROOT / "src"))

pyproject = tomllib.loads(Path("pyproject.toml").read_text(encoding="utf-8"))
ext_modules = []
base_macros = [("VERSION_INFO", repr(scm_version))]

if os.environ.get("PYGIM_BCP_PROFILING", "").strip() == "1":
    base_macros.append(("PYGIM_BCP_PROFILING", "1"))


# Ensure macOS deployment target is high enough for C++23 library features
# (std::format with floating-point and std::to_chars require macOS 13.3+).
if sys.platform == "darwin":
    os.environ.setdefault("MACOSX_DEPLOYMENT_TARGET", "13.3")

# Pick sensible flags per-compiler
if sys.platform == "win32":
    extra_compile_args = ["/std:c++latest", "/O2", "/GL"]
    extra_link_args_global = ["/LTCG"]
else:
    extra_compile_args = [
        "-std=c++23",
        "-O3",
        "-funroll-loops",
        "-fno-math-errno",
        "-flto",
    ]
    extra_link_args_global = ["-flto"]
    if sys.platform.startswith("linux"):
        # Bundle the C++ runtime into each extension so a build with a newer GCC
        # (e.g. system g++-16) still loads in the conda Python, whose own
        # libstdc++ may be older than the compiler's.
        extra_link_args_global.append("-static-libstdc++")
        # Hide the statically linked libstdc++ symbols.  Without this every
        # extension exports its own copy of the C++ runtime, and as soon as a
        # library built against the *dynamic* libstdc++ (pyarrow's libarrow)
        # is loaded into the same process the two runtimes interpose each
        # other -- observed as a segfault in pathlike's TOML reader whenever
        # pyarrow had been imported first.
        extra_link_args_global.append("-Wl,--exclude-libs,ALL")


# ── Build environment ──────────────────────────────────────────────────────

conda_prefix = os.environ.get("CONDA_PREFIX") or os.environ.get("PREFIX")
if not conda_prefix and hasattr(sys, "prefix"):
    conda_prefix = sys.prefix


# ── Dependency presets ─────────────────────────────────────────────────────
# Each ext.*.toml can list deps from this table.  Every listed dep must be
# present at build time; unknown or missing deps abort the build (_require_dep).


def _base_kwargs():
    return {
        "extra_compile_args": list(extra_compile_args),
        "extra_link_args": list(extra_link_args_global),
    }


_STD_PROBE_CACHE = {}


def _supported_flags(flags, std_flag):
    """The subset of *flags* the build compiler accepts (probed one at a time,
    together with the extension's ``-std=`` flag — some switches only exist in
    one dialect: GCC 16 rejects ``-freflection`` outside ``-std=c++26``).

    For optional, compiler-specific switches — e.g. ``-freflection`` (GCC 16's
    P2996 implementation), which the pathlike proofs use where available and
    silently do without elsewhere. Windows never reaches this.
    """
    import shutil
    import subprocess

    cxx = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
    if not cxx:
        return []
    accepted = []
    for flag in flags:
        probe = subprocess.run(
            [cxx, std_flag, flag, "-x", "c++", "-fsyntax-only", "-"],
            input=b"int main(){}", capture_output=True, check=False,
        )
        if probe.returncode == 0:
            accepted.append(flag)
        else:
            print(f"[setup.py] {cxx}: {flag} unsupported, building without it")
    return accepted


def _first_supported_std(requested):
    """The first ``-std=`` dialect the build compiler accepts, walking down
    from *requested*.

    CI images and user machines may ship a compiler that predates the
    requested standard's flag (GCC 13 has no ``-std=c++26``); the code must
    still build, so degrade to the nearest accepted dialect flag rather than
    fail. Windows never reaches this (MSVC uses ``/std:c++latest``).
    """
    if requested in _STD_PROBE_CACHE:
        return _STD_PROBE_CACHE[requested]
    import shutil
    import subprocess

    cxx = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
    fallbacks = {"c++26": ["c++2c", "c++23"], "c++2c": ["c++23"]}
    chosen = requested
    if cxx:
        for cand in [requested, *fallbacks.get(requested, [])]:
            probe = subprocess.run(
                [cxx, f"-std={cand}", "-x", "c++", "-fsyntax-only", "-"],
                input=b"int main(){}", capture_output=True,
            )
            if probe.returncode == 0:
                chosen = cand
                break
    if chosen != requested:
        print(f"[setup.py] {cxx}: -std={requested} unsupported, using -std={chosen}")
    _STD_PROBE_CACHE[requested] = chosen
    return chosen


def _odbc_include_dirs():
    """Directories searched for the unixODBC header ``sql.h`` (non-Windows)."""
    dirs = [
        Path("/usr/include"),
        Path("/usr/local/include"),
        Path("/opt/homebrew/include"),
        Path(sys.prefix) / "include",
    ]
    if conda_prefix:
        dirs.append(Path(conda_prefix) / "include")
    return list(dict.fromkeys(dirs))  # sys.prefix and conda_prefix usually coincide


def _odbc_library_name():
    """ODBC driver-manager link library: unixODBC's ``odbc``, or ``odbc32`` from the Windows SDK."""
    return "odbc32" if sys.platform == "win32" else "odbc"


_DEP_INSTALL_HINTS = {
    "odbc": (
        "the unixODBC development files: `apt install unixodbc-dev` (Debian/Ubuntu), "
        "`yum install unixODBC-devel` (RHEL/manylinux), `brew install unixodbc` (macOS) "
        "or `conda install -c conda-forge unixodbc` (conda env). On Windows, sql.h and "
        "odbc32.lib ship with the Windows SDK used by MSVC."
    ),
    "arrow": "pyarrow (`pip install pyarrow`); its wheels bundle the Arrow C++ headers and libraries.",
}


def _require_dep(dep_name, module_name):
    """Abort the build unless *dep_name* is available for *module_name*.

    pygim is a complete library: every extension is mandatory on every
    platform.  A missing system dependency is therefore a build error with an
    actionable message -- never a silently skipped module, which would only
    resurface much later as an ImportError at runtime (or as a stale binary
    from an older build being picked up by an editable install).
    """

    def fail(problem):
        raise SystemExit(
            f"[setup.py] Cannot build {module_name}: {problem}.\n"
            f"           Install {_DEP_INSTALL_HINTS.get(dep_name, dep_name)}"
        )

    if dep_name == "odbc":
        if sys.platform == "win32":
            return  # sql.h and odbc32.lib come from the Windows SDK bundled with MSVC.
        dirs = _odbc_include_dirs()
        if not any((inc / "sql.h").exists() for inc in dirs):
            fail("unixODBC header sql.h not found in " + ", ".join(str(d) for d in dirs))
        return
    if dep_name == "arrow":
        try:
            import pyarrow as _pa
        except ImportError:
            fail("pyarrow is not importable in the build environment")
        if sys.platform == "win32":
            # MSVC links against import libraries.  pyarrow wheels ship
            # arrow.lib / parquet.lib next to the DLLs (verified for 23.x).
            lib_dirs = [Path(d) for d in _pa.get_library_dirs()]
            if conda_prefix:
                lib_dirs.append(Path(conda_prefix) / "Library" / "lib")
            if not any((d / "arrow.lib").exists() for d in lib_dirs):
                fail("Arrow import library arrow.lib not found in "
                     + ", ".join(str(d) for d in lib_dirs))
        return
    if dep_name not in _DEP_CONFIGURATORS:
        fail(f"unknown dependency preset {dep_name!r} in its ext.*.toml "
             f"(known: {sorted(_DEP_CONFIGURATORS)})")


def _require_compiler():
    """Abort before compilation if no usable C++ compiler is available.

    Repository rule: anything the build needs must be verified to exist
    before compilation begins.  Without this, setuptools fails mid-build
    with the far less actionable "command 'g++' failed: No such file or
    directory" (observed when building outside an activated conda env).
    """
    if sys.platform == "win32":
        return  # MSVC is located by setuptools via vswhere; no cheap probe exists.
    import shutil

    cxx = os.environ.get("CXX")
    if cxx and shutil.which(cxx.split()[0]):
        return
    if any(shutil.which(c) for c in ("c++", "g++", "clang++")):
        return
    raise SystemExit(
        "[setup.py] No C++ compiler found (checked $CXX, c++, g++, clang++).\n"
        "           Install one: `conda install -c conda-forge cxx-compiler` (or activate the\n"
        "           conda env that provides it), `apt install g++` (Debian/Ubuntu), or the\n"
        "           Xcode command-line tools (macOS)."
    )


_DEP_CONFIGURATORS = {
    "arrow": lambda kw: _apply_arrow(kw),
    "odbc": lambda kw: _apply_odbc(kw),
}


def _ensure_arrow_symlinks(libdir):
    """Create unversioned .so/.dylib symlinks for pyarrow-bundled Arrow libs.

    pyarrow ships versioned shared objects (e.g. libarrow.so.2300) but omits
    the unversioned symlinks (libarrow.so) that the linker expects when using
    ``-larrow``.  Create them if missing; silently skip on permission errors.
    """
    p = Path(libdir)
    for name in (
        "arrow",
        "parquet",
        "arrow_python",
        "arrow_acero",
        "arrow_dataset",
        "arrow_compute",
    ):
        if sys.platform == "darwin":
            target = p / f"lib{name}.dylib"
            candidates = sorted(p.glob(f"lib{name}.*.dylib"))
        else:
            target = p / f"lib{name}.so"
            candidates = sorted(p.glob(f"lib{name}.so.*"))
        if target.exists() or not candidates:
            continue
        try:
            target.symlink_to(candidates[-1].name)
        except OSError:
            pass


def _apply_arrow(kw):
    # pyarrow (declared in [build-system] requires) bundles Arrow C++ headers
    # and shared libraries — works cross-platform without any system packages.
    import pyarrow as _pa

    kw.setdefault("include_dirs", []).append(_pa.get_include())
    for libdir in _pa.get_library_dirs():
        kw.setdefault("library_dirs", []).append(libdir)
        # Ensure the unversioned symlinks exist for the linker.
        _ensure_arrow_symlinks(libdir)
    kw.setdefault("libraries", []).append("arrow")
    # Set RPATH relative to the installed extension so it can locate the
    # bundled Arrow libraries inside the pyarrow package at runtime.
    # Standard pip install places both pygim/ and pyarrow/ as siblings inside
    # site-packages, so @loader_path/../pyarrow (macOS) / $ORIGIN/../pyarrow
    # (Linux) resolves correctly to pyarrow's library directory.
    if sys.platform == "darwin":
        kw.setdefault("extra_link_args", []).append(
            "-Wl,-rpath,@loader_path/../pyarrow"
        )
    elif sys.platform.startswith("linux"):
        kw.setdefault("extra_link_args", []).append("-Wl,-rpath,$ORIGIN/../pyarrow")
    # Also honour conda / system installations (e.g. dev envs with conda-forge Arrow).
    if conda_prefix:
        inc = "Library/include" if sys.platform == "win32" else "include"
        lib = "Library/lib" if sys.platform == "win32" else "lib"
        kw.setdefault("include_dirs", []).append(f"{conda_prefix}/{inc}")
        kw.setdefault("library_dirs", []).append(f"{conda_prefix}/{lib}")


def _apply_odbc(kw):
    _apply_arrow(kw)  # Arrow + Parquet headers/libs and rpath are shared.
    kw.setdefault("libraries", []).extend([_odbc_library_name(), "parquet"])
    # Base standard is already C++23 (required for std::expected etc.).
    # MSSQL ODBC Driver 18 shared library (Linux).
    mssql_odbc_lib = Path("/opt/microsoft/msodbcsql18/lib64")
    if mssql_odbc_lib.exists():
        kw.setdefault("library_dirs", []).append(str(mssql_odbc_lib))
        lib_files = list(mssql_odbc_lib.glob("libmsodbcsql-*.so*"))
        if lib_files:
            kw.setdefault("libraries", []).append(f":{lib_files[0].name}")
            kw.setdefault("extra_link_args", []).append(f"-Wl,-rpath,{mssql_odbc_lib}")
    # unixODBC headers and library installed via Homebrew (macOS).
    for brew_prefix in [Path("/opt/homebrew"), Path("/usr/local")]:
        brew_inc = brew_prefix / "include"
        brew_lib = brew_prefix / "lib"
        if brew_inc.exists():
            kw.setdefault("include_dirs", []).append(str(brew_inc))
        if brew_lib.exists():
            kw.setdefault("library_dirs", []).append(str(brew_lib))


# ── Open type lists: [extension.typelist] ──────────────────────────────────
# An extension whose plug-ins are "one header each in a directory" (pathlike's
# engines) declares the directory, and the build turns it into ONE generated
# header holding the includes and a C++ type list:
#
#   [extension.typelist]
#   glob      = "adapter/engines/*.h"            # relative to the manifest
#   header    = "pathlike_engines.gen.h"         # written to build/gen/<module>/
#   namespace = "pygim::pathlike::engines"       # each header defines <namespace>::<stem>
#   template  = "pygim::pathlike::engine_list"   # the variadic registry template
#   alias     = "pygim::pathlike::Engines"       # the alias the module consumes
#
# This is the portable stand-in for static reflection: the C++ still has to
# #include the plug-in headers from somewhere, and where P2996 reflection is
# available (GCC 16, -freflection) the extension proves the generated list
# equals what the compiler sees. The generated header is build-local (build/
# is git-ignored) and rewritten only when its content changes.


def _apply_typelist(ext_toml, ext_cfg, kwargs, gen_root=None):
    """Generate the type-list header for *ext_cfg*'s ``typelist`` table into
    ``<gen_root>/<module>/`` (default ``build/gen``); returns its path. Adds the
    generated directory and the manifest's directory to the include path."""
    tl = ext_cfg["typelist"]
    headers = sorted(ext_toml.parent.glob(tl["glob"]), key=lambda h: h.stem)
    if not headers:
        sys.exit(f"[setup.py] {ext_cfg['module']}: typelist glob {tl['glob']!r} matched no files "
                 f"under {ext_toml.parent}")
    stems = [h.stem for h in headers]
    if len(set(stems)) != len(stems):
        sys.exit(f"[setup.py] {ext_cfg['module']}: duplicate typelist stems {stems}")
    plug_dir = Path(tl["glob"]).parent
    alias_ns, _, alias_leaf = tl["alias"].rpartition("::")
    members = ", ".join(tl["namespace"] + "::" + stem for stem in stems)
    lines = [
        f"// GENERATED by setup.py from {ext_toml.parent.as_posix()}/{tl['glob']} — do not edit.",
        "// To add a plug-in, add a header there; it is discovered on the next build.",
        "#pragma once",
        *(f'#include "{(plug_dir / h.name).as_posix()}"' for h in headers),
        f"namespace {alias_ns} {{",
        f"using {alias_leaf} = {tl['template']}<{members}>;",
        f"}}  // namespace {alias_ns}",
        "",
    ]
    text = "\n".join(lines)
    out = Path(gen_root or ROOT / "build" / "gen") / ext_cfg["module"] / tl["header"]
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text(encoding="utf-8") != text:
        out.write_text(text, encoding="utf-8")
    kwargs.setdefault("include_dirs", []).extend([str(out.parent), str(ext_toml.parent)])
    print(f"[setup.py] {ext_cfg['module']}: type list of {len(stems)} plug-ins "
          f"({', '.join(stems)}) -> {out}")
    return str(out)


def _header_dependencies(sources, search_dirs):
    """Every header reachable from *sources* through quoted ``#include "..."``
    lines, resolved relative to the including file and then to *search_dirs*
    (transitively; system includes are ignored). setuptools only tracks the
    .cpp sources by default, so a header-only edit — including one in another
    extension's directory, such as the shared registry core — would otherwise
    leave a stale .so behind."""
    import re

    include_re = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
    seen = set()
    todo = [Path(s).resolve() for s in sources]
    while todo:
        f = todo.pop()
        try:
            text = f.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for rel in include_re.findall(text):
            for base in [f.parent, *map(Path, search_dirs)]:
                cand = (base / rel).resolve()
                if cand.is_file():
                    if cand not in seen:
                        seen.add(cand)
                        todo.append(cand)
                    break
    return sorted(str(h) for h in seen)


# ── Extension discovery via ext.*.toml ─────────────────────────────────────
# Each ext.<name>.toml declares:
#   [extension]
#   module   = "pygim_module_name"      # required
#   sources  = ["path/to/file.cpp"]     # optional; relative to ext.<name>.toml
#   deps     = ["arrow", "odbc"]        # optional; default: []
#   std      = "c++26"                  # optional; per-extension standard (degrades if unsupported)
#   flags_if_supported = ["-freflection"]   # optional; each kept only if the compiler accepts it

FAST_ROOT = Path("src/_pygim_fast")
ext_modules = []

_require_compiler()

for ext_toml in sorted(FAST_ROOT.rglob("ext.*.toml")):
    ext_cfg = tomllib.loads(ext_toml.read_text(encoding="utf-8"))["extension"]
    module_name = f"pygim.{ext_cfg['module']}"

    # Every extension is mandatory: a missing system dependency aborts the build.
    deps = ext_cfg.get("deps", [])
    for dep in deps:
        _require_dep(dep, module_name)

    # Resolve sources relative to the colocated manifest.
    ext_stem = ext_toml.stem.split(".", 1)[1]  # "ext.factory" → "factory"
    if "sources" in ext_cfg:
        sources = [str(ext_toml.parent / s) for s in ext_cfg["sources"]]
    else:
        sources = [str(ext_toml.parent / f"{ext_stem}.cpp")]

    # Build kwargs from dep presets
    kwargs = _base_kwargs()
    for dep in deps:
        _DEP_CONFIGURATORS[dep](kwargs)

    # Optional per-extension C++ standard override (default: the global standard).
    # e.g. ext.pathlike.toml sets std = "c++26" while the rest stay on c++23.
    std = ext_cfg.get("std")
    if std:
        args = kwargs["extra_compile_args"]
        args[:] = [a for a in args if not (a.startswith("-std=") or a.startswith("/std:"))]
        args.append("/std:c++latest" if sys.platform == "win32"
                    else f"-std={_first_supported_std(std)}")

    # Optional per-extension flags, kept only if this compiler accepts them
    # (e.g. flags_if_supported = ["-freflection"]).
    if sys.platform != "win32" and ext_cfg.get("flags_if_supported"):
        std_flag = next(a for a in kwargs["extra_compile_args"] if a.startswith("-std="))
        kwargs["extra_compile_args"].extend(_supported_flags(ext_cfg["flags_if_supported"], std_flag))

    # Open type lists (plug-in headers discovered into a generated header).
    generated = [_apply_typelist(ext_toml, ext_cfg, kwargs)] if "typelist" in ext_cfg else []

    # Every header the sources reach through quoted includes is a build
    # dependency, so a header-only edit rebuilds exactly the extensions that
    # include it (setuptools tracks the .cpp sources only by default).
    local_include_dirs = [d for d in kwargs.get("include_dirs", []) if Path(d).resolve().is_relative_to(ROOT.resolve())]
    kwargs["depends"] = sorted(set(generated) | set(_header_dependencies(sources + generated, [ext_toml.parent, *local_include_dirs])))

    ext_modules.append(
        Pybind11Extension(
            module_name,
            sources,
            define_macros=list(base_macros),
            **kwargs,
        )
    )

cfg = {**pyproject["project"]}
cfg["package_dir"] = {
    "": "./src/",
}
cfg["ext_modules"] = ext_modules
cfg["packages"] = find_namespace_packages(where="src")
cfg["install_requires"] = cfg.pop("dependencies")

# Map PEP 621 scripts to setuptools entry_points
scripts = cfg.pop("scripts", None)
if scripts:
    cfg["entry_points"] = {
        "console_scripts": [f"{name}={entry}" for name, entry in scripts.items()]
    }

# Map PEP 621 optional-dependencies to setuptools extras_require
extras = cfg.pop("optional-dependencies", None)
if extras:
    cfg["extras_require"] = extras

pprint.pprint(cfg)
setup(**cfg)
