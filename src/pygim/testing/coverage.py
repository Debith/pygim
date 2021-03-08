"""
This module contains script to run coverage for specific module.
"""

from pathlib import Path
from importlib import reload
from contextlib import contextmanager
import coverage
import pytest

__all__ = ['measure_coverage', 'run_tests']


@contextmanager
def measure_coverage(*, include=None, show_missing: bool = True):
    """ Run code coverage for the code executed in this context manager.

    Parameters:
        include: File to be included in the coverage report. If None, all shown.
        show_missing: True, if coverage report should include lines that were not run.
    """
    cov = coverage.Coverage()
    cov.start()

    yield

    cov.stop()
    cov.save()
    cov.report(include=include, show_missing=show_missing)


def run_tests(test_file, module=None, include=None, pytest_args=None, *, coverage=True):
    """ Runs tests for the file. """
    assert Path(test_file).is_file(), f'The file "{test_file}" is not actually a file!'

    if not coverage:
        pytest.main([test_file, "--tb=short"])
        return

    assert module, "When running the coverage, module must be specified!"

    with measure_coverage(include=include or module.__file__):
        reload(module)  # This is needed to include lines in module level in the report.
        if pytest_args is None:
            pytest_args = []

        pytest_args = [test_file, "--tb=short"] + pytest_args
        pytest.main(pytest_args)
