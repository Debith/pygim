"""Build and verify the standalone GCC reflection experiment.

Run from the existing dnd Python environment. GCC 16 and its matching gcov build
only a standalone executable; installed extension binaries are never rebuilt.
"""

import argparse
import collections
import copy
import gzip
import hashlib
import inspect
import json
import os
import subprocess
import sys
import time

from verify import (
    audit_binding,
    contract_failures,
    coverage_gaps,
    expected_to_csv,
    required_calls,
)

from pygim import path

GENERATED_TESTS = '''"""Generated replay cases. The small oracle is explicitly human-authored."""
import json
import os
import subprocess

import pytest
from pygim import path, utils

records = json.loads(path(os.path.join(os.path.dirname(__file__), "cases.json")).read_bytes())
cases = records["cases"]


__ORACLE__


@pytest.fixture(scope="session")
def live_native_cases(tmp_path_factory):
    # Execute the generated C++ calls again, once for the whole replay suite.
    # Keep replay counters separate from the original coverage evidence.
    env = dict(os.environ, GCOV_PREFIX=str(tmp_path_factory.mktemp("native-profiles")),
               GCOV_PREFIX_STRIP="100")
    run = subprocess.run([os.path.join(os.path.dirname(__file__), "probe")],
                         capture_output=True, text=True, check=True, env=env, timeout=10)
    live = json.loads(run.stdout)
    assert live["function"] == records["function"]
    assert live["case_count"] == len(cases)
    return {case["id"]: case for case in live["cases"]}


@pytest.mark.parametrize("case", cases, ids=lambda case: case["id"])
def test_native_call_against_contract(case, live_native_cases):
    live = live_native_cases[case["id"]]
    assert live["arguments"] == case["arguments"]
    assert "native_error" not in live
    assert live["native_result"] == expected_to_csv(case["arguments"])


# The separate binding audit records EVERY omitted and keyword rejection.
# Replay only the supported explicit positional interface here; this must not
# be reported as full Python/native call-space parity.
@pytest.mark.parametrize("case", [c for c in cases if not c["omitted"]],
                         ids=lambda case: case["id"])
def test_public_python_call(case):
    values = list(case["arguments"]["strings"])
    before = list(values)
    result = utils.to_csv(values, case["arguments"]["sorted"])
    assert result == expected_to_csv(case["arguments"])
    assert values == before
'''


def write_text(destination, text):
    destination.write_bytes(text.encode("utf-8"))


def write_json(destination, value):
    write_text(destination, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def command(argv, cwd, env=None):
    started = time.perf_counter()
    result = subprocess.run(argv, cwd=cwd, env=env,
                            capture_output=True, text=True, encoding="utf-8", timeout=120, check=False)
    return result, time.perf_counter() - started


def require_success(result, label):
    if result.returncode:
        raise RuntimeError(f"{label} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")


def build(source, destination, includes, compiler, coverage=False):
    argv = [compiler, "-std=c++26", "-freflection", "-O0", "-g", "-Wall", "-Wextra"]
    if coverage:
        argv += ["--coverage", "-fcondition-coverage"]
    argv += [f"-I{p}" for p in includes] + [str(source), "-o", str(destination)]
    result, seconds = command(argv, destination.parent)
    write_text(destination.parent / (destination.name + ".build.log"), result.stdout + result.stderr)
    require_success(result, "compile")
    return {"command": argv, "seconds": seconds}


def collect_coverage(run_dir, gcov_binary, header_suffix, function_fragment):
    notes = [run_dir / name for name in os.listdir(run_dir) if name.endswith(".gcno")]
    if len(notes) != 1:
        raise AssertionError(f"Expected one standalone translation unit, found {notes}")
    gcov, gcov_seconds = command([gcov_binary, "--json-format", "--branch-probabilities",
                                  "--branch-counts", "--conditions", notes[0]], run_dir)
    require_success(gcov, "gcov")
    write_text(run_dir / "gcov.log", gcov.stdout + gcov.stderr)
    coverage_file = path(os.path.splitext(notes[0])[0] + ".gcov.json.gz")
    coverage = json.loads(gzip.decompress(coverage_file.read_bytes()))
    core_coverage = next(f for f in coverage["files"] if f["file"].endswith(header_suffix))
    target_function = next(f for f in core_coverage["functions"] if function_fragment in f["demangled_name"])
    lines = [line for line in core_coverage["lines"] if line.get("function_name") == target_function["name"]]
    branches = [{"line": line["line_number"], **branch} for line in lines for branch in line.get("branches", [])]
    conditions = [{"line": line["line_number"], **condition} for line in lines for condition in line.get("conditions", [])]
    native_coverage = {
        "function": target_function,
        "lines": lines,
        "branches_total": len(branches),
        "branches_taken": sum(b["count"] > 0 for b in branches),
        "uncovered_branches": [b for b in branches if b["count"] == 0],
        "conditions": conditions,
        "condition_outcomes_total": sum(c["count"] for c in conditions),
        "condition_outcomes_covered": sum(c["covered"] for c in conditions),
    }
    write_json(run_dir / "native-coverage.json", native_coverage)

    return native_coverage, gcov_seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default="build/reflection-call-space")
    parser.add_argument("--compiler", default="/usr/bin/g++-16")
    parser.add_argument("--gcov", default="/usr/bin/gcov-16")
    args = parser.parse_args()
    root = path(__file__).parent.parent.parent.parent
    here = path(__file__).parent
    output = path(os.path.abspath(args.output))
    output.mkdir(parents=True, exist_ok=True)
    # A fresh child prevents accumulated .gcda counts from previous runs.
    run_dir = output / ("run-" + str(time.time_ns()))
    run_dir.mkdir()
    includes = [root / "src/_pygim_fast/utils", here]
    source = here / "probe.cpp"
    binary = run_dir / "probe"
    print("Building GCC reflection probe...", flush=True)
    build_record = build(source, binary, includes, args.compiler, coverage=True)
    execution, process_seconds = command([binary], run_dir)
    require_success(execution, "native cases")
    document = json.loads(execution.stdout)
    write_json(run_dir / "cases.json", document)
    gaps = coverage_gaps(document)
    failures = contract_failures(document)
    if gaps or failures:
        raise AssertionError({"coverage_gaps": gaps, "contract_failures": failures})
    print(f"Verified {len(document['cases'])} native cases against the independent contract.", flush=True)

    # Negative control 1: delete a call AND adjust the count. Coverage must
    # notice the missing tuple, not just an incorrect summary count.
    missing = copy.deepcopy(document)
    missing["cases"].pop()
    missing["case_count"] -= 1
    detected_missing = coverage_gaps(missing)
    assert any(g["kind"] == "missing_calls" for g in detected_missing)

    # Negative control 2: a new unsupported parameter must fail compilation.
    unsupported_source = run_dir / "unsupported.cpp"
    write_text(unsupported_source, '#include "call_space.h"\n'
                                 'std::string unsupported(double ratio) { return {}; }\n'
                                 'int main() { call_space::emit<^^unsupported>(std::cout); }\n')
    unsupported, unsupported_seconds = command(
        [args.compiler, "-std=c++26", "-freflection", f"-I{here}", "-fsyntax-only", unsupported_source], run_dir)
    write_text(run_dir / "unsupported.log", unsupported.stdout + unsupported.stderr)
    assert unsupported.returncode != 0 and "Unmodeled reflected parameter" in unsupported.stderr

    # Negative control 3: compile a deliberately wrong COPY of the real header.
    # No production source is changed. The same reflected calls must detect it.
    original_header = includes[0] / "core_utils.h"
    header_text = original_header.read_bytes().decode("utf-8")
    assert header_text.count("if (sorted) {") == 1
    mutant_dir = run_dir / "mutant"
    mutant_dir.mkdir()
    write_text(mutant_dir / "core_utils.h", header_text.replace("if (sorted) {", "if (!sorted) {", 1))
    mutant_binary = mutant_dir / "probe"
    mutant_build = build(source, mutant_binary, [mutant_dir, *includes], args.compiler)
    mutant_execution, mutant_process_seconds = command([mutant_binary], mutant_dir)
    require_success(mutant_execution, "mutated implementation")
    mutant_document = json.loads(mutant_execution.stdout)
    assert not coverage_gaps(mutant_document)
    killed_by = contract_failures(mutant_document)
    assert killed_by, "Contract failed to detect inverted sorting behavior"
    write_json(mutant_dir / "failures.json", killed_by)
    print(f"Negative controls passed; inverted sorting detected by {len(killed_by)} cases.", flush=True)

    native_coverage, gcov_seconds = collect_coverage(
        run_dir, args.gcov, "utils/core_utils.h", "to_csv(")

    audit = audit_binding(document)
    write_json(run_dir / "binding-audit.json", audit)
    binding_counts = collections.Counter((r["form"], r["status"]) for r in audit["attempts"])
    assert not any(r["status"] in {"contract_failure", "unexpected_error"} for r in audit["attempts"])

    # Emit an ordinary, independently rerunnable pytest file alongside its data.
    write_text(run_dir / "test_to_csv_generated.py", GENERATED_TESTS.replace(
        "__ORACLE__", inspect.getsource(expected_to_csv)))
    env = dict(os.environ, PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
    pytest_command = [sys.executable, "-m", "pytest", "-q", "-c", "/dev/null",
                      "--rootdir", str(run_dir), "--junitxml", str(run_dir / "pytest.xml"),
                      str(run_dir / "test_to_csv_generated.py")]
    replay, pytest_seconds = command(pytest_command, run_dir, env)
    write_text(run_dir / "pytest.log", replay.stdout + replay.stderr)
    require_success(replay, "generated pytest replay")

    report = {
        "function": document["function"],
        "compiler": document["compiler"],
        "source_sha256": hashlib.sha256(header_text.encode()).hexdigest(),
        "artifacts": str(run_dir),
        "scope": "Full product within declared value domains; not all possible strings/lists or Python conversions",
        "native_required": len(required_calls(document)),
        "native_verified": len(document["cases"]),
        "coverage_gaps": gaps,
        "negative_controls": {
            "deleted_case": detected_missing,
            "unmodeled_parameter_compile_rejected": True,
            "inverted_sort_mutant_detected_by": len(killed_by),
        },
        "binding_outcomes": [{"form": form, "status": status, "count": count}
                             for (form, status), count in sorted(binding_counts.items())],
        "python_native_call_parity_complete": all(r["status"] == "verified" for r in audit["attempts"]),
        "native_branches": {k: native_coverage[k] for k in (
            "branches_total", "branches_taken", "uncovered_branches", "conditions",
            "condition_outcomes_total", "condition_outcomes_covered")},
        "time_seconds": {"native_generation_execution_serialization": document["native_seconds"],
                         "native_process": process_seconds, "native_compile": build_record["seconds"],
                         "mutant_compile": mutant_build["seconds"], "mutant_process": mutant_process_seconds,
                         "unsupported_compile": unsupported_seconds, "gcov": gcov_seconds,
                         "generated_pytest_process": pytest_seconds},
        "commands": {"compile": build_record["command"], "pytest": pytest_command},
        "generated_pytest_output": replay.stdout,
        "human_supplied": ["selected target ^^to_csv", "string atoms and sequence-length bound",
                           "join/sort contract and its expected default false", "public binding lookup in pygim.utils"],
    }
    write_json(run_dir / "report.json", report)
    write_json(output / "latest.json", {"artifacts": str(run_dir)})
    summary = (
        "GCC reflection call-space prototype: to_csv\n\n"
        f"Native calls verified: {report['native_verified']}/{report['native_required']}\n"
        f"Native branches taken: {native_coverage['branches_taken']}/{native_coverage['branches_total']} (raw gcov)\n"
        f"Condition outcomes: {native_coverage['condition_outcomes_covered']}/{native_coverage['condition_outcomes_total']}\n"
        f"Mutation detected by: {len(killed_by)} generated calls\n"
        f"Missing case and unsupported parameter: both detected\n"
        f"Python/native parity complete: {report['python_native_call_parity_complete']}\n"
        + "".join(f"Python {form}: {count} {status}\n" for (form, status), count in sorted(binding_counts.items()))
        + f"\nNative generation + calls + JSON: {document['native_seconds']:.6f} s\n"
        + f"Native process: {process_seconds:.6f} s; compile: {build_record['seconds']:.3f} s\n"
        + f"Generated pytest process: {pytest_seconds:.3f} s\n\n{replay.stdout}\n"
        + f"Artifacts: {run_dir}\n"
    )
    write_text(run_dir / "report.txt", summary)
    print(summary)


if __name__ == "__main__":
    main()
