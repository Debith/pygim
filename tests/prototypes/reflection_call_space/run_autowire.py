"""Reflect and verify the real IoC autowiring policy, without rebuilding pygim."""
import argparse
import collections
import copy
import hashlib
import json
import os
import sys
import time

from behavior_autowire import (
    analyze_case,
    require_valid_report,
    write_behavior_report,
    write_conflict_control,
)
from run import (
    build,
    collect_coverage,
    command,
    require_success,
    write_json,
    write_text,
)
from static_autowire import run_static_assertions
from verify import case_key, coverage_gaps, required_calls
from verify_autowire import audit_public, contract_failures

from pygim import path

GENERATED_TESTS = '''"""Generated batched replay: every row is checked, with its case ID on failure."""
import collections
import json
import os
import subprocess

import pytest
from behavior_autowire import analyze_behaviors, require_valid_report
from pygim import path
from verify import coverage_gaps
from verify_autowire import actual_outcome, audit_public, expected_autowiring

HERE = os.path.dirname(__file__)
recorded = json.loads(path(os.path.join(HERE, "cases.json")).read_bytes())
saved = {case["id"]: case for case in recorded["cases"]}


@pytest.fixture(scope="session")
def live(tmp_path_factory):
    env = dict(os.environ, GCOV_PREFIX=str(tmp_path_factory.mktemp("profiles")),
               GCOV_PREFIX_STRIP="100")
    result = subprocess.run([os.path.join(HERE, "probe")], capture_output=True,
                            text=True, check=True, env=env, timeout=30)
    return json.loads(result.stdout)


def test_complete_reflected_call_space(live):
    assert live["function"] == recorded["function"]
    assert not coverage_gaps(live)
    assert live["case_count"] == recorded["case_count"]


@pytest.mark.parametrize("outcome", ["success", "runtime_error", "missing_key", "lookup_failure"])
def test_native_contract_group(live, outcome):
    checked = 0
    for case in live["cases"]:
        expected = expected_autowiring(case["arguments"])
        if expected.get("native_error_type", "success") != outcome:
            continue
        assert case["arguments"] == saved[case["id"]]["arguments"], case["id"]
        assert actual_outcome(case) == expected, case["id"]
        checked += 1
    assert checked, outcome


def test_public_container_autowiring(live):
    rows = audit_public(live)
    assert not [row for row in rows if row["status"] == "contract_failure"]
    before = json.loads(path(os.path.join(HERE, "binding-audit.json")).read_bytes())
    assert collections.Counter(row["status"] for row in rows) == collections.Counter(
        row["status"] for row in before)


def test_behavior_rules_and_overlaps(live):
    rules = json.loads(path(os.path.join(HERE, "autowire_behaviors.json")).read_bytes())
    report = analyze_behaviors(live, rules)
    require_valid_report(report)
    before = json.loads(path(os.path.join(HERE, "behaviors/report.json")).read_bytes())
    for key in ("rules", "overlaps", "verified_cases", "rules_sha256"):
        assert report[key] == before[key], key
'''


def mutant_run(name, header_text, source, includes, run_dir, compiler):
    destination = run_dir / name
    (destination / "wiring/ioc").mkdir(parents=True)
    (destination / "wiring/common").mkdir()
    write_text(destination / "wiring/ioc/core.h", header_text)
    (destination / "wiring/common/errors.h").write_bytes(
        (includes[0] / "wiring/common/errors.h").read_bytes())
    compilation = build(source, destination / "probe", [destination, *includes], compiler)
    execution, seconds = command([destination / "probe"], destination)
    require_success(execution, name)
    return json.loads(execution.stdout), {"compile": compilation["seconds"], "process": seconds}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default="build/reflection-call-space-autowire")
    parser.add_argument("--compiler", default="/usr/bin/g++-16")
    parser.add_argument("--gcov", default="/usr/bin/gcov-16")
    parser.add_argument("--static-shard-size", type=int, default=1024)
    args = parser.parse_args()
    if args.static_shard_size < 1:
        parser.error("--static-shard-size must be positive")
    here = path(__file__).parent
    root = here.parent.parent.parent
    output = path(os.path.abspath(args.output))
    output.mkdir(parents=True, exist_ok=True)
    run_dir = output / ("run-" + str(time.time_ns()))
    run_dir.mkdir()
    includes = [root / "src/_pygim_fast", here]
    source = here / "autowire_probe.cpp"
    header = includes[0] / "wiring/ioc/core.h"
    header_text = header.read_bytes().decode("utf-8")
    print("Building reflected IoC autowiring probe...", flush=True)
    compilation = build(source, run_dir / "probe", includes, args.compiler, coverage=True)
    execution, native_seconds = command([run_dir / "probe"], run_dir)
    require_success(execution, "native call space")
    document = json.loads(execution.stdout)
    write_json(run_dir / "cases.json", document)
    gaps, failures = coverage_gaps(document), contract_failures(document)
    write_json(run_dir / "verification.json", {"gaps": gaps, "failures": failures})
    assert not gaps and not failures, f"See {run_dir / 'verification.json'}"
    outcomes = collections.Counter(case.get("native_error_type", "success") for case in document["cases"])
    print(f"Verified {document['case_count']} calls, including outcomes {dict(outcomes)}.", flush=True)

    rules = json.loads((here / "autowire_behaviors.json").read_bytes())
    behavior_report = write_behavior_report(document, rules, run_dir / "behaviors")
    require_valid_report(behavior_report)
    behavior_control = write_conflict_control(document, rules, run_dir / "behaviors/contradiction-control")
    print(f"Behavior rules: {len(behavior_report['rules'])} exercised; overlap types "
          f"{behavior_report['overlap_types_by_status']}; no contradictions.", flush=True)

    # A missing row must fail even after the declared count is adjusted.
    missing = copy.deepcopy(document)
    missing["cases"].pop()
    missing["case_count"] -= 1
    deleted_case = coverage_gaps(missing)
    assert any(gap["kind"] == "missing_calls" for gap in deleted_case)

    # Shrink BOTH the advertised vector domain and its calls. The independent
    # recursive field/sequence model must still reject this self-consistent lie.
    shrunken = copy.deepcopy(document)
    removed = shrunken["function"]["parameters"][0]["domain"]["values"].pop()
    shrunken["cases"] = [case for case in shrunken["cases"]
                        if case_key(case["arguments"]["params"]) != case_key(removed)]
    shrunken["case_count"] = len(shrunken["cases"])
    shrunk_domain = coverage_gaps(shrunken)
    assert any(gap["kind"] == "incomplete_structured_domain" for gap in shrunk_domain)

    mutations = {
        "skip_registered_defaults": (
            "        if (!contains(*param.annotation)) {",
            "        if (param.has_default) continue;\n        if (!contains(*param.annotation)) {"),
        "reverse_injection_order": (
            "        injections.push_back({param.name, *param.annotation});",
            "        injections.insert(injections.begin(), {param.name, *param.annotation});"),
        "wrong_missing_exception": (
            '            throw missing_key(\n                "No provider registered',
            '            throw std::runtime_error(\n                "No provider registered'),
    }
    killed, mutation_times = {}, {}
    for name, (before, after) in mutations.items():
        assert header_text.count(before) == 1, f"Mutation location changed: {name}"
        mutant, timings = mutant_run(name, header_text.replace(before, after, 1),
                                    source, includes, run_dir, args.compiler)
        assert not coverage_gaps(mutant)
        detected = contract_failures(mutant)
        assert detected, f"Surviving mutant: {name}"
        killed[name] = len(detected)
        mutation_times[name] = timings
        write_json(run_dir / name / "failures.json", detected)
        # The data rules must detect a real native policy defect independently.
        witness = next(case for case in mutant["cases"] if case["id"] == detected[0]["id"])
        behavior_failure = analyze_case(witness, rules)
        assert behavior_failure["native_failure"] and not behavior_failure["contract_disagreement"]
        write_json(run_dir / name / "behavior-failure.json", behavior_failure)
        print(f"Mutation {name}: detected by {len(detected)} calls.", flush=True)

    # An enum declaration grows the generated product automatically; the human
    # contract must refuse the unknown semantics rather than silently accepting.
    enum_before = "    Variadic = 3,"
    assert header_text.count(enum_before) == 1
    enlarged, timings = mutant_run(
        "added_parameter_kind", header_text.replace(enum_before, "    NewKind = 4,\n" + enum_before, 1),
        source, includes, run_dir, args.compiler)
    assert not coverage_gaps(enlarged)
    assert enlarged["case_count"] > document["case_count"]
    try:
        contract_failures(enlarged)
    except ValueError as error:
        enum_drift_error = str(error)
    else:
        raise AssertionError("New enum semantics escaped the contract review gate")
    write_json(run_dir / "added_parameter_kind/drift.json",
               {"case_count": enlarged["case_count"], "contract_rejection": enum_drift_error})
    mutation_times["added_parameter_kind"] = timings

    unsupported_source = run_dir / "unsupported_field.cpp"
    write_text(unsupported_source, '#include "call_space.h"\n'
               'struct NewShape { double ratio; };\n'
               'template <> inline constexpr bool call_space::reflected_record<NewShape> = true;\n'
               'bool updated(const NewShape& value) { return true; }\n'
               'int main() { call_space::emit<^^updated>(std::cout); }\n')
    supported_source = run_dir / "supported_field.cpp"
    write_text(supported_source, unsupported_source.read_bytes().decode("utf-8").replace("double ratio", "bool ratio"))
    supported, _ = command([args.compiler, "-std=c++26", "-freflection", f"-I{here}",
                           "-fsyntax-only", supported_source], run_dir)
    require_success(supported, "modeled Boolean field control")
    unsupported, _ = command([args.compiler, "-std=c++26", "-freflection", f"-I{here}",
                               "-fsyntax-only", unsupported_source], run_dir)
    write_text(run_dir / "unsupported_field.log", unsupported.stdout + unsupported.stderr)
    assert unsupported.returncode and "Unmodeled reflected field" in unsupported.stderr

    static_report = run_static_assertions(
        document, run_dir, includes, args.compiler, args.static_shard_size)

    coverage, gcov_seconds = collect_coverage(
        run_dir, args.gcov, "wiring/ioc/core.h", "plan_autowiring<int, Lookup const&>")
    print("Auditing real public Container calls...", flush=True)
    started = time.perf_counter()
    audit = audit_public(document)
    audit_seconds = time.perf_counter() - started
    write_json(run_dir / "binding-audit.json", audit)
    public_counts = collections.Counter(row["status"] for row in audit)
    assert not public_counts["contract_failure"], f"See {run_dir / 'binding-audit.json'}"

    write_text(run_dir / "test_autowire_generated.py", GENERATED_TESTS)
    for name in ("verify.py", "verify_autowire.py", "behavior_autowire.py", "autowire_behaviors.json"):
        (run_dir / name).write_bytes((here / name).read_bytes())
    (run_dir / "test_behavior_checker.py").write_bytes((here / "check_autowire_behaviors.py").read_bytes())
    pytest_command = [sys.executable, "-m", "pytest", "-q", "-c", "/dev/null",
                      "--rootdir", str(run_dir), "--junitxml", str(run_dir / "pytest.xml"),
                      str(run_dir / "test_autowire_generated.py"), str(run_dir / "test_behavior_checker.py")]
    replay, replay_seconds = command(pytest_command, run_dir,
                                     dict(os.environ, PYTEST_DISABLE_PLUGIN_AUTOLOAD="1"))
    write_text(run_dir / "pytest.log", replay.stdout + replay.stderr)
    require_success(replay, "generated replay")

    report = {
        "function": document["function"], "compiler": document["compiler"],
        "source_sha256": hashlib.sha256(header_text.encode()).hexdigest(),
        "artifacts": str(run_dir),
        "native_required": len(required_calls(document)), "native_verified": document["case_count"],
        "native_outcomes": dict(outcomes), "public_outcomes": dict(public_counts),
        "static_assertions": static_report,
        "behaviors": {"rules": len(behavior_report["rules"]),
                      "verified_cases": behavior_report["verified_cases"],
                      "contradictions": len(behavior_report["contradictions"]),
                      "overlap_types_by_status": behavior_report["overlap_types_by_status"],
                      "report": str(run_dir / "behaviors/report.json")},
        "python_native_call_parity_complete": False,
        "native_coverage": coverage,
        "negative_controls": {"deleted_case": deleted_case, "shrunken_domain": shrunk_domain,
                              "mutations_detected_by": killed,
                              "added_enum_cases": enlarged["case_count"],
                              "added_enum_contract_rejection": enum_drift_error,
                              "unsupported_field_compile_rejected": True},
        "time_seconds": {"native_compile": compilation["seconds"],
                         "native_generation_execution_serialization": document["native_seconds"],
                         "native_process": native_seconds, "public_audit": audit_seconds,
                         "gcov": gcov_seconds, "generated_pytest_process": replay_seconds,
                         "behavior_analysis": behavior_report["analysis_seconds"],
                         "mutation_runs": mutation_times},
        "commands": {"compile": compilation["command"], "pytest": pytest_command},
        "scope": "Full finite product: lengths 0..2, names first/second, keys 0/1, named enums, lookup faults",
        "human_supplied": ["concrete template instantiation", "record opt-ins",
                           "name/key domains and vector length bound", "lookup implementation and fault schedules",
                           "oracle, exception classification and public constructor adapter",
                           "behavior guards, obligations and ordered decision stages"],
    }
    report["negative_controls"]["behavior_contradictions"] = behavior_control
    write_json(run_dir / "report.json", report)
    write_json(output / "latest.json", {"artifacts": str(run_dir)})
    summary = (
        "GCC reflection call-space prototype: IoC plan_autowiring\n\n"
        f"Native calls verified: {report['native_verified']}/{report['native_required']}\n"
        f"Behavior rules exercised: {len(behavior_report['rules'])}; contradictions: 0\n"
        f"Overlap types by status: {behavior_report['overlap_types_by_status']}\n"
        f"Behavior report and case witnesses: {run_dir / 'behaviors'}\n"
        f"Generated static_assert cases: {static_report['assertion_count']}; all compiled successfully\n"
        f"Static compilation: {static_report['compile_seconds']:.3f}s in {static_report['shards']} shards\n"
        "Static negative controls: three policy mutants and enum drift rejected by assertions\n"
        f"Native outcomes: {dict(outcomes)}\n"
        f"Condition outcomes: {coverage['condition_outcomes_covered']}/{coverage['condition_outcomes_total']}\n"
        f"Raw native branches: {coverage['branches_taken']}/{coverage['branches_total']}\n"
        f"Mutations detected by: {killed}\n"
        f"Added enum expands to {enlarged['case_count']} calls; contract review required\n"
        f"Public Python outcomes: {dict(public_counts)}\n"
        "Python/native parity: partial; callback faults and invalid Python signatures explicitly classified\n\n"
        f"Native execution/JSON: {document['native_seconds']:.6f}s; compile: {compilation['seconds']:.3f}s\n"
        f"Public audit: {audit_seconds:.3f}s; generated pytest process: {replay_seconds:.3f}s\n\n"
        f"{replay.stdout}\nArtifacts: {run_dir}\n"
    )
    write_text(run_dir / "report.txt", summary)
    print(summary)


if __name__ == "__main__":
    main()
