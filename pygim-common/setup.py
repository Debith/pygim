# type: ignore
import sys
from pathlib import Path

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup, find_namespace_packages
import toml
import psutil

ROOT = Path(__file__).parent
sys.path.append(str(ROOT / "pygim"))

from __version__ import __version__

pyproject = toml.loads(Path('pyproject.toml').read_text())
ext_modules = []

# This is a bit hacky way to resolve passing arguments to setup.py.
# Invoked by:
# $ python -m build -w --config-setting python_only
if not psutil.Process().parent().cmdline()[-1] == "python_only":
    ext_modules = [
        Pybind11Extension("_pygim.common_fast",
            list(str(p) for p in Path(ROOT).rglob("*.cpp")),
            # Example: passing in the version to the compiled code
            define_macros = [('VERSION_INFO', __version__)],
            extra_compile_args=["--std=c++17", "-O3"],
            #extra_compile_args=["-g", "-march=native", "-O3", "-fopenmp", "-std=c++17"],
            ),
    ]

pygim = map(lambda v: ("pygim." + v), find_namespace_packages("pygim"))
pygim_internal = map(lambda v: ("_pygim." + v), find_namespace_packages("_pygim"))

cfg = {**pyproject["project"]}
cfg["packages"] = list(pygim) + list(pygim_internal) + ["pygim", "_pygim"]
cfg["package_dir"] = {
    "": ".",
}
cfg["ext_modules"] = ext_modules
cfg["cmdclass"] = {"build_ext": build_ext}
cfg["install_requires"] = cfg.pop("dependencies")

import pprint
pprint.pprint(cfg)
setup(**cfg)
