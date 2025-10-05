"""Helper to load compiled repository extension safely.

This avoids circular import with the python package name `pygim.repository`.
"""
from importlib import machinery, util
import os

def load_repository_extension(package_path: str):
    # Locate shared object starting with repository. and having .so/.pyd
    for name in os.listdir(package_path):
        if name.startswith('repository.') and (name.endswith('.so') or name.endswith('.pyd')):
            full = os.path.join(package_path, name)
            loader = machinery.ExtensionFileLoader('_pygim_repository_ext', full)
            spec = util.spec_from_loader('_pygim_repository_ext', loader)
            if spec is None:
                continue
            mod = util.module_from_spec(spec)
            loader.exec_module(mod)  # type: ignore[arg-type]
            return mod
    raise ImportError('repository extension shared object not found')
