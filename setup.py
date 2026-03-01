# type: ignore
import sys
import os
import pprint
from pathlib import Path
from subprocess import run, DEVNULL
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


def get_cpp_files(path):
    cpp_files = list(p for p in Path(path).rglob("*.cpp"))
    return cpp_files


def extension_name_from_cpp(cpp_file: Path) -> str:
    if cpp_file.stem == "bindings":
        return f"pygim.{cpp_file.parent.name}"
    return f"pygim.{cpp_file.stem}"


# Pick sensible flags per‐compiler
if sys.platform == "win32":
    extra_compile_args = ["/std:c++20", "/O2"]
else:
    extra_compile_args = ["-std=c++20", "-O3"]


# ── Upfront ODBC + Arrow feature probes ────────────────────────────────────
# Run once before the extension loop so all modules can reference the result.

probe_base = ROOT / "__odbc_probe__"
probe_c = probe_base.with_suffix('.c')
build_mssql = False
arrow_available = False
conda_prefix = os.environ.get('CONDA_PREFIX') or os.environ.get('PREFIX')
if not conda_prefix and hasattr(sys, 'prefix'):
    conda_prefix = sys.prefix

# ODBC probe
header_code = "#include <sql.h>\nint main(){return 0;}"
try:
    probe_c.write_text(header_code)
    if run(["gcc", "-c", str(probe_c), "-o", str(probe_base.with_suffix('.o'))],
           stdout=DEVNULL, stderr=DEVNULL).returncode == 0:
        link_obj = probe_base.with_suffix('.o')
        if run(["gcc", str(link_obj), "-lodbc", "-o", str(probe_base)],
               stdout=DEVNULL, stderr=DEVNULL).returncode == 0:
            build_mssql = True
        else:
            print("[setup] Skipping ODBC modules (ODBC library link failed)")
    else:
        print("[setup] Skipping ODBC modules (ODBC headers not found)")
finally:
    for p in [probe_c, probe_base.with_suffix('.o'), probe_base]:
        if p.exists():
            try: p.unlink()
            except Exception: pass

# Arrow probe (only if ODBC is available)
if build_mssql:
    extra_includes = [f"-I{conda_prefix}/include"] if conda_prefix else []
    extra_libs = [f"-L{conda_prefix}/lib"] if conda_prefix else []
    arrow_probe_c = probe_base.with_suffix('.arrow.cpp')
    arrow_code = "#include <arrow/api.h>\nint main(){return 0;}"
    try:
        arrow_probe_c.write_text(arrow_code)
        compile_cmd = ["g++", "-std=c++20", "-c", str(arrow_probe_c)] + extra_includes + \
                      ["-o", str(probe_base.with_suffix('.arrow.o'))]
        if run(compile_cmd, stdout=DEVNULL, stderr=DEVNULL).returncode == 0:
            link_cmd = ["g++", str(probe_base.with_suffix('.arrow.o'))] + extra_libs + \
                       ["-larrow", "-o", str(probe_base.with_suffix('.arrow'))]
            if run(link_cmd, stdout=DEVNULL, stderr=DEVNULL).returncode == 0:
                arrow_available = True
                print("[setup] Arrow C++ detected - enabling BCP optimization path")
            else:
                print("[setup] Arrow headers found but library link failed - BCP optimization disabled")
        else:
            print("[setup] Arrow C++ not detected - BCP optimization path disabled")
    except Exception as e:
        print(f"[setup] Arrow probe exception: {e}")
    finally:
        for p in [arrow_probe_c, probe_base.with_suffix('.arrow.o'), probe_base.with_suffix('.arrow')]:
            if p.exists():
                try: p.unlink()
                except Exception: pass


def odbc_kwargs():
    """Return kwargs, macros additions for an ODBC-dependent extension."""
    kw = {
        "libraries": ["odbc"],
        "include_dirs": [],
        "library_dirs": [],
        "extra_link_args": [],
        "extra_compile_args": list(extra_compile_args),
    }
    extra_macros = [("PYGIM_HAVE_ODBC", 1)]

    mssql_odbc_lib = Path("/opt/microsoft/msodbcsql18/lib64")
    if mssql_odbc_lib.exists():
        kw["library_dirs"].append(str(mssql_odbc_lib))
        lib_files = list(mssql_odbc_lib.glob("libmsodbcsql-*.so*"))
        if lib_files:
            kw["libraries"].append(f":{lib_files[0].name}")
            kw["extra_link_args"].append(f"-Wl,-rpath,{mssql_odbc_lib}")

    if arrow_available:
        kw["libraries"].extend(["arrow", "parquet"])
        extra_macros.append(("PYGIM_HAVE_ARROW", 1))
        if conda_prefix:
            kw["include_dirs"].append(f"{conda_prefix}/include")
            kw["library_dirs"].append(f"{conda_prefix}/lib")

    return kw, extra_macros


# ── Detail source bundles ──────────────────────────────────────────────────

repo_v2_detail_root = Path("src/_pygim_fast/repository/adapter/detail")
repo_v2_detail_sources = sorted(repo_v2_detail_root.rglob("*.cpp")) if repo_v2_detail_root.exists() else []


# ── Extension module loop ──────────────────────────────────────────────────

ext_modules = []
for cpp_file in get_cpp_files("src/_pygim_fast"):
    if "mssql_strategy/detail" in str(cpp_file).replace("\\", "/"):
        continue
    if "adapter/detail" in str(cpp_file).replace("\\", "/"):
        continue
    stem = cpp_file.stem
    kwargs = {}
    macros = list(base_macros)
    if stem == "repository_v2":
        # repository_v2 bundles adapter/detail sources (MssqlStrategy v2 impl).
        sources = [str(cpp_file)] + [str(p) for p in repo_v2_detail_sources]
        kwargs["extra_compile_args"] = list(extra_compile_args)
        if build_mssql and repo_v2_detail_sources:
            kw, extra_macros = odbc_kwargs()
            kwargs.update(kw)
            macros.extend(extra_macros)
    else:
        # Standard extensions: just default compile args, single source.
        kwargs["extra_compile_args"] = list(extra_compile_args)
        sources = [str(cpp_file)]

    ext_modules.append(
        Pybind11Extension(
            extension_name_from_cpp(cpp_file),
            sources,
            define_macros=macros,
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
