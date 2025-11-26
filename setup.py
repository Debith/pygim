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


# Pick sensible flags per‐compiler
if sys.platform == "win32":
    # MSVC: enable C++17 (or C++20 if your toolchain supports it) and optimize
    extra_compile_args = ["/std:c++20", "/O2"]
    # or for VC++2022 with C++20: ["/std:c++20", "/O2"]
else:
    extra_compile_args = ["-std=c++20", "-O3"]


detail_root = Path("src/_pygim_fast/repository/mssql_strategy/detail")
mssql_detail_sources = []
if detail_root.exists():
    mssql_detail_sources = sorted(detail_root.glob("*.cpp"))

ext_modules = []
for cpp_file in get_cpp_files("src/_pygim_fast"):
    if "mssql_strategy/detail" in str(cpp_file).replace("\\", "/"):
        continue
    stem = cpp_file.stem
    kwargs = {}
    macros = list(base_macros)
    if stem == "mssql_strategy":
        sources = [str(cpp_file)] + [str(p) for p in mssql_detail_sources]
        from subprocess import run, DEVNULL
        probe_base = ROOT / "__odbc_probe__"
        probe_c = probe_base.with_suffix('.c')
        # First: header presence
        header_code = "#include <sql.h>\nint main(){return 0;}"
        build_mssql = False
        try:
            probe_c.write_text(header_code)
            if run(["gcc", "-c", str(probe_c), "-o", str(probe_base.with_suffix('.o'))], stdout=DEVNULL, stderr=DEVNULL).returncode == 0:
                # Second: link test with -lodbc
                link_obj = probe_base.with_suffix('.o')
                if run(["gcc", str(link_obj), "-lodbc", "-o", str(probe_base)], stdout=DEVNULL, stderr=DEVNULL).returncode == 0:
                    build_mssql = True
                else:
                    print("[setup] Skipping mssql_strategy (ODBC library link failed)")
            else:
                print("[setup] Skipping mssql_strategy (ODBC headers not found)")
        finally:
            for p in [probe_c, probe_base.with_suffix('.o'), probe_base]:
                if p.exists():
                    try:
                        p.unlink()
                    except Exception:
                        pass
        if not build_mssql:
            continue

        # Probe for Arrow C++ library (optional feature for BCP optimization)
        arrow_available = False
        arrow_probe_c = probe_base.with_suffix('.arrow.cpp')
        arrow_code = "#include <arrow/api.h>\nint main(){return 0;}"

        # Check for conda environment (common case)
        import sys
        conda_prefix = os.environ.get('CONDA_PREFIX') or os.environ.get('PREFIX')
        if not conda_prefix and hasattr(sys, 'prefix'):
            conda_prefix = sys.prefix

        extra_includes = []
        extra_libs = []
        if conda_prefix:
            extra_includes = [f"-I{conda_prefix}/include"]
            extra_libs = [f"-L{conda_prefix}/lib"]

        try:
            arrow_probe_c.write_text(arrow_code)
            # Try to compile with arrow headers (check conda env)
            compile_cmd = ["g++", "-std=c++20", "-c", str(arrow_probe_c)] + extra_includes + ["-o", str(probe_base.with_suffix('.arrow.o'))]
            if run(compile_cmd, stdout=DEVNULL, stderr=DEVNULL).returncode == 0:
                # Try to link with -larrow
                link_cmd = ["g++", str(probe_base.with_suffix('.arrow.o'))] + extra_libs + ["-larrow", "-o", str(probe_base.with_suffix('.arrow'))]
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
                    try:
                        p.unlink()
                    except Exception:
                        pass

        kwargs["libraries"] = ["odbc"]
        kwargs["include_dirs"] = []
        kwargs["library_dirs"] = []
        kwargs["extra_link_args"] = []

        # Check for SQL Server ODBC Driver 18 (for BCP functions on Linux)
        mssql_odbc_lib = Path("/opt/microsoft/msodbcsql18/lib64")
        if mssql_odbc_lib.exists():
            kwargs["library_dirs"].append(str(mssql_odbc_lib))
            # Use -l:filename to link specific library file
            lib_files = list(mssql_odbc_lib.glob("libmsodbcsql-*.so*"))
            if lib_files:
                lib_name = lib_files[0].name
                kwargs["libraries"].append(f":{lib_name}")
                # Add rpath for runtime
                kwargs["extra_link_args"].append(f"-Wl,-rpath,{mssql_odbc_lib}")

        if arrow_available:
            kwargs["libraries"].extend(["arrow", "parquet"])
            macros.append(("PYGIM_HAVE_ARROW", 1))
            if conda_prefix:
                kwargs["include_dirs"].append(f"{conda_prefix}/include")
                kwargs["library_dirs"].append(f"{conda_prefix}/lib")
        else:
            kwargs["extra_compile_args"] = extra_compile_args
        if "extra_compile_args" not in kwargs:
            kwargs["extra_compile_args"] = extra_compile_args
        macros.append(("PYGIM_HAVE_ODBC", 1))
    else:
        # Non-MSSQL extensions: just use default compile args
        kwargs["extra_compile_args"] = extra_compile_args
        sources = [str(cpp_file)]

    ext_modules.append(
        Pybind11Extension(
            f"pygim.{stem}",
            sources,
            define_macros=macros,
            **kwargs
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
