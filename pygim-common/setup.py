#type: ignore
import importlib
from pathlib import Path

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup,find_packages
import toml

ROOT = Path(__file__).parent
version_file = ROOT / "pygim/__version__.py"

spec = importlib.util.spec_from_file_location(version_file)
version_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version_module)
__version__ = version_module.__version__

pyproject = toml.loads(Path('pyproject.toml').read_text())


ext_modules = [
    Pybind11Extension("utils.fast_iterable",
        [
            "_pygim/_utils/iterable_fast.cpp",
            "_pygim/_utils/flatten.cpp",
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