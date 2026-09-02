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


# ── Extension discovery via ext.*.toml ─────────────────────────────────────
# Each ext.<name>.toml declares:
#   [extension]
#   module   = "pygim_module_name"      # required
#   sources  = ["path/to/file.cpp"]     # optional; relative to ext.<name>.toml
#   deps     = ["arrow", "odbc"]        # optional; default: []

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
