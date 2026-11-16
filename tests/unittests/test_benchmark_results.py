# -*- coding: utf-8 -*-
"""benchmarks/_results.py: what a recorded run says about the machine it ran on. Enough to compare
runs (the Python, the OS and the architecture), never which machine it was. Debith, PR #38 review,
2026-10-18: "Bit too detailed information to store here. Platform, cpu, hostname"; and 2026-10-31:
"I don't like the hostname to be there ... even old ones"."""

import importlib.util
import json
import pathlib
import platform
import socket

import pytest

from pygim import path

BENCHMARKS = pathlib.Path(__file__).resolve().parents[2] / "benchmarks"
pytestmark = pytest.mark.skipif(not (BENCHMARKS / "_results.py").is_file(),
                                reason="benchmarks/ not present (testing the installed package)")


def stored_records():
    """Every record under benchmarks/results, with the name of its file: read by pygim's own reader."""
    files = path(BENCHMARKS / "results").pathset("*.jsonl")
    return [(f.name, json.loads(line))
            for f, text in zip(files, files.read_all_files()) for line in text.splitlines() if line.strip()]


@pytest.fixture
def results(tmp_path, monkeypatch):
    """benchmarks/_results.py, loaded by its path (benchmarks/ is not a package), writing into tmp_path."""
    spec = importlib.util.spec_from_file_location("bench_results_under_test", BENCHMARKS / "_results.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module, "RESULTS_DIR", tmp_path)
    return module


def test_a_recorded_run_does_not_name_its_machine(results):
    line = results.save("probe", {"x": {"seconds": 1.0}}, reps=1).read_text(encoding="utf-8")
    record = json.loads(line)
    assert "hostname" not in record and "cpu" not in record
    assert socket.gethostname() not in line


def test_a_recorded_run_names_its_platform_by_system_and_architecture_only(results):
    record = json.loads(results.save("probe", {"x": {"seconds": 1.0}}, reps=1).read_text(encoding="utf-8"))
    assert record["platform"] == f"{platform.system()}-{platform.machine()}"


def test_no_stored_run_names_its_machine():
    records = stored_records()
    assert len(records) >= 6  # the series are there to check
    assert [f"{name}: {record['utc']}" for name, record in records if {"hostname", "cpu"} & record.keys()] == []
