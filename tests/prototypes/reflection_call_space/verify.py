"""Independent consumer of reflection records; it never parses C++ source.

Only ``expected_to_csv`` knows the operation's behavioral contract. The coverage
checker knows declared factors and legal trailing omission, but no function body.
"""

import itertools
import json

from pygim import utils


def case_key(arguments):
    return json.dumps(arguments, ensure_ascii=False, sort_keys=True)


def domain_gaps(domain, location):
    """Rebuild nested products independently; a shrunken emitted list is a gap."""
    issues = []
    kind = domain["kind"]
    values = domain["values"]
    if kind == "record":
        fields = domain["fields"]
        expected = [dict(zip((field["name"] for field in fields), row))
                    for row in itertools.product(*(field["domain"]["values"] for field in fields))]
        for field in fields:
            issues.extend(domain_gaps(field["domain"], f"{location}.{field['name']}"))
    elif kind in {"sequence", "optional"}:
        element = domain["element"]
        issues.extend(domain_gaps(element, f"{location}.element"))
        expected = ([None, *element["values"]] if kind == "optional" else
                    [list(row) for length in range(domain["max_length"] + 1)
                     for row in itertools.product(element["values"], repeat=length)])
    elif kind == "boolean":
        expected = [False, True]
    else:
        expected = values  # Scalar bounds/atoms and reflected enumerators are declarations.
    actual_keys = [case_key(value) for value in values]
    if not values or len(set(actual_keys)) != len(values):
        issues.append({"kind": "invalid_domain", "location": location})
    if set(actual_keys) != {case_key(value) for value in expected}:
        issues.append({"kind": "incomplete_structured_domain", "location": location})
    return issues


def required_calls(document):
    """Enumerate legal arities and products independently of C++ recursion."""
    params = document["function"]["parameters"]
    result = set()
    for arity in range(len(params) + 1):
        if not all(p["has_default"] for p in params[arity:]):
            continue
        for values in itertools.product(*(p["domain"]["values"] for p in params[:arity])):
            result.add(case_key(dict(zip((p["name"] for p in params[:arity]), values))))
    return result


def coverage_gaps(document):
    params = document["function"]["parameters"]
    records = document["cases"]
    required = required_calls(document)
    actual = [case_key(case["arguments"]) for case in records]
    issues = []
    for param in params:
        issues.extend(domain_gaps(param["domain"], param["name"]))
        values = param["domain"]["values"]
        if not values:
            issues.append({"kind": "empty_domain", "parameter": param["name"]})
        if len({case_key(v) for v in values}) != len(values):
            issues.append({"kind": "duplicate_domain_values", "parameter": param["name"]})
    missing = sorted(required - set(actual))
    extra = sorted(set(actual) - required)
    if missing:
        issues.append({"kind": "missing_calls", "count": len(missing), "example": missing[0]})
    if extra:
        issues.append({"kind": "unexpected_calls", "count": len(extra), "example": extra[0]})
    if len(set(actual)) != len(actual):
        issues.append({"kind": "duplicate_calls"})
    if len({c["id"] for c in records}) != len(records):
        issues.append({"kind": "duplicate_ids"})
    if document["case_count"] != len(records):
        issues.append({"kind": "count_mismatch"})
    for case in records:
        omitted = [p["name"] for p in params if p["name"] not in case["arguments"]]
        if case["omitted"] != omitted:
            issues.append({"kind": "incorrect_omissions", "id": case["id"]})
    return issues


def expected_to_csv(arguments):
    """Human contract: join with ', '; ascending iff requested; default false.

    This is independent of the C++ result. Neither this contract nor the default
    value is inferred from reflection. This function is not an RFC CSV encoder.
    """
    unknown = set(arguments) - {"strings", "sorted"}
    if unknown:
        raise ValueError(f"Contract does not describe parameters: {sorted(unknown)}")
    values = arguments["strings"]
    return ", ".join(sorted(values) if arguments.get("sorted", False) else values)


def contract_failures(document):
    return [
        {"id": case["id"], "expected": expected_to_csv(case["arguments"]),
         "actual": case.get("native_result"), "error": case.get("native_error")}
        for case in document["cases"]
        if "native_error" in case or case.get("native_result") != expected_to_csv(case["arguments"])
    ]


def audit_binding(document):
    """Probe, do not presume, whether native names/defaults reach Python.

    Rejections are explicit parity gaps, not skipped or passing assertions.
    Overloaded functions would additionally require a declaration-to-binding map.
    """
    function = getattr(utils, document["function"]["name"])
    params = document["function"]["parameters"]
    rows = []
    for case in document["cases"]:
        for form in ("positional", "native_keyword_names"):
            arguments = json.loads(json.dumps(case["arguments"]))
            before = json.loads(json.dumps(arguments))
            row = {"id": case["id"], "form": form, "omitted": case["omitted"]}
            try:
                if form == "positional":
                    result = function(*(arguments[p["name"]] for p in params if p["name"] in arguments))
                else:
                    result = function(**arguments)
                row.update(result=result, status="verified" if result == expected_to_csv(before)
                           and arguments == before else "contract_failure")
            except TypeError as error:
                row.update(status="rejected", error=str(error))
            rows.append(row)
    return {"module_file": utils.__file__, "docstring": function.__doc__, "attempts": rows}
