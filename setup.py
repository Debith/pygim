#type: ignore
import pprint
import sys
from pathlib import Path

# Available at setup time due to pyproject.toml
from setuptools import setup, find_namespace_packages
import toml

pyproject = toml.loads(Path('pyproject.toml').read_text())
ext_modules = []

cfg = {**pyproject["project"]}
cfg['package_dir']={
    '': './src/',
    }
cfg['ext_modules'] = ext_modules
# Explicitly find packages in src directory
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