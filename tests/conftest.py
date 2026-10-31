# Preload Arrow's shared libraries before any test module is collected.  The
# Arrow-linked extensions (_persistence, _fetch_benchmark, datagen) resolve
# libarrow via ``$ORIGIN/../pyarrow``, which only exists for a
# wheel installed into site-packages.  In an editable checkout the loader finds
# libarrow only if pyarrow has already been imported into the process, so make
# that true regardless of which test module imports an extension first.
import pyarrow
import pyarrow.parquet  # noqa: F401

collect_ignore_glob = ["**/mssql_example.py", "**/mssql_write_example.py"]

import os

import pytest

# The whole suite runs in half a second (Debith, 2026-10-30: "there is no reason tests take longer
# than 500ms ... whole suite, actually"; docs/definition_of_done.md). It does not yet, so this is the
# layer under it: no test may take what the whole suite is allowed, its fixtures' setup and teardown
# with it. Over it, the test fails at its teardown, naming its time. PYGIM_TEST_TIME_SCALE stretches the budget where the clock says more
# about the machine than the test — 0 turns it off: a sanitizer's run, and CI until a run there has
# measured its runners. A test that waits on a service outside the project (a live database's first
# login took 1.1 s) is marked `outside_service`: that time is not the suite's to cut.
BUDGET_SECONDS = 0.5
TOOK = pytest.StashKey[float]()
FAILED = pytest.StashKey[bool]()   # a test that failed already is not failed again for its time


def pytest_configure(config):
    config.addinivalue_line("markers", "outside_service: waits on a service outside the project; the time budget does not apply")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    report = (yield).get_result()
    took = item.stash[TOOK] = item.stash.get(TOOK, 0.0) + report.duration
    failed = item.stash[FAILED] = item.stash.get(FAILED, False) or report.failed
    budget = BUDGET_SECONDS * float(os.environ.get("PYGIM_TEST_TIME_SCALE", "1"))
    if report.when == "teardown" and not failed and budget and took > budget and not item.get_closest_marker("outside_service"):
        report.outcome = "failed"
        report.longrepr = f"{item.nodeid} took {took * 1e3:.0f} ms, over the {budget * 1e3:.0f} ms a test may take"
