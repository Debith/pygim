from __future__ import annotations

import itertools

import pytest

from pygim import utils


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
    "mb": 1024 ** 2,
    "megabyte": 1024 ** 2,
    "megabytes": 1024 ** 2,
    "mib": 1024 ** 2,
    "mebibyte": 1024 ** 2,
    "mebibytes": 1024 ** 2,
    "gb": 1024 ** 3,
    "gigabyte": 1024 ** 3,
    "gigabytes": 1024 ** 3,
    "gib": 1024 ** 3,
    "gibibyte": 1024 ** 3,
    "gibibytes": 1024 ** 3,
    "tb": 1024 ** 4,
    "terabyte": 1024 ** 4,
    "terabytes": 1024 ** 4,
    "pb": 1024 ** 5,
    "petabyte": 1024 ** 5,
    "petabytes": 1024 ** 5,
    "kbit": 1024 / 8,
    "kilobit": 1024 / 8,
    "kilobits": 1024 / 8,
    "mbit": (1024 ** 2) / 8,
    "megabit": (1024 ** 2) / 8,
    "megabits": (1024 ** 2) / 8,
    "gbit": (1024 ** 3) / 8,
    "gigabit": (1024 ** 3) / 8,
    "gigabits": (1024 ** 3) / 8,
    "tbit": (1024 ** 4) / 8,
    "terabit": (1024 ** 4) / 8,
    "terabits": (1024 ** 4) / 8,
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


def _reference_calculate_rate(quantity, quantity_unit, duration, duration_unit, precision=2):
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
def test_calculate_rate_matches_reference(quantity, quantity_unit, duration, duration_unit):
    expected = _reference_calculate_rate(quantity, quantity_unit, duration, duration_unit)
    assert utils.calculate_rate(quantity, quantity_unit, duration, duration_unit) == expected


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


if __name__ == "__main__":
    pytest.main([__file__])