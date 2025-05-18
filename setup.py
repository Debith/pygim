#type: ignore
import pprint
import sys
from pathlib import Path

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup, find_namespace_packages
import toml

ROOT = Path(__file__).parent
sys.path.append(str(ROOT / "src"))
from pygim.__version__ import __version__

pyproject = toml.loads(Path('pyproject.toml').read_text())
ext_modules = []

def get_cpp_files(path):
    cpp_files = list(p.resolve() for p in Path(path).rglob("*.cpp"))
    for cpp_file in cpp_files:
        cpp_file.touch()
    return [str(f) for f in cpp_files]

ext_modules = [
    Pybind11Extension("pygim.pathset",
        ['src/_pygim_fast/pathset.cpp'],
        # Example: passing in the version to the compiled code
        define_macros = [('VERSION_INFO', __version__)],
        extra_compile_args=["--std=c++20", "-O3"],
        #extra_compile_args=["-g", "-march=native", "-O3", "-fopenmp", "-std=c++17"],
        ),
]

cfg = {**pyproject["project"]}
cfg['package_dir']={
    '': './src/',
    }
cfg['ext_modules'] = ext_modules
cfg['packages'] = find_namespace_packages(where='src')
cfg['install_requires'] = cfg.pop('dependencies')

# Map PEP 621 scripts to setuptools entry_points
scripts = cfg.pop('scripts', None)
if scripts:
    cfg['entry_points'] = {'console_scripts': [f'{name}={entry}' for name, entry in scripts.items()]}

# Map PEP 621 optional-dependencies to setuptools extras_require
extras = cfg.pop('optional-dependencies', None)
if extras:
    cfg['extras_require'] = extras

pprint.pprint(cfg)
setup(**cfg)