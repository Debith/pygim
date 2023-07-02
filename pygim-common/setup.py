#type: ignore
import sys
from pathlib import Path

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup,find_packages
import toml

ROOT = Path(__file__).parent
sys.path.append(str(ROOT / "pygim"))

from __version__ import __version__
from pygim.fileio.pathset import PathSet

pyproject = toml.loads(Path('pyproject.toml').read_text())

print("===================")
#print(PathSet(ROOT, "*.cpp").transform())

"""
ext_modules = [
    Pybind11Extension("_pygim._pygim_common_fast",
        #PathSet(ROOT, "*.cpp").transform(),
        # Example: passing in the version to the compiled code
        define_macros = [('VERSION_INFO', __version__)],
        ),
]
"""
ext_modules = []
cfg = {**pyproject["project"]}
cfg['packages']=find_packages('pygim')
cfg['package_dir']={'': 'pygim'}
cfg['ext_modules']=ext_modules
cfg['cmdclass']={"build_ext": build_ext}

setup(**cfg)