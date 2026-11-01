from __future__ import annotations

import ast
import itertools
import os
import platform
import sys
import time

import pytest

from pygim import utils
from pygim.pathlike import path
from stub_parity import Stub


DATA_UNIT_FACTORS = {
    "b": 1,
    "byte": 1,
    "bytes": 1,
    "bit": 1 / 8,
    "bits": 1 / 8,
    "kb": 1024,
    "kilobyte": 1024,
    "kilobytes": 1024,
    "kib": 1024,
    "kibibyte": 1024,
    "kibibytes": 1024,
    "mb": 1024**2,
    "megabyte": 1024**2,
    "megabytes": 1024**2,
    "mib": 1024**2,
    "mebibyte": 1024**2,
    "mebibytes": 1024**2,
    "gb": 1024**3,
    "gigabyte": 1024**3,
    "gigabytes": 1024**3,
    "gib": 1024**3,
    "gibibyte": 1024**3,
    "gibibytes": 1024**3,
    "tb": 1024**4,
    "terabyte": 1024**4,
    "terabytes": 1024**4,
    "pb": 1024**5,
    "petabyte": 1024**5,
    "petabytes": 1024**5,
    "kbit": 1024 / 8,
    "kilobit": 1024 / 8,
    "kilobits": 1024 / 8,
    "mbit": (1024**2) / 8,
    "megabit": (1024**2) / 8,
    "megabits": (1024**2) / 8,
    "gbit": (1024**3) / 8,
    "gigabit": (1024**3) / 8,
    "gigabits": (1024**3) / 8,
    "tbit": (1024**4) / 8,
    "terabit": (1024**4) / 8,
    "terabits": (1024**4) / 8,
}

TIME_UNIT_FACTORS = {
    "s": 1,
    "sec": 1,
    "secs": 1,
    "second": 1,
    "seconds": 1,
    "ms": 1 / 1_000,
    "millisecond": 1 / 1_000,
    "milliseconds": 1 / 1_000,
    "us": 1 / 1_000_000,
    "microsecond": 1 / 1_000_000,
    "microseconds": 1 / 1_000_000,
    "ns": 1 / 1_000_000_000,
    "nanosecond": 1 / 1_000_000_000,
    "nanoseconds": 1 / 1_000_000_000,
    "m": 60,
    "min": 60,
    "mins": 60,
    "minute": 60,
    "minutes": 60,
    "h": 3_600,
    "hr": 3_600,
    "hrs": 3_600,
    "hour": 3_600,
    "hours": 3_600,
}


def _reference_calculate_rate(
    quantity, quantity_unit, duration, duration_unit, precision=2
):
    q_factor = DATA_UNIT_FACTORS[quantity_unit.lower()]
    t_factor = TIME_UNIT_FACTORS[duration_unit.lower()]
    bytes_total = quantity * q_factor
    seconds_total = duration * t_factor
    rate = bytes_total / seconds_total
    units = ["B/s", "KB/s", "MB/s", "GB/s", "TB/s", "PB/s"]
    idx = 0
    while abs(rate) >= 1024 and idx < len(units) - 1:
        rate /= 1024
        idx += 1
    return f"{rate:.{precision}f} {units[idx]}"


QUANTITY_CASES = [
    (1, "bytes"),
    (64, "bytes"),
    (4096, "bytes"),
    (0.5, "kilobytes"),
    (12, "kilobytes"),
    (2.5, "megabytes"),
    (1, "gigabyte"),
    (12.5, "megabits"),
    (900, "kilobits"),
    (3, "gigabits"),
]

TIME_CASES = [
    (1, "seconds"),
    (0.5, "seconds"),
    (2, "seconds"),
    (500, "milliseconds"),
    (250, "microseconds"),
    (50_000, "nanoseconds"),
    (3, "minutes"),
    (0.25, "hours"),
]

RATE_CASES = [
    (q, qunit, d, dunit)
    for (q, qunit), (d, dunit) in itertools.product(QUANTITY_CASES, TIME_CASES)
]


@pytest.mark.parametrize("quantity, quantity_unit, duration, duration_unit", RATE_CASES)
def test_calculate_rate_matches_reference(
    quantity, quantity_unit, duration, duration_unit
):
    expected = _reference_calculate_rate(
        quantity, quantity_unit, duration, duration_unit
    )
    assert (
        utils.calculate_rate(quantity, quantity_unit, duration, duration_unit)
        == expected
    )


def test_calculate_rate_supports_precision_override():
    result = utils.calculate_rate(10 * 1024 * 1024, "bytes", 3, "seconds", precision=1)
    assert result == "3.3 MB/s"


@pytest.mark.parametrize(
    "quantity, quantity_unit, duration, duration_unit",
    [
        (1, "lightyears", 1, "seconds"),
        (1, "bytes", 1, "fortnights"),
    ],
)
def test_calculate_rate_invalid_units(quantity, quantity_unit, duration, duration_unit):
    with pytest.raises(ValueError):
        utils.calculate_rate(quantity, quantity_unit, duration, duration_unit)


def test_calculate_rate_rejects_non_positive_duration():
    with pytest.raises(ValueError):
        utils.calculate_rate(1, "bytes", 0, "seconds")


# ── process memory probes (utils/memory.h): the delta test lives in test_utils_memory_probe.py ──
def test_rss_probe_is_cheap():
    n = 20_000
    t0 = time.perf_counter()
    for _ in range(n):
        utils.rss_bytes()
    per_call = (time.perf_counter() - t0) / n
    assert per_call < 20e-6, per_call                              # ~1 us on Linux (a procfs pread); generous for CI


# ── the processor (utils/cpu.h) ──
SYS_CACHES = path("/sys/devices/system/cpu/cpu0/cache")


def test_cpu_reports_caches_never_zero():
    c = utils.cpu()
    assert set(c) == {"l1d", "l2", "l3", "line", "avx2"}
    assert 0 < c["l1d"] <= c["l2"] <= c["l3"] and c["line"] in (32, 64, 128, 256)


@pytest.mark.skipif(not hasattr(os, "sysconf") or "SC_LEVEL2_CACHE_SIZE" not in os.sysconf_names
                    or os.sysconf("SC_LEVEL2_CACHE_SIZE") <= 0, reason="the system reports no L2 here")
def test_cpu_caches_are_the_systems():
    c = utils.cpu()
    assert (c["l1d"], c["l2"]) == (os.sysconf("SC_LEVEL1_DCACHE_SIZE"), os.sysconf("SC_LEVEL2_CACHE_SIZE"))


def _listed_data_caches():
    """{level: bytes} of the largest cache of each level that holds data, as the kernel lists cpu0's."""
    read = lambda index, what: index.joinpath(what).read_bytes().decode().strip()
    found = {}
    for index in SYS_CACHES.pathset("index*", kind="dir"):
        size = read(index, "size")
        bytes_ = int(size[:-1]) * {"K": 1024, "M": 1 << 20}[size[-1]] if size[-1] in "KM" else int(size)
        if read(index, "type") != "Instruction":
            found[int(read(index, "level"))] = max(found.get(int(read(index, "level")), 0), bytes_)
    return found


@pytest.mark.skipif(not SYS_CACHES.exists(), reason="the kernel lists no caches here")
def test_cpu_caches_are_the_kernels_data_caches():
    """The L1d and L2 are the largest caches of their level the kernel lists as holding data — type
    "Data" or "Unified", never "Instruction" — whichever source pygim read: sysconf, or that list
    where sysconf says nothing (glibc on arm64, musl). Found 2026-10-29: the list's reader took the
    type from the size's first byte, so a larger L1 instruction cache stood as the L1d."""
    listed = _listed_data_caches()
    c = utils.cpu()
    assert (c["l1d"], c["l2"]) == (listed[1], listed[2])


@pytest.mark.skipif(sys.platform != "linux" or platform.machine() not in ("x86_64", "AMD64"), reason="reads /proc/cpuinfo's x86 flags")
def test_cpu_avx2_is_the_processors():
    flags = next(line for line in path("/proc/cpuinfo").read_bytes().decode().splitlines() if line.startswith("flags")).split()
    assert utils.cpu()["avx2"] == ("avx2" in flags)


if __name__ == "__main__":
    pytest.main([__file__])


@pytest.mark.parametrize("name, dunder", [("__init__", True), ("__x__", True), ("____", True), ("__", False), ("_x_", False), ("__x", False)])
def test_is_dunder(name, dunder):
    assert utils.is_dunder(name) is dunder


def test_to_csv_joins_and_may_sort():
    assert utils.to_csv(["b", "a"]) == "b, a"
    assert utils.to_csv(["b", "a"], sorted=True) == "a, b"
    assert utils.to_csv([]) == ""


def test_a_unit_is_read_without_the_locale():
    """'MiB' and 'MIB' are one unit; a unit with a byte past ASCII (µs) is read as written."""
    assert utils.calculate_rate(1, "MIB", 1, "S") == utils.calculate_rate(1, "mib", 1, "s") == "1.00 MB/s"
    assert utils.calculate_rate(1, "bytes", 1, "µs") == "976.56 KB/s"


# ── the stub ────────────────────────────────────────────────────────────────
UTILS_CLASSES = ("Duration", "ManualTime", "Timestamp", "TimerSnapshot", "TimerLap", "Report", "QuickTimer", "gimdict")
# What pybind11 gives every class, or what the stub leaves to object's.
UTILS_NOT_STUBBED = {"__module__", "__doc__", "__dict__", "__weakref__", "__new__", "__repr__", "_pybind11_conduit_v1_"}


class TestStub:
    """src/pygim/utils.pyi is what an editor and a type checker see of the module (stub_parity)."""

    STUB = Stub(utils)

    def test_it_names_every_class_function_and_constant_of_the_module(self):
        assert {n for n in vars(utils) if not n.startswith("_")} <= self.STUB.names()

    @pytest.mark.parametrize("cls", UTILS_CLASSES)
    def test_every_member_is_stubbed(self, cls):
        bound = set(vars(getattr(utils, cls))) - UTILS_NOT_STUBBED
        if "__init__" in bound and "Initialize self" in (vars(getattr(utils, cls))["__init__"].__doc__ or ""):
            bound.discard("__init__")   # no constructor of its own: made by a static method
        assert sorted(bound - set(self.STUB.methods(cls))) == []

    @pytest.mark.parametrize("cls, name", [(c, n) for c in UTILS_CLASSES for n in vars(getattr(utils, c)) if n not in UTILS_NOT_STUBBED])
    def test_each_documented_member_has_the_bindings_docstring(self, cls, name):
        bound = vars(getattr(utils, cls))[name]
        if bound is None:   # __hash__ = None: unhashable, nothing to document
            return
        raw = bound.__func__ if isinstance(bound, staticmethod) else bound
        text = Stub.binding_doc(raw)
        if not text or "Initialize self" in text:
            return
        assert Stub.same_text(ast.get_docstring(self.STUB.methods(cls)[name]), text), text

    @pytest.mark.parametrize("name", sorted(n for n in vars(utils) if not n.startswith("_") and callable(getattr(utils, n)) and not isinstance(getattr(utils, n), type)))
    def test_each_function_takes_the_bindings_parameters_and_docstring(self, name):
        stubbed, bound = self.STUB.function(name), getattr(utils, name)
        assert Stub.params(stubbed, method=False) == Stub.binding_params(bound)
        assert Stub.same_text(ast.get_docstring(stubbed), Stub.binding_doc(bound)), Stub.binding_doc(bound)

    @pytest.mark.parametrize("cls", UTILS_CLASSES)
    def test_each_class_docstring_is_the_bindings(self, cls):
        assert Stub.same_text(ast.get_docstring(self.STUB.cls(cls)), getattr(utils, cls).__doc__)
