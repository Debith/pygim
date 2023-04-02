# -*- coding: utf-8 -*-
"""
This module contains script to run coverage for specific module.
"""

import sys
from importlib import reload
from contextlib import contextmanager
import coverage
import pytest

__all__ = ["measure_coverage", "run_tests"]


@contextmanager
def measure_coverage(*, include=None, show_missing: bool = True):
    """Run code coverage for the code executed in this context manager.

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


def run_tests(test_file, module_name, pytest_args=None, *, coverage: bool = True):
    """Runs tests for the file."""
    module = sys.modules[module_name]
    assert isinstance(module.__file__, str)

    pytest_args = [str(test_file), "--tb=short"]

    if not coverage:
        pytest.main(pytest_args)
        return

    assert module, "When running the coverage, module must be specified!"

    with measure_coverage(include=module.__file__):
        reload(module)  # This is needed to include lines in module level in the report.
        pytest.main(pytest_args)
