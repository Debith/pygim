# -*- coding: utf-8 -*-
"""benchmarks/results: what a recorded run says about the machine it ran on. Enough to compare runs
(the CPU, the Python, the platform), never which machine it was: Debith, 2026-10-31, "I don't like
the hostname to be there ... even old ones"."""

import importlib.util
import json
import pathlib
import re
import socket
import subprocess

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


def strings_in(value, where=""):
    """Every text a record holds, with where it is."""
    if isinstance(value, dict):
        for key, item in value.items():
            yield from strings_in(item, f"{where}.{key}" if where else key)
    elif isinstance(value, list):
        for item in value:
            yield from strings_in(item, f"{where}[]")
    elif isinstance(value, str):
        yield where, value


def is_local_path(text):
    return text.startswith(("/", "~", "\\\\")) or re.match(r"^[A-Za-z]:[\\/]", text) is not None


def test_no_stored_run_holds_a_path_on_its_machine():
    """Found 2026-10-31 (Debith): a corpus was recorded where it lay on the machine — a home folder, a
    session's scratch folder — where its name says which corpus it was."""
    found = [f"{name}: {where} = {text}" for name, record in stored_records()
             for where, text in strings_in(record) if is_local_path(text)]
    assert found == []


def test_a_run_that_holds_a_path_on_its_machine_is_refused(tmp_path, monkeypatch):
    results = results_module()
    monkeypatch.setattr(results, "RESULTS_DIR", tmp_path)
    with pytest.raises(ValueError, match=r"sections\.corpus\.root holds /home/someone/cpython, a path on this machine"):
        results.save("probe", {"corpus": {"root": "/home/someone/cpython"}}, reps=1)
    assert not (tmp_path / "probe.jsonl").exists()                # nothing written


def test_a_corpus_is_named_by_its_checkout_and_below(tmp_path):
    results = results_module()
    checkout = tmp_path / "cpython"
    (checkout / "Lib").mkdir(parents=True)
    subprocess.run(["git", "init", "-q", str(checkout)], check=True)
    (tmp_path / "loose").mkdir()
    assert results.corpus(checkout / "Lib") == "cpython/Lib"
    assert results.corpus(checkout) == "cpython"
    assert results.corpus(str(tmp_path / "loose")) == "loose"   # outside a checkout: its own name
