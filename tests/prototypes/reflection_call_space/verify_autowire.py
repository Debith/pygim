"""Explicit IoC contract and public adapter for the reflected call-space model.

The oracle knows policy, never the C++ implementation or its observed results.
Public checks build real Python constructors and call pygim.ioc.Container.
"""
import inspect

from pygim.ioc import Container

PARAMETER_FIELDS = {"name", "kind", "annotation", "has_default"}
KINDS = {
    "PositionalOrKeyword": inspect.Parameter.POSITIONAL_OR_KEYWORD,
    "KeywordOnly": inspect.Parameter.KEYWORD_ONLY,
    "PositionalOnly": inspect.Parameter.POSITIONAL_ONLY,
    "Variadic": inspect.Parameter.VAR_POSITIONAL,
}
PROVIDERS = {"Neither": set(), "Zero": {0}, "One": {1}, "Both": {0, 1}}
FAILURE_CALLS = {"Never": None, "First": 1, "Second": 2}
DEFAULT = "__use_constructor_default__"


def expected_autowiring(arguments):
    """Human policy: ordered validation; defaults only when injection unavailable.

    Variadics are ignored. Positional-only parameters are unsupported, including
    optional ones. A type and provider are needed unless a default exists.
    A failed lookup propagates even when the parameter has a default.
    """
    if set(arguments) != {"params", "contains"}:
        raise ValueError("Contract needs review: changed function parameters")
    environment = arguments["contains"]
    if set(environment) != {"available", "fail_at"}:
        raise ValueError("Contract needs review: changed callback environment")
    registered = PROVIDERS[environment["available"]]
    fail_on = FAILURE_CALLS[environment["fail_at"]]
    params = arguments["params"]
    # Validate the whole input schema even when an earlier parameter fails.
    for param in params:
        if set(param) != PARAMETER_FIELDS or param["kind"] not in KINDS:
            raise ValueError("Contract needs review: changed parameter record or enum")
        if param["annotation"] not in (None, 0, 1):
            raise ValueError("Contract needs review: unknown dependency identity")

    injections, lookups = [], []

    def error(kind, message):
        return {"native_error_type": kind, "native_error": message, "lookup_trace": list(lookups)}

    for param in params:
        name, kind, annotation = param["name"], param["kind"], param["annotation"]
        if kind == "Variadic":
            continue
        if kind == "PositionalOnly":
            return error("runtime_error", f"autowire does not support positional-only parameter '{name}'")
        if annotation is None:
            if param["has_default"]:
                continue
            return error("runtime_error", f"autowire requires a type annotation for parameter '{name}'")
        lookups.append(annotation)
        if len(lookups) == fail_on:
            return error("lookup_failure", "injected lookup failure")
        if annotation in registered:
            injections.append({"name": name, "annotation": annotation})
        elif not param["has_default"]:
            return error("missing_key", f"No provider registered for autowired dependency '{name}'")
    return {"native_result": injections, "lookup_trace": lookups}


def actual_outcome(case):
    return {key: case[key] for key in
            ("native_result", "native_error", "native_error_type", "lookup_trace") if key in case}


def contract_failures(document):
    failures = []
    for case in document["cases"]:
        expected = expected_autowiring(case["arguments"])
        actual = actual_outcome(case)
        if actual != expected:
            failures.append({"id": case["id"], "arguments": case["arguments"],
                             "expected": expected, "actual": actual})
    return failures


def make_service(params, variadic_kind):
    """Map representable native records to real constructors; reject other shapes.

    Repeated names, illegal order, and variadic defaults are legal native records
    but cannot describe a Python signature. No installed bindings are modified.
    """
    signature_params = [inspect.Parameter("self", inspect.Parameter.POSITIONAL_ONLY)]
    for param in params:
        kind = KINDS[param["kind"]]
        if param["kind"] == "Variadic":
            kind = variadic_kind
        annotation = (inspect.Parameter.empty if param["annotation"] is None
                      else f"Dependency{param['annotation']}")
        default = DEFAULT if param["has_default"] else inspect.Parameter.empty
        signature_params.append(inspect.Parameter(param["name"], kind, default=default,
                                                  annotation=annotation))
    signature = inspect.Signature(signature_params)
    dependencies = [type(f"Dependency{i}", (), {}) for i in range(2)]
    namespace = {dependency.__name__: dependency for dependency in dependencies}
    fields = ", ".join(f"{param['name']!r}: {param['name']}" for param in params)
    source = f"def __init__{signature}:\n    self.received = {{{fields}}}\n"
    # This source is generated solely from the bounded local test model.
    exec(compile(source, "<generated-autowire-constructor>", "exec"), namespace)  # noqa: S102
    return type("Service", (), {"__init__": namespace["__init__"]}), dependencies


def public_attempt(case, variadic_kind=inspect.Parameter.VAR_POSITIONAL):
    arguments = case["arguments"]
    row = {"id": case["id"], "variadic_form": variadic_kind.name}
    if arguments["contains"]["fail_at"] != "Never":
        return {**row, "status": "native_only_callback_fault"}
    try:
        service, dependencies = make_service(arguments["params"], variadic_kind)
    except (ValueError, SyntaxError) as error:
        return {**row, "status": "not_a_python_signature", "reason": str(error)}
    expected = expected_autowiring(arguments)
    container = Container()
    instances = [dependency() for dependency in dependencies]
    for key in PROVIDERS[arguments["contains"]["available"]]:
        container.register(dependencies[key], lambda key=key: instances[key])
    container.register(service, service, autowire=True)
    try:
        resolved = container.resolve(service)
    except (RuntimeError, KeyError, TypeError, ValueError) as error:
        error_type = {"runtime_error": RuntimeError, "missing_key": KeyError}.get(
            expected.get("native_error_type"))
        verified = (error_type is not None and type(error) is error_type
                    and expected["native_error"] in str(error))
        return {**row, "status": "verified_error" if verified else "contract_failure",
                "error_type": type(error).__name__, "error": str(error)}
    if "native_error" in expected:
        return {**row, "status": "contract_failure", "reason": "Expected error; call succeeded"}
    planned = {item["name"]: item["annotation"] for item in expected["native_result"]}
    for param in arguments["params"]:
        name = param["name"]
        actual = resolved.received[name]
        if name in planned:
            valid = actual is instances[planned[name]]
        elif param["kind"] == "Variadic":
            valid = actual == (() if variadic_kind == inspect.Parameter.VAR_POSITIONAL else {})
        else:
            valid = actual == DEFAULT
        if not valid:
            return {**row, "status": "contract_failure", "reason": f"Wrong argument {name}"}
    return {**row, "status": "verified_result"}


def audit_public(document):
    rows = []
    for case in document["cases"]:
        rows.append(public_attempt(case))
        # ParamKind merges *args and **kwargs; exercise both public forms.
        if any(param["kind"] == "Variadic" for param in case["arguments"]["params"]):
            rows.append(public_attempt(case, inspect.Parameter.VAR_KEYWORD))
    return rows
