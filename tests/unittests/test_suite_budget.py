# -*- coding: utf-8 -*-
"""No test takes what the whole suite is allowed, half a second (tests/conftest.py): Debith,
2026-10-30, asked whether the suite should run on eight processes, "there is no reason tests take
longer than 500ms ... whole suite, actually"."""

import pathlib

import pytest

pytest_plugins = ["pytester"]

CONFTEST = pathlib.Path(__file__).resolve().parents[1] / "conftest.py"


@pytest.fixture
def suite(pytester, monkeypatch):
    """A run of three tests under the suite's own conftest, its budget scaled to 10 ms."""
    pytester.makeconftest(CONFTEST.read_text(encoding="utf-8"))
    pytester.makepyfile(test_inner="""
import time
import pytest

def test_quick():
    pass

def test_slow():
    time.sleep(0.012)

@pytest.mark.outside_service
def test_slow_on_a_service():
    time.sleep(0.012)

def test_failing_and_slow():
    time.sleep(0.012)
    assert False
""")
    monkeypatch.setenv("PYGIM_TEST_TIME_SCALE", "0.02")
    return pytester


def test_a_test_over_its_budget_fails_naming_its_time(suite):
    ran = suite.runpytest("-p", "no:cacheprovider")
    ran.assert_outcomes(passed=3, failed=1, errors=1)
    ran.stdout.fnmatch_lines(["*test_slow *took * ms, over the 10 ms a test may take*"])


def test_a_scale_of_naught_turns_the_budget_off(suite, monkeypatch):
    """A sanitizer's run turns it off: an instrumented test's clock measures the instrumentation."""
    monkeypatch.setenv("PYGIM_TEST_TIME_SCALE", "0")
    suite.runpytest("-p", "no:cacheprovider").assert_outcomes(passed=3, failed=1)
