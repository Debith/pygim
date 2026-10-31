# -*- coding: utf-8 -*-
"""benchmarks/results: what a recorded run says about the machine it ran on. Enough to compare runs
(the CPU, the Python, the platform), never which machine it was: Debith, 2026-10-31, "I don't like
the hostname to be there ... even old ones"."""

import importlib.util
import json
import pathlib
import socket

import pytest

from pygim import PathSet, path

BENCHMARKS = pathlib.Path(__file__).resolve().parents[2] / "benchmarks"
pytestmark = pytest.mark.skipif(not (BENCHMARKS / "_results.py").is_file(),
                                reason="benchmarks/ not present (testing the installed package)")


def results_module():
    spec = importlib.util.spec_from_file_location("bench_results_under_test", BENCHMARKS / "_results.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def stored_records():
    files = PathSet(path(str(BENCHMARKS / "results")).glob("*.jsonl"))   # pygim's own walker and reader
    return [(pathlib.Path(str(f)).name, json.loads(line))
            for f, text in zip(files.to_list(), files.read_all_files()) for line in text.splitlines() if line.strip()]


def test_a_recorded_run_does_not_name_its_machine(tmp_path, monkeypatch):
    results = results_module()
    monkeypatch.setattr(results, "RESULTS_DIR", tmp_path)
    written = results.save("probe", {"x": {"seconds": 1.0}}, reps=1)
    line = written.read_text(encoding="utf-8")
    record = json.loads(line)
    assert "hostname" not in record
    assert socket.gethostname() not in line
    assert {"python", "platform", "cpu", "reps"} <= set(record)   # still enough to compare runs


def test_no_stored_run_names_its_machine():
    records = stored_records()
    assert len(records) >= 21                                     # the series are there to check
    assert [f"{name}: {r['utc']}" for name, r in records if "hostname" in r] == []
