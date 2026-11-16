# -*- coding: utf-8 -*-
"""
This module contains script to run coverage for specific module.
"""

import sys
from importlib import reload
from contextlib import contextmanager
import pytest

__all__ = ["measure_coverage", "run_tests"]


@contextmanager
def measure_coverage(*, include=None, show_missing: bool = True):
    """Run code coverage for the code executed in this context manager.

    Parameters
    ----------
    include : str or pathlib.Path, optional
        File (or file pattern) to be included in the coverage report. If None, all shown.
    show_missing : bool, optional
        True, if coverage report should include lines that were not run.

    Coverage stops, saves and reports whether the code in the context returns or raises.
    """
    # FIXME: Running pytest fails, when `coverage` module is being imported.
    #        The error appears to be originating from os.getcwd(), which
    #        indicates that some folder is interpreted as current is deleted.
    import coverage

    cov = coverage.Coverage()
    cov.start()
    try:
        yield
    finally:   # found 2026-10-31: a body that raised left coverage running, and nothing reported
        cov.stop()
        cov.save()
        cov.report(include=include, show_missing=show_missing)


def run_tests(test_file, module_name=None, pytest_args=None, *, coverage: bool = True):
    """Run tests on given file.

    Examples
    --------
    This function is typically included at the bottom of `test_<myfile>.py`.

    .. code-block:: python

        if __name__ == "__main__":
            from pygim.core.testing import run_tests
            run_tests(__file__, MyClass.__module__, coverage=True)

    Notes
    -----
    When coverage is enabled, debugging the actual code is not possible, as the module
    must be reloaded before execution to ensure coverage for the module is captured.
    This can remove breakpoints assigned from the IDE.

    Parameters
    ----------
    test_file : `str` or `pathlib.Path`
        Path to file to be tested.
    module_name : `str`
        Name of the module used for coverage. Usually works by passing object.__class__.__module__.
    pytest_args : `list`, optional
        Any arguments needed to be passed for pytest. (the default is None, which means
        ``[test_file, "--tb=short"]``).
    coverage : `bool`, optional
        Runs the coverage. (the default is True, which runs the coverage).

    Raises
    ------
    ValueError
        When coverage is asked for a module that is not imported, or has no file to measure.
    """
    pytest_args = pytest_args or [str(test_file), "--tb=short"]

    if not coverage or not module_name:
        pytest.main(pytest_args)
        return

    module = sys.modules.get(module_name)
    if module is None:
        raise ValueError(f"{module_name!r} is not imported: coverage measures a module that is")
    if not isinstance(getattr(module, "__file__", None), str):
        raise ValueError(f"{module_name!r} has no file to measure")

    with measure_coverage(include=module.__file__):
        reload(module)  # This is needed to include lines in module level in the report.
        pytest.main(pytest_args)
