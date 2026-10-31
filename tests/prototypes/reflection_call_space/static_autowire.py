"""Generate real static_assert statements from the reflected autowiring call space.

Inputs come from the model; expectations come only from expected_autowiring.
No observed native result is embedded as a golden answer. Each compiler invocation
checks a bounded shard, keeping constant-evaluation memory bounded.
"""
import collections
import copy
import hashlib
import json
import re
import time

from run import command, require_success, write_json, write_text
from verify import coverage_gaps
from verify_autowire import expected_autowiring


def cpp_string(value):
    return json.dumps(value, ensure_ascii=True)


def assertion_source(case):
    """One call, including error type/message and lookup order, becomes one proof."""
    arguments = case["arguments"]
    expected = expected_autowiring(arguments)
    params = []
    for param in arguments["params"]:
        annotation = "std::nullopt" if param["annotation"] is None else str(param["annotation"])
        params.append("{" + ", ".join((cpp_string(param["name"]),
                      "pygim::core::ParamKind::" + param["kind"], annotation,
                      str(param["has_default"]).lower())) + "}")
    environment = arguments["contains"]
    plan = ["{" + cpp_string(item["name"]) + ", " + str(item["annotation"]) + "}"
            for item in expected.get("native_result", [])]
    return (
        "static_assert(verify_autowire_case(\n"
        "    {" + ", ".join(params) + "},\n"
        "    {Available::" + environment["available"] + ", FailAt::" + environment["fail_at"] + "},\n"
        "    {" + ", ".join(plan) + "},\n"
        "    " + cpp_string(expected.get("native_error_type", "success")) + ", "
        + cpp_string(expected.get("native_error", "")) + ",\n"
        "    {" + ", ".join(map(str, expected["lookup_trace"])) + "}),\n"
        "    " + cpp_string("autowire " + case["id"]) + ");\n"
    )


def schema_source(document):
    """Guard saved assertions against changed signatures, fields, enums or bounds."""
    lines = ['#pragma once', '#include "autowire_static.h"', ""]
    params = document["function"]["parameters"]

    def check(expression, subject):
        lines.append(f"static_assert({expression}, {cpp_string('call-space schema: ' + subject)});")

    def domain_checks(domain, alias):
        kind = domain["kind"]
        if kind == "record":
            fields = domain["fields"]
            check(f"call_space::members<{alias}>().size() == {len(fields)}", alias + " fields")
            for index, field in enumerate(fields):
                field_info = f"call_space::members<{alias}>()[{index}]"
                check(f"std::meta::identifier_of({field_info}) == {cpp_string(field['name'])}",
                      alias + " field name")
                check(f"std::meta::display_string_of(std::meta::type_of({field_info})) == "
                      + cpp_string(field["cpp_type"]), alias + " field type")
                child = f"{alias}_field_{index}"
                lines.append(f"using {child} = call_space::member_type<{alias}, {index}>;")
                domain_checks(field["domain"], child)
        elif kind in {"sequence", "optional"}:
            trait = "sequence_traits" if kind == "sequence" else "optional_traits"
            child = alias + "_element"
            lines.append(f"using {child} = typename call_space::{trait}<{alias}>::element;")
            if kind == "sequence":
                check(f"call_space::domain<{alias}, autowire_model>::max_length == "
                      + str(domain["max_length"]), alias + " length bound")
            domain_checks(domain["element"], child)
        else:
            if kind == "enum":
                values = [f"{alias}::{value}" for value in domain["values"]]
                check(f"std::meta::enumerators_of(^^{alias}).size() == {len(values)}",
                      alias + " enumerator count")
            elif kind == "string":
                values = [cpp_string(value) for value in domain["values"]]
            elif kind == "boolean":
                values = [str(value).lower() for value in domain["values"]]
            elif kind == "integer":
                values = [str(value) for value in domain["values"]]
            else:
                raise ValueError(f"Unmodeled compile-time domain {kind}")
            check(f"call_space::domain<{alias}, autowire_model>::values() == "
                  + f"std::vector<{alias}>{{{', '.join(values)}}}", alias + " values")

    check(f"std::meta::parameters_of(autowire_target()).size() == {len(params)}", "parameter count")
    check("std::meta::display_string_of(std::meta::return_type_of(autowire_target())) == "
          + cpp_string(document["function"]["return_type"]), "return type")
    for index, param in enumerate(params):
        info = f"std::meta::parameters_of(autowire_target())[{index}]"
        check(f"std::meta::identifier_of({info}) == {cpp_string(param['name'])}", "parameter name")
        check(f"std::meta::display_string_of(std::meta::type_of({info})) == "
              + cpp_string(param["cpp_type"]), "parameter type")
        check(f"std::meta::has_default_argument({info}) == {str(param['has_default']).lower()}",
              "parameter default")
        alias = f"input_{index}"
        lines.append(f"using {alias} = call_space::parameter_type<autowire_target(), {index}>;")
        domain_checks(param["domain"], alias)
    return "\n".join(lines) + "\n"


def assertion_ids(text):
    return re.findall(r'^    "autowire (case-\d+)"\);$', text, re.MULTILINE)


def proof_coverage_gaps(expected_ids, emitted_ids):
    expected, emitted = set(expected_ids), set(emitted_ids)
    issues = []
    if expected - emitted:
        issues.append({"kind": "missing_assertions", "count": len(expected - emitted),
                       "example": min(expected - emitted)})
    if emitted - expected:
        issues.append({"kind": "unexpected_assertions", "count": len(emitted - expected)})
    if len(emitted_ids) != len(emitted):
        issues.append({"kind": "duplicate_assertions"})
    return issues


def compile_source(source, includes, compiler):
    argv = [compiler, "-std=c++26", "-freflection", "-fsyntax-only", "-fmax-errors=3",
            "-Wall", "-Wextra", *[f"-I{include}" for include in includes], str(source)]
    result, seconds = command(argv, source.parent)
    write_text(source.parent / (source.name + ".log"), result.stdout + result.stderr)
    return result, {"command": argv, "seconds": seconds, "returncode": result.returncode}


def generate_static_assertions(document, destination, shard_size=1024):
    if shard_size < 1:
        raise ValueError("shard_size must be positive")
    gaps = coverage_gaps(document)
    if gaps:
        raise AssertionError({"input_call_space": gaps})
    destination.mkdir(parents=True, exist_ok=True)
    write_text(destination / "model_schema.h", schema_source(document))
    files, manifest, emitted_ids = [], [], []
    for start in range(0, len(document["cases"]), shard_size):
        cases = document["cases"][start:start + shard_size]
        source = destination / f"assertions_{len(files):03d}.cpp"
        text = '#include "model_schema.h"\n\n'
        for case in cases:
            line = text.count("\n") + 1
            text += assertion_source(case) + "\n"
            manifest.append({"id": case["id"], "file": source.name, "line": line,
                             "outcome": expected_autowiring(case["arguments"]).get(
                                 "native_error_type", "success")})
        write_text(source, text)
        # Read the written sources independently rather than trusting loop counts.
        emitted_ids.extend(assertion_ids(source.read_bytes().decode("utf-8")))
        files.append(source)
    issues = proof_coverage_gaps([case["id"] for case in document["cases"]], emitted_ids)
    assert not issues, issues
    write_json(destination / "manifest.json", manifest)
    return files, manifest


def run_static_assertions(document, run_dir, includes, compiler, shard_size=1024):
    started = time.perf_counter()
    destination = run_dir / "static"
    files, manifest = generate_static_assertions(document, destination, shard_size)
    compile_includes = [destination, *includes]
    outcomes = collections.Counter(row["outcome"] for row in manifest)
    print(f"Compiling {len(manifest)} generated static_assert cases in {len(files)} shards...", flush=True)
    builds = []
    for index, source in enumerate(files):
        result, build = compile_source(source, compile_includes, compiler)
        builds.append(build)
        if result.returncode:
            write_json(destination / "failed-build.json", build)
        require_success(result, f"static assertions {source.name}")
        print(f"Static shard {index + 1}/{len(files)} passed ({build['seconds']:.2f}s).", flush=True)

    # Removing a proof must fail the independent manifest check.
    first_case = document["cases"][0]
    first_text = files[0].read_bytes().decode("utf-8")
    missing_text = first_text.replace(assertion_source(first_case), "", 1)
    missing_ids = assertion_ids(missing_text)
    expected_first_ids = assertion_ids(first_text)
    missing_issues = proof_coverage_gaps(expected_first_ids, missing_ids)
    assert any(issue["kind"] == "missing_assertions" for issue in missing_issues)
    write_text(destination / "missing_assertion.cpp.txt", missing_text)

    # Tampering with observed native results cannot change generated expectations.
    poisoned = copy.deepcopy(first_case)
    poisoned["native_result"] = [{"name": "WRONG", "annotation": 999}]
    poisoned["native_error"] = "WRONG"
    assert assertion_source(poisoned) == assertion_source(first_case)

    mutation_checks = {}
    by_id = {case["id"]: case for case in document["cases"]}
    for name in ("skip_registered_defaults", "reverse_injection_order", "wrong_missing_exception"):
        mutations = json.loads((run_dir / name / "failures.json").read_bytes())
        witness = by_id[mutations[0]["id"]]
        source = destination / (name + ".cpp")
        write_text(source, '#include "model_schema.h"\n\n' + assertion_source(witness))
        result, build = compile_source(source, [run_dir / name, *compile_includes], compiler)
        marker = "static assertion failed: autowire " + witness["id"]
        assert result.returncode != 0 and marker in result.stderr, result.stderr
        mutation_checks[name] = {**build, "case": witness["id"], "diagnostic": marker}

    drift_source = destination / "enum_drift.cpp"
    write_text(drift_source, '#include "model_schema.h"\n\n' + assertion_source(first_case))
    drift, drift_build = compile_source(
        drift_source, [run_dir / "added_parameter_kind", *compile_includes], compiler)
    assert drift.returncode != 0 and "static assertion failed: call-space schema:" in drift.stderr

    report = {
        "assertion_count": len(manifest), "outcomes": dict(outcomes), "shards": len(files),
        "all_assertions_compiled": True, "runtime_only_cases": [],
        "coverage_gaps": [], "builds": builds,
        "compile_seconds": sum(build["seconds"] for build in builds),
        "total_seconds": time.perf_counter() - started,
        "negative_controls": {"missing_assertion": missing_issues,
                              "observed_result_poisoning_ignored": True,
                              "mutants_rejected_by_static_assert": mutation_checks,
                              "enum_drift_rejected_by_schema_assert": drift_build},
        "model_sha256": hashlib.sha256(json.dumps(document["function"], sort_keys=True).encode()).hexdigest(),
        "scope": "Same finite argument model; local-trace callback delegates to shared Lookup.evaluate",
        "does_not_prove": ["arbitrary inputs or callbacks", "actual allocation failure",
                          "Python binding behavior", "full branch coverage"],
    }
    write_json(destination / "report.json", report)
    write_text(destination / "report.txt",
               f"Autowiring compile-time assertions: {len(manifest)}/{len(manifest)} passed\n"
               f"Outcome counts: {dict(outcomes)}\n"
               f"Shards: {len(files)}; successful compilation total: {report['compile_seconds']:.3f}s\n"
               "Three policy mutants rejected by named case assertions; enum drift rejected by schema assertions.\n"
               "Missing assertion detected; observed native results do not supply expected answers.\n"
               "Every case is evaluated during compilation; no proof executable is run.\n")
    return report
