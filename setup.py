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


# ── Build environment ──────────────────────────────────────────────────────
# ODBC and Arrow are required dependencies. If headers or libraries are
# missing the C++ compilation will fail — this is intentional (fail-fast).

conda_prefix = os.environ.get('CONDA_PREFIX') or os.environ.get('PREFIX')
if not conda_prefix and hasattr(sys, 'prefix'):
    conda_prefix = sys.prefix


def odbc_kwargs():
    """Return kwargs for an ODBC + Arrow extension (both required)."""
    kw = {
        "libraries": ["odbc", "arrow", "parquet"],
        "include_dirs": [],
        "library_dirs": [],
        "extra_link_args": [],
        "extra_compile_args": list(extra_compile_args),
    }

    mssql_odbc_lib = Path("/opt/microsoft/msodbcsql18/lib64")
    if mssql_odbc_lib.exists():
        kw["library_dirs"].append(str(mssql_odbc_lib))
        lib_files = list(mssql_odbc_lib.glob("libmsodbcsql-*.so*"))
        if lib_files:
            kw["libraries"].append(f":{lib_files[0].name}")
            kw["extra_link_args"].append(f"-Wl,-rpath,{mssql_odbc_lib}")

    if conda_prefix:
        kw["include_dirs"].append(f"{conda_prefix}/include")
        kw["library_dirs"].append(f"{conda_prefix}/lib")

    return kw


# ── Detail source bundles ──────────────────────────────────────────────────

repo_v2_detail_root = Path("src/_pygim_fast/repository/adapter/detail")
repo_v2_detail_sources = sorted(repo_v2_detail_root.rglob("*.cpp")) if repo_v2_detail_root.exists() else []

bcp_strategy_cpp = Path("src/_pygim_fast/repository/mssql_strategy/detail/bcp/bcp_strategy.cpp")
bcp_sources = [str(bcp_strategy_cpp)] if bcp_strategy_cpp.exists() else []


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
    if stem == "_repository":
        # _repository bundles adapter/detail + BCP sources.
        sources = [str(cpp_file)] + [str(p) for p in repo_v2_detail_sources] + bcp_sources
        kwargs.update(odbc_kwargs())
    elif stem == "mssql_strategy":
        # Old monolith — superseded by _repository; skip.
        continue
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
