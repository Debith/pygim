"""Interpret data rules and account for overlaps in the finite autowiring model.

Rules at one parameter/stage are simultaneous obligations, independent of array
order. The original oracle remains an independent check on this specification.
This is behavioral evidence, not C++ control-flow instrumentation.
"""
import argparse
import collections
import copy
import hashlib
import itertools
import json
import string
import time

from verify import coverage_gaps
from verify_autowire import (
    FAILURE_CALLS,
    PROVIDERS,
    actual_outcome,
    expected_autowiring,
)

from pygim import path

FACTS = {"variadic", "positional_only", "annotated", "has_default", "registered", "lookup_throws"}
ACTIONS = {"continue", "lookup", "skip", "inject", "fail"}


def compile_rules(specification):
    """Validate the data language and index simultaneous rules by stage."""
    if set(specification) != {"schema_version", "stages", "rules"} or specification["schema_version"] != 1:
        raise ValueError("Behavior rules: unsupported specification schema")
    stages = specification["stages"]
    if not stages or len(stages) != len(set(stages)):
        raise ValueError("Behavior rules: stages must be nonempty and unique")
    indexed, identifiers = {stage: [] for stage in stages}, set()
    for rule in specification["rules"]:
        if set(rule) != {"id", "description", "stage", "when", "requires"}:
            raise ValueError(f"Behavior rule has unexpected fields: {rule}")
        identifier = rule["id"]
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            raise ValueError(f"Behavior rule id must be nonempty and unique: {identifier!r}")
        identifiers.add(identifier)
        if rule["stage"] not in indexed:
            raise ValueError(f"Rule {identifier}: unknown stage {rule['stage']}")
        unknown = set(rule["when"]) - FACTS
        if unknown or any(type(value) is not bool for value in rule["when"].values()):
            raise ValueError(f"Rule {identifier}: unknown facts {sorted(unknown)} or non-Boolean guard")
        requires = rule["requires"]
        action = requires.get("action")
        fields = {"action", "error_type", "message"} if action == "fail" else {"action"}
        if action not in ACTIONS or set(requires) != fields:
            raise ValueError(f"Rule {identifier}: incomplete or unknown requirement {requires}")
        if action == "fail":
            if not isinstance(requires["error_type"], str) or not isinstance(requires["message"], str):
                raise ValueError(f"Rule {identifier}: exception type and message must be strings")
            for _, field, format_spec, conversion in string.Formatter().parse(requires["message"]):
                if field is not None and (field != "name" or format_spec or conversion):
                    raise ValueError(f"Rule {identifier}: unknown message substitution {field}")
        indexed[rule["stage"]].append(rule)
    for rules in indexed.values():
        rules.sort(key=lambda rule: rule["id"])
    return indexed


def matches(guard, facts):
    """An unattempted lookup has no known outcome: its guard is undecidable."""
    if any(facts[key] is not None and facts[key] != value for key, value in guard.items()):
        return False
    return None if any(facts[key] is None for key in guard) else True


def conflicts(rules, parameter, stage, blocked_by):
    """Detect incompatible requirements even at stages hidden by precedence."""
    found = []
    for left, right in itertools.combinations(rules, 2):
        for key in sorted(left["requires"].keys() & right["requires"].keys()):
            if left["requires"][key] != right["requires"][key]:
                found.append({"parameter": parameter, "stage": stage,
                              "rules": [left["id"], right["id"]], "property": key,
                              "values": [left["requires"][key], right["requires"][key]],
                              "reached": blocked_by is None, "blocked_by": blocked_by})
    return found


def _analyze_case(case, stages):
    # Reject schema/enum drift even in parameters after an early exit.
    oracle = expected_autowiring(case["arguments"])
    environment = case["arguments"]["contains"]
    registered = PROVIDERS[environment["available"]]
    fail_on = FAILURE_CALLS[environment["fail_at"]]
    plan, trace, events, contradictions, gaps = [], [], [], [], []
    aborted, failure = None, None
    for index, param in enumerate(case["arguments"]["params"]):
        facts = {"variadic": param["kind"] == "Variadic",
                 "positional_only": param["kind"] == "PositionalOnly",
                 "annotated": param["annotation"] is not None,
                 "has_default": param["has_default"],
                 "registered": param["annotation"] in registered,
                 "lookup_throws": None}
        blocked = aborted
        for stage, rules in stages.items():
            selected, undecidable = [], []
            for rule in rules:
                applies = matches(rule["when"], facts)
                if applies is False:
                    continue
                events.append({"rule": rule["id"], "parameter": index, "stage": stage,
                               "action": rule["requires"]["action"],
                               "status": "guard_unresolved" if applies is None else
                               (blocked["status"] if blocked else "reached"),
                               "blocked_by": blocked})
                if applies is True:
                    requires = dict(rule["requires"])
                    if requires["action"] == "fail":
                        requires["message"] = requires["message"].format(name=param["name"])
                    selected.append({**rule, "requires": requires})
                elif applies is None:
                    undecidable.append(rule["id"])
            collisions = conflicts(selected, index, stage, blocked)
            contradictions.extend(collisions)
            if blocked:
                continue
            if collisions or not selected or undecidable:
                if not selected or undecidable:
                    gaps.append({"parameter": index, "stage": stage,
                                 "reason": "Reached stage has missing or undecidable obligations",
                                 "undecidable_rules": undecidable, "facts": dict(facts)})
                aborted = blocked = {"status": "blocked_by_specification",
                                     "parameter": index, "stage": stage,
                                     "rules": [rule["id"] for rule in selected]}
                if collisions:
                    for row in events:
                        if row["parameter"] == index and row["stage"] == stage and row["status"] == "reached":
                            row["status"] = "contradictory"
                continue
            # All matching rules agree. No first-match or last-match policy.
            requires = selected[0]["requires"]
            action = requires["action"]
            source = {"parameter": index, "stage": stage, "rules": [rule["id"] for rule in selected]}
            if action == "lookup":
                if param["annotation"] is None or facts["lookup_throws"] is not None:
                    gaps.append({**source, "reason": "Lookup requires an annotation and a single attempt"})
                    aborted = blocked = {**source, "status": "blocked_by_specification"}
                    continue
                trace.append(param["annotation"])
                facts["lookup_throws"] = len(trace) == fail_on
            elif action == "fail":
                failure = {"native_error_type": requires["error_type"],
                           "native_error": requires["message"]}
                aborted = blocked = {**source, "status": "masked_by_failure"}
            elif action in {"skip", "inject"}:
                if action == "inject":
                    plan.append({"name": param["name"], "annotation": param["annotation"]})
                blocked = {**source, "status": "masked_by_skip" if action == "skip" else "masked_by_injection"}
        if blocked is None:
            gaps.append({"parameter": index, "stage": list(stages)[-1],
                         "reason": "Parameter reaches end of stages without skip, injection or failure"})
            aborted = {"status": "blocked_by_specification", "parameter": index,
                       "stage": list(stages)[-1], "rules": []}

    expected = None if contradictions or gaps else {**(failure or {"native_result": plan}), "lookup_trace": trace}
    actual = actual_outcome(case)
    disagreement = expected is not None and expected != oracle
    native_failure = expected is not None and expected != actual
    verified = expected is not None and not disagreement and not native_failure
    for row in events:
        if row["status"] != "reached":
            continue
        if not verified:
            row["status"] = "unverified"
        elif failure and row["action"] not in {"lookup", "fail"}:
            # A later exception discards the plan. Its prefix is not observable.
            row["status"] = "inferred_before_failure"
        else:
            row["status"] = "exercised"
    return {"id": case["id"], "verified": verified, "expected": expected, "actual": actual,
            "oracle": oracle, "rules": events, "contradictions": contradictions,
            "specification_gaps": gaps, "contract_disagreement": disagreement,
            "native_failure": native_failure}


def analyze_case(case, specification):
    return _analyze_case(case, compile_rules(specification))


def _entry(**identity):
    return {**identity, "instances": {}, "cases": {}, "examples": {}}


def _tally(entry, status, case_id, rows, seen, identity):
    entry["instances"][status] = entry["instances"].get(status, 0) + 1
    key = (*identity, status)
    if key in seen:
        return
    seen.add(key)
    entry["cases"][status] = entry["cases"].get(status, 0) + 1
    examples = entry["examples"].setdefault(status, [])
    if len(examples) < 3:
        examples.append({"case": case_id, "occurrences": [
            {key: row[key] for key in ("rule", "parameter", "stage", "status", "blocked_by")} for row in rows]})


def _overlap_status(left, right, collisions):
    pair = sorted([left["rule"], right["rule"]])
    if left["parameter"] == right["parameter"] and any(
            row["parameter"] == left["parameter"] and row["rules"] == pair for row in collisions):
        return "contradictory"
    statuses = {left["status"], right["status"]}
    if "contradictory" in statuses:
        return "blocked_by_specification"
    for status in ("blocked_by_specification", "unverified", "masked_by_failure",
                   "masked_by_skip", "masked_by_injection", "inferred_before_failure"):
        if status in statuses:
            return status
    return "exercised"


def analyze_behaviors(document, specification, emit_case=None):
    """Count occurrences and distinct cases separately; preserve witnesses."""
    stages = compile_rules(specification)
    identifiers = sorted(rule["id"] for rule in specification["rules"])
    rule_counts = {identifier: _entry(rule=identifier) for identifier in identifiers}
    pairs = {}
    for left, right in itertools.combinations_with_replacement(identifiers, 2):
        relations = ["across_parameters"] if left == right else ["same_parameter", "across_parameters"]
        for relation in relations:
            pairs[left, right, relation] = _entry(rules=[left, right], relation=relation)
    totals = collections.Counter()
    findings = {"contradictions": [], "specification_gaps": [], "contract_disagreements": [], "native_failures": []}
    observed_ids = set()
    for case in document["cases"]:
        if case["id"] in observed_ids:
            raise ValueError(f"Duplicate behavior case id: {case['id']}")
        observed_ids.add(case["id"])
        result = _analyze_case(case, stages)
        totals["cases"] += 1
        totals["verified_cases"] += result["verified"]
        if emit_case:
            emit_case(result)
        for category in ("contradictions", "specification_gaps"):
            findings[category].extend({"case": case["id"], **row} for row in result[category])
        for flag, category in (("contract_disagreement", "contract_disagreements"), ("native_failure", "native_failures")):
            if result[flag]:
                findings[category].append({"case": case["id"], "expected": result["expected"],
                                           "oracle": result["oracle"], "actual": result["actual"]})
        seen = set()
        for row in result["rules"]:
            _tally(rule_counts[row["rule"]], row["status"], case["id"], [row], seen, ("rule", row["rule"]))
        # Unknown predicates are potential applicability, not established overlap.
        matched = [row for row in result["rules"] if row["status"] != "guard_unresolved"]
        for left, right in itertools.combinations(matched, 2):
            relation = "same_parameter" if left["parameter"] == right["parameter"] else "across_parameters"
            identity = (*sorted([left["rule"], right["rule"]]), relation)
            status = _overlap_status(left, right, result["contradictions"])
            _tally(pairs[identity], status, case["id"], [left, right], seen, ("pair", *identity))
    overlaps = list(pairs.values())
    for row in overlaps:
        row["jointly_matched_in_model"] = bool(row["instances"])
    statuses = sorted({status for row in overlaps for status in row["instances"]})
    return {"schema_version": 1, **dict(totals), "rules": list(rule_counts.values()), "overlaps": overlaps,
            **findings,
            "rules_without_exercised_witness": [key for key, row in rule_counts.items()
                                               if not row["cases"].get("exercised")],
            "overlap_types_by_status": {status: sum(bool(row["instances"].get(status)) for row in overlaps)
                                        for status in statuses},
            "overlap_types_without_joint_match": sum(not row["jointly_matched_in_model"] for row in overlaps),
            "rules_sha256": hashlib.sha256(json.dumps(specification, sort_keys=True).encode()).hexdigest(),
            "scope": "Finite reflected cases; parameter occurrence and stage determine obligation identity",
            "counting": "Instances count rule occurrences/pairs; cases deduplicate each status per case. "
                        "A pair type can have several statuses across occurrences/cases.",
            "evidence": "Input predicates and stage precedence are specified, not native branch probes. "
                        "Exercised requires agreement with both the independent oracle and native result/error/trace. "
                        "Non-lookup prefix actions on failing calls remain inferred_before_failure. "
                        "An unattempted callback has unknown lookup_throws, never an assumed failure."}


def require_valid_report(report):
    """No specification defect or false coverage is a passing result."""
    bad = {key: len(report[key]) for key in
           ("contradictions", "specification_gaps", "contract_disagreements", "native_failures",
            "rules_without_exercised_witness") if report[key]}
    if report.get("call_space_gaps"):
        bad["call_space_gaps"] = len(report["call_space_gaps"])
    if bad:
        raise AssertionError(f"Behavior report failed: {bad}")


def report_text(report):
    lines = ["Autowiring behavior coverage", "",
             f"Verified calls: {report.get('verified_cases', 0)}/{report.get('cases', 0)}",
             f"Contradictions: {len(report['contradictions'])}; specification gaps: {len(report['specification_gaps'])}",
             f"Contract disagreements: {len(report['contract_disagreements'])}; native failures: {len(report['native_failures'])}",
             "", "Rule instances (one case may contain several parameter occurrences):"]
    for row in report["rules"]:
        lines.append(f"  {row['rule']}: " + ", ".join(f"{key}={value}" for key, value in sorted(row["instances"].items())))
    lines += ["", "Overlap types (rule pair + same/across parameter; categories can overlap):"]
    lines += [f"  {key}: {value}" for key, value in report["overlap_types_by_status"].items()]
    lines += [f"  No joint match in this finite model: {report['overlap_types_without_joint_match']}",
              "", "Overlap details; numbers count distinct cases per category:"]
    for row in report["overlaps"]:
        if not row["jointly_matched_in_model"]:
            continue
        lines.append(f"  {' + '.join(row['rules'])} [{row['relation']}]: "
                     + ", ".join(f"{key}={value}" for key, value in sorted(row["cases"].items())))
        for status, examples in sorted(row["examples"].items()):
            witness = examples[0]
            occurrences = ", ".join(f"{item['rule']}@{item['parameter']}" for item in witness["occurrences"])
            lines.append(f"    {status}: {witness['case']} ({occurrences})")
    for key in ("contradictions", "specification_gaps", "contract_disagreements", "native_failures"):
        if report[key]:
            lines += ["", key + ":", *["  " + json.dumps(row, sort_keys=True) for row in report[key][:10]]]
    lines += ["", report["counting"], report["evidence"], ""]
    return "\n".join(lines)


def write_behavior_report(document, specification, destination):
    """Persist evidence before the caller gates success, including failing reports."""
    started = time.perf_counter()
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "rules.json").write_bytes((json.dumps(specification, indent=2) + "\n").encode())
    with open(str(destination / "cases.jsonl"), "w", encoding="utf-8") as stream:
        report = analyze_behaviors(document, specification,
                                   lambda row: stream.write(json.dumps(row, separators=(",", ":")) + "\n"))
    report["call_space_gaps"] = coverage_gaps(document)
    report["analysis_seconds"] = time.perf_counter() - started
    (destination / "report.json").write_bytes((json.dumps(report, indent=2) + "\n").encode())
    (destination / "report.txt").write_bytes(report_text(report).encode())
    return report


def write_conflict_control(document, specification, destination):
    """A scratch specification must expose both reached and masked conflicts."""
    broken = copy.deepcopy(specification)
    broken["rules"].append({
        "id": "defaults_suppress_injection", "description": "Deliberately contradictory negative control.",
        "stage": "provider", "when": {"annotated": True, "registered": True, "has_default": True},
        "requires": {"action": "skip"}})
    witnesses = []
    for kind in ("KeywordOnly", "PositionalOnly"):
        witnesses.append(next(case for case in document["cases"]
                              if len(case["arguments"]["params"]) == 1
                              and case["arguments"]["params"][0]["kind"] == kind
                              and case["arguments"]["params"][0]["annotation"] == 0
                              and case["arguments"]["params"][0]["has_default"]
                              and case["arguments"]["contains"] == {"available": "Both", "fail_at": "Never"}))
    details = []
    report = analyze_behaviors({"cases": witnesses}, broken, details.append)
    assert len(report["contradictions"]) == 4
    assert {row["reached"] for row in report["contradictions"]} == {True, False}
    try:
        require_valid_report(report)
    except AssertionError as error:
        assert "contradictions" in str(error)
        report["expected_gate_failure"] = str(error)
    else:
        raise AssertionError("Contradictory rules escaped the report gate")
    destination.mkdir(parents=True, exist_ok=True)
    for name, value in (("rules.json", broken), ("native-witnesses.json", witnesses),
                        ("case-analysis.json", details), ("report.json", report)):
        (destination / name).write_bytes((json.dumps(value, indent=2) + "\n").encode())
    (destination / "report.txt").write_bytes(report_text(report).encode())
    return {"case_ids": [case["id"] for case in witnesses], "contradictions": 4,
            "reached_and_masked_conflicts_detected": True, "report": str(destination / "report.json")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", help="Reflected cases.json from an autowiring run")
    parser.add_argument("--rules", default=str(path(__file__).parent / "autowire_behaviors.json"))
    parser.add_argument("--output", required=True, help="Directory for the behavior report and witnesses")
    args = parser.parse_args()
    document = json.loads(path(args.cases).read_bytes())
    specification = json.loads(path(args.rules).read_bytes())
    report = write_behavior_report(document, specification, path(args.output))
    print(f"Behavior report: {args.output}/report.txt; {report.get('verified_cases', 0)} verified calls; "
          f"{len(report['contradictions'])} contradictions")
    require_valid_report(report)


if __name__ == "__main__":
    main()
