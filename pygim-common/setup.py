#type: ignore
import sys
from pathlib import Path

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup,find_namespace_packages
import toml

ROOT = Path(__file__).parent
sys.path.append(str(ROOT / "pygim"))

from __version__ import __version__

pyproject = toml.loads(Path('pyproject.toml').read_text())

ext_modules = [
    Pybind11Extension("_pygim.common_fast",
        list(str(p) for p in Path(ROOT).rglob("*.cpp")),
        # Example: passing in the version to the compiled code
        define_macros = [('VERSION_INFO', __version__)],
        ),
]

pygim = map(lambda v: ('pygim.' + v), find_namespace_packages('pygim'))
pygim_internal = map(lambda v: ('_pygim.' + v), find_namespace_packages('_pygim'))


cfg = {**pyproject["project"]}
cfg['packages']= list(pygim) + list(pygim_internal) + ['pygim', '_pygim']
cfg['package_dir']={
    '': '.',
    }
cfg['ext_modules']=ext_modules
cfg['cmdclass']={"build_ext": build_ext}
from pprint import pp
pp(cfg)
setup(**cfg)