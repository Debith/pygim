# -*- coding: utf-8 -*-
"""pygim \u2014 Lightweight, high-performance Python utilities backed by C++ extensions."""

# Top-level names resolve LAZILY (PEP 562): importing ``pygim`` — or one clean
# submodule like ``pygim.utils`` — must not drag in every compiled extension.
# A submodule that fails to load (missing build, toolchain/libstdc++ mismatch)
# then only breaks the callers that actually use it.
_LAZY_EXPORTS = {
    "PathSet": ("pygim.pathlike", "PathSet"),
    "path": ("pygim.pathlike", "path"),
    "Registry": ("pygim.registry", "Registry"),
    "Factory": ("pygim.factory", "Factory"),
    "Container": ("pygim.ioc", "Container"),
    # Re-exported from the Python module (not the compiled extension directly)
    # so the top-level entry point shares its SQLAlchemy-URL translation.
    "DataStore": ("pygim.persistence", "DataStore"),
    "acquire_datastore": ("pygim.persistence", "acquire_datastore"),
}

__all__ = [*_LAZY_EXPORTS]


def __getattr__(name):
    try:
        module_name, attr = _LAZY_EXPORTS[name]
    except KeyError:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}") from None
    import importlib

    value = getattr(importlib.import_module(module_name), attr)
    globals()[name] = value   # resolved once: from now on a plain attribute (PEP 562 only fires on a miss)
    return value
