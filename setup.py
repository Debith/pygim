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


# ── Build environment ──────────────────────────────────────────────────────

conda_prefix = os.environ.get("CONDA_PREFIX") or os.environ.get("PREFIX")
if not conda_prefix and hasattr(sys, "prefix"):
    conda_prefix = sys.prefix


# ── Dependency presets ─────────────────────────────────────────────────────
# Each ext.*.toml can list deps from this table.  Any dep not listed here
# is silently ignored (forward-compatible with future presets like "pg").


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


def _dep_available(dep_name):
    """Return True if the extension for *dep_name* can be built on this platform."""
    if dep_name == "odbc" and sys.platform == "win32":
        # ODBC extensions link against 'odbc' (unixODBC) which doesn't exist on
        # Windows — the Windows equivalent is 'odbc32'.  Skip until the build
        # configuration handles this platform difference.
        print(
            "[setup.py] Skipping odbc extensions: not supported on Windows "
            "(library name incompatibility: 'odbc' vs 'odbc32')."
        )
        return False
    if dep_name == "odbc":
        # unixODBC headers must exist somewhere the compiler will look;
        # otherwise the persistence extensions cannot build and must be
        # skipped (their tests auto-skip without the driver anyway).
        include_candidates = [
            Path("/usr/include"),
            Path("/usr/local/include"),
            Path("/opt/homebrew/include"),
            Path(sys.prefix) / "include",
        ]
        if conda_prefix:
            include_candidates.append(Path(conda_prefix) / "include")
        if not any((inc / "sql.h").exists() for inc in include_candidates):
            print(
                "[setup.py] Skipping odbc extensions: unixODBC headers "
                "(sql.h) not found in any known include directory."
            )
            return False
        return True
    if dep_name == "arrow" and sys.platform == "win32":
        # pyarrow pip package on Windows ships DLLs but not the import
        # libraries (.lib) that MSVC needs at link time.  Skip unless
        # Arrow C++ is installed separately (e.g. via conda).
        try:
            import pyarrow as _pa

            for d in _pa.get_library_dirs():
                if (Path(d) / "arrow.lib").exists():
                    return True
        except ImportError:
            pass
        if conda_prefix:
            lib = Path(f"{conda_prefix}/Library/lib")
            if (lib / "arrow.lib").exists():
                return True
        print(
            "[setup.py] Skipping arrow extensions on Windows: "
            "Arrow C++ import libraries (.lib) not found."
        )
        return False
    return True  # All other deps must be present; build fails fast if not.


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
    kw.setdefault("libraries", []).extend(["odbc", "parquet"])
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

for ext_toml in sorted(FAST_ROOT.rglob("ext.*.toml")):
    ext_cfg = tomllib.loads(ext_toml.read_text(encoding="utf-8"))["extension"]
    module_name = f"pygim.{ext_cfg['module']}"

    # Skip extensions whose system dependencies are not installed.
    deps = ext_cfg.get("deps", [])
    missing = [d for d in deps if not _dep_available(d)]
    if missing:
        print(f"[setup.py] Skipping {module_name}: missing system deps {missing}")
        continue

    # Resolve sources relative to the colocated manifest.
    ext_stem = ext_toml.stem.split(".", 1)[1]  # "ext.factory" → "factory"
    if "sources" in ext_cfg:
        sources = [str(ext_toml.parent / s) for s in ext_cfg["sources"]]
    else:
        sources = [str(ext_toml.parent / f"{ext_stem}.cpp")]

    # Build kwargs from dep presets
    kwargs = _base_kwargs()
    for dep in deps:
        configurator = _DEP_CONFIGURATORS.get(dep)
        if configurator:
            configurator(kwargs)

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
