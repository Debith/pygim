import sys
from pathlib import Path

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup,find_packages
import toml

pyproject = toml.loads(Path('pyproject.toml').read_text())

__version__ = "0.0.1"


ext_modules = [
    Pybind11Extension("pygim.utils.fast_iterable",
        [
            "pygim/utils/fast_iterable.cpp",
        ],
        # Example: passing in the version to the compiled code
        define_macros = [('VERSION_INFO', __version__)],
        ),
]

cfg = {**pyproject["project"]}
cfg['packages']=find_packages('pygim')
cfg['package_dir']={'': 'pygim'}
cfg['ext_modules']=ext_modules
cfg['cmdclass']={"build_ext": build_ext}

setup(**cfg)