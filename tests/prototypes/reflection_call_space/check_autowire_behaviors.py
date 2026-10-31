"""Explicitly run with pytest: evidence accounting, precedence, and specification defects."""
import copy
import json
from pathlib import Path

import pytest
from behavior_autowire import analyze_behaviors, analyze_case, require_valid_report
from verify_autowire import expected_autowiring


@pytest.fixture
def rules():
    return json.loads(Path(__file__).with_name("autowire_behaviors.json").read_bytes())


def param(name="first", kind="KeywordOnly", annotation=0, default=False):
    return {"name": name, "kind": kind, "annotation": annotation, "has_default": default}


def case(*params, available="Both", fail_at="Never", identifier="example"):
    arguments = {"params": list(params), "contains": {"available": available, "fail_at": fail_at}}
    return {"id": identifier, "arguments": arguments, **expected_autowiring(arguments)}


def event(result, rule, index=0):
    return next(row for row in result["rules"] if row["rule"] == rule and row["parameter"] == index)


def test_registered_default_exercises_both_obligations(rules):
    result = analyze_case(case(param(default=True)), rules)
    assert result["verified"]
    assert event(result, "inject_registered")["status"] == "exercised"
    assert event(result, "registered_beats_default")["status"] == "exercised"
    assert not result["contradictions"]


def test_failure_names_same_and_later_parameter_rules_it_masks(rules):
    result = analyze_case(case(param(kind="PositionalOnly", annotation=None), param("second")), rules)
    assert event(result, "reject_positional_only")["status"] == "exercised"
    for rule, index in (("reject_untyped_required", 0), ("inject_registered", 1), ("lookup_annotated", 1)):
        row = event(result, rule, index)
        assert row["status"] == "masked_by_failure"
        assert row["blocked_by"]["rules"] == ["reject_positional_only"]
        assert row["blocked_by"]["parameter"] == 0
    assert event(result, "propagate_lookup_failure", 1)["status"] == "guard_unresolved"
    assert result["expected"]["lookup_trace"] == []


def test_skip_is_separate_from_failure_and_does_not_stop_later_parameters(rules):
    result = analyze_case(case(param(kind="Variadic"), param("second")), rules)
    assert event(result, "inject_registered")["status"] == "masked_by_skip"
    assert event(result, "inject_registered", 1)["status"] == "exercised"
    assert result["expected"]["lookup_trace"] == [0]


def test_second_failure_uses_attempted_lookups_not_parameter_position(rules):
    result = analyze_case(case(param(kind="Variadic"), param("second", default=True), fail_at="Second"), rules)
    assert result["verified"]
    assert event(result, "registered_beats_default", 1)["status"] == "exercised"
    assert not any(row["rule"] == "propagate_lookup_failure" and row["status"] == "exercised"
                   for row in result["rules"])


def test_default_cannot_swallow_failure_and_prefix_plan_is_not_observed(rules):
    result = analyze_case(case(param(), param("second", default=True), fail_at="Second"), rules)
    assert result["verified"]
    assert event(result, "inject_registered")["status"] == "inferred_before_failure"
    assert event(result, "lookup_annotated")["status"] == "exercised"
    assert event(result, "default_does_not_swallow_failure", 1)["status"] == "exercised"
    assert event(result, "registered_beats_default", 1)["status"] == "masked_by_failure"


def contradictory_rules(rules):
    broken = copy.deepcopy(rules)
    broken["rules"].append({"id": "defaults_suppress_injection", "description": "Deliberately wrong contract.",
                            "stage": "provider", "when": {"annotated": True, "registered": True,
                                                           "has_default": True},
                            "requires": {"action": "skip"}})
    return broken


@pytest.mark.parametrize("kind", ["KeywordOnly", "PositionalOnly"])
def test_contradictions_include_masked_decisions_and_ignore_rule_list_order(rules, kind):
    broken = contradictory_rules(rules)
    witness = case(param(kind=kind, default=True))
    forward = analyze_case(witness, broken)
    broken["rules"].reverse()
    backward = analyze_case(witness, broken)
    assert forward == backward
    assert len(forward["contradictions"]) == 2
    assert all(row["property"] == "action" for row in forward["contradictions"])
    assert all(row["reached"] == (kind == "KeywordOnly") for row in forward["contradictions"])
    report = analyze_behaviors({"cases": [witness]}, broken)
    assert report["overlap_types_by_status"]["contradictory"] == 2
    with pytest.raises(AssertionError, match="contradictions"):
        require_valid_report(report)


def test_missing_decision_rule_fails_closed(rules):
    rules["rules"] = [row for row in rules["rules"] if row["id"] != "reject_missing_required"]
    result = analyze_case(case(param(), available="Neither"), rules)
    assert result["specification_gaps"][0]["stage"] == "provider"
    assert result["expected"] is None
    assert not result["verified"]


def test_consistently_wrong_data_disagrees_with_independent_contract(rules):
    rule = next(row for row in rules["rules"] if row["id"] == "reject_missing_required")
    rule["requires"]["error_type"] = "runtime_error"
    result = analyze_case(case(param(), available="Neither"), rules)
    assert result["contract_disagreement"]
    assert result["native_failure"]
    assert not result["contradictions"]


def test_conflicts_compare_concrete_messages_not_template_spelling(rules):
    duplicate = copy.deepcopy(next(row for row in rules["rules"] if row["id"] == "reject_untyped_required"))
    duplicate["id"] = "fixed_name_error"
    duplicate["requires"]["message"] = "autowire requires a type annotation for parameter 'first'"
    rules["rules"].append(duplicate)
    assert analyze_case(case(param(annotation=None)), rules)["verified"]
    conflict = analyze_case(case(param("second", annotation=None)), rules)
    assert conflict["contradictions"][0]["property"] == "message"


def test_poisoned_observations_cannot_change_expectations_or_earn_coverage(rules):
    original = case(param(default=True))
    poisoned = {**original, "native_result": [], "lookup_trace": []}
    before, after = analyze_case(original, rules), analyze_case(poisoned, rules)
    assert before["expected"] == after["expected"]
    assert after["native_failure"]
    assert not any(row["status"] == "exercised" for row in after["rules"])


def test_repeated_names_keep_occurrence_identity_and_pair_counts(rules):
    witness = case(param(default=True), param(default=True))
    report = analyze_behaviors({"cases": [witness]}, rules)
    overlap = next(row for row in report["overlaps"]
                   if row["rules"] == ["inject_registered", "registered_beats_default"]
                   and row["relation"] == "same_parameter")
    assert overlap["instances"]["exercised"] == 2
    assert overlap["cases"]["exercised"] == 1
    repeated = next(row for row in report["overlaps"]
                    if row["rules"] == ["inject_registered", "inject_registered"])
    assert repeated["relation"] == "across_parameters"
    assert repeated["instances"]["exercised"] == 1


def test_unknown_guard_fact_is_not_silently_false(rules):
    rules["rules"][0]["when"] = {"typo": True}
    with pytest.raises(ValueError, match="typo"):
        analyze_case(case(), rules)


def test_dynamic_guard_at_a_reached_stage_cannot_be_silently_ignored(rules):
    rule = next(row for row in rules["rules"] if row["id"] == "propagate_lookup_failure")
    rule["stage"] = "kind"
    result = analyze_case(case(param()), rules)
    assert result["specification_gaps"][0]["undecidable_rules"] == ["propagate_lookup_failure"]
    assert not result["verified"]


def test_empty_call_is_verified_without_inventing_rule_coverage(rules):
    result = analyze_case(case(), rules)
    assert result["verified"]
    assert result["rules"] == []
    assert result["expected"] == {"native_result": [], "lookup_trace": []}
