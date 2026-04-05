# type: ignore
import sys
import os
import pprint
from pathlib import Path
from setuptools_scm import get_version

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup, find_namespace_packages
import toml

scm_version = get_version(root=".", relative_to=__file__)
ROOT = Path(__file__).parent
sys.path.append(str(ROOT / "src"))

pyproject = toml.loads(Path("pyproject.toml").read_text())
ext_modules = []
base_macros = [("VERSION_INFO", repr(scm_version))]

if os.environ.get("PYGIM_BCP_PROFILING", "").strip() == "1":
    base_macros.append(("PYGIM_BCP_PROFILING", "1"))


# Pick sensible flags per-compiler
if sys.platform == "win32":
    extra_compile_args = ["/std:c++23", "/O2", "/GL"]
    extra_link_args_global = ["/LTCG"]
else:
    extra_compile_args = [
        "-std=c++23",
        "-O3",
        "-march=native",
        "-mtune=native",
        "-funroll-loops",
        "-fno-math-errno",
        "-flto",
    ]
    extra_link_args_global = ["-flto"]


# ── Build environment ──────────────────────────────────────────────────────

conda_prefix = os.environ.get('CONDA_PREFIX') or os.environ.get('PREFIX')
if not conda_prefix and hasattr(sys, 'prefix'):
    conda_prefix = sys.prefix


# ── Dependency presets ─────────────────────────────────────────────────────
# Each ext.*.toml can list deps from this table.  Any dep not listed here
# is silently ignored (forward-compatible with future presets like "pg").

def _base_kwargs():
    return {
        "extra_compile_args": list(extra_compile_args),
        "extra_link_args": list(extra_link_args_global),
    }


def _dep_available(dep_name):
    """Return True if the extension for *dep_name* can be built on this platform."""
    if dep_name == "odbc" and sys.platform == "win32":
        # ODBC extensions link against 'odbc' (unixODBC) which doesn't exist on
        # Windows — the Windows equivalent is 'odbc32'.  Skip until the build
        # configuration handles this platform difference.
        print("[setup.py] Skipping odbc extensions: not supported on Windows "
              "(library name incompatibility: 'odbc' vs 'odbc32').")
        return False
    return True  # All other deps must be present; build fails fast if not.


_DEP_CONFIGURATORS = {
    "arrow": lambda kw: _apply_arrow(kw),
    "odbc":  lambda kw: _apply_odbc(kw),
}


def _apply_arrow(kw):
    # pyarrow (declared in [build-system] requires) bundles Arrow C++ headers
    # and shared libraries — works cross-platform without any system packages.
    import pyarrow as _pa
    kw.setdefault("include_dirs", []).append(_pa.get_include())
    for libdir in _pa.get_library_dirs():
        kw.setdefault("library_dirs", []).append(libdir)
    kw.setdefault("libraries", []).append("arrow")
    # Set RPATH relative to the installed extension so it can locate the
    # bundled Arrow libraries inside the pyarrow package at runtime.
    # Standard pip install places both pygim/ and pyarrow/ as siblings inside
    # site-packages, so @loader_path/../pyarrow (macOS) / $ORIGIN/../pyarrow
    # (Linux) resolves correctly to pyarrow's library directory.
    if sys.platform == "darwin":
        kw.setdefault("extra_link_args", []).append(
            "-Wl,-rpath,@loader_path/../pyarrow")
    elif sys.platform.startswith("linux"):
        kw.setdefault("extra_link_args", []).append(
            "-Wl,-rpath,$ORIGIN/../pyarrow")
    # Also honour conda / system installations (e.g. dev envs with conda-forge Arrow).
    if conda_prefix:
        inc = "Library/include" if sys.platform == "win32" else "include"
        lib = "Library/lib"     if sys.platform == "win32" else "lib"
        kw.setdefault("include_dirs", []).append(f"{conda_prefix}/{inc}")
        kw.setdefault("library_dirs", []).append(f"{conda_prefix}/{lib}")


def _apply_odbc(kw):
    _apply_arrow(kw)  # Arrow + Parquet headers/libs and rpath are shared.
    kw.setdefault("libraries", []).extend(["odbc", "parquet"])
    # MSSQL ODBC Driver 18 shared library (Linux).
    mssql_odbc_lib = Path("/opt/microsoft/msodbcsql18/lib64")
    if mssql_odbc_lib.exists():
        kw.setdefault("library_dirs", []).append(str(mssql_odbc_lib))
        lib_files = list(mssql_odbc_lib.glob("libmsodbcsql-*.so*"))
        if lib_files:
            kw.setdefault("libraries", []).append(f":{lib_files[0].name}")
            kw.setdefault("extra_link_args", []).append(
                f"-Wl,-rpath,{mssql_odbc_lib}")
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
#   sources  = ["path/to/file.cpp"]     # optional; default: "<name>.cpp"
#   deps     = ["arrow", "odbc"]        # optional; default: []

FAST_ROOT = Path("src/_pygim_fast")
ext_modules = []

for ext_toml in sorted(FAST_ROOT.glob("ext.*.toml")):
    ext_cfg = toml.loads(ext_toml.read_text())["extension"]
    module_name = f"pygim.{ext_cfg['module']}"

    # Skip extensions whose system dependencies are not installed.
    deps = ext_cfg.get("deps", [])
    missing = [d for d in deps if not _dep_available(d)]
    if missing:
        print(f"[setup.py] Skipping {module_name}: missing system deps {missing}")
        continue

    # Resolve sources relative to FAST_ROOT
    ext_stem = ext_toml.stem.split(".", 1)[1]  # "ext.factory" → "factory"
    if "sources" in ext_cfg:
        sources = [str(FAST_ROOT / s) for s in ext_cfg["sources"]]
    else:
        sources = [str(FAST_ROOT / f"{ext_stem}.cpp")]

    # Build kwargs from dep presets
    kwargs = _base_kwargs()
    for dep in deps:
        configurator = _DEP_CONFIGURATORS.get(dep)
        if configurator:
            configurator(kwargs)

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
