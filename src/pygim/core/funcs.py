import inspect
import types
import typing as t


###############################################################################
# JSON‑Schema helpers
###############################################################################

_SIMPLE = {int: "integer", float: "number", bool: "boolean", str: "string"}


class _NoSchema(Exception):
    """A type JSON Schema has no form for here."""


def _type_to_schema(tp: t.Any) -> t.Dict[str, t.Any]:
    """Python type → JSON Schema: int, float, bool and str; list[T]; dict[str, T] by its values;
    Optional[T] as T or null; Any as any value. _NoSchema for every other type — found 2026-10-31,
    they were all described as a string (D79)."""
    if tp is t.Any:
        return {}
    origin, args = t.get_origin(tp), t.get_args(tp)
    if origin in (t.Union, types.UnionType):
        rest = [a for a in args if a is not type(None)]
        if len(rest) == 1 and len(rest) < len(args):   # Optional[T]: T, or None
            return {"anyOf": [_type_to_schema(rest[0]), {"type": "null"}]}
        raise _NoSchema(tp)
    base = origin or tp
    if base in _SIMPLE and not args:
        return {"type": _SIMPLE[base]}
    if base is list and len(args) <= 1:
        return {"type": "array", "items": _type_to_schema(args[0] if args else t.Any)}
    if base is dict and not args:
        return {"type": "object"}
    if base is dict and args[0] is str:   # a JSON object's keys are strings
        return {"type": "object", "additionalProperties": _type_to_schema(args[1])}
    raise _NoSchema(tp)


def _named(tp: t.Any) -> str:
    """A type as a message names it: a class by its name, anything else as Python writes it."""
    return tp.__name__ if isinstance(tp, type) and not t.get_args(tp) else str(tp)


def get_schema(func: t.Callable[..., t.Any]) -> t.Dict[str, t.Any]:
    """Return JSON‑Schema for *func*'s parameters, raising if issues exist.

    Each parameter must be annotated with a type JSON Schema has a form for: int, float, bool, str,
    list[T], dict[str, T], Optional[T] or Any. TypeError lists every parameter that is not — an
    unannotated one, ``*args`` or ``**kwargs``, a type with no form here — rather than describe it
    as something it is not."""
    sig = inspect.signature(func)
    hints = t.get_type_hints(func)
    props: t.Dict[str, t.Any] = {}
    required: t.List[str] = []
    issues: t.List[str] = []

    for p in sig.parameters.values():
        if p.name == "self":
            continue
        if p.kind in (p.VAR_POSITIONAL, p.VAR_KEYWORD):
            issues.append(f"variadic parameter '{p.name}' is not supported")
            continue
        if p.name not in hints:
            issues.append(f"parameter '{p.name}' lacks an explicit annotation")
            continue
        try:
            props[p.name] = _type_to_schema(hints[p.name])
        except _NoSchema:
            named = _named(hints[p.name])
            issues.append(f"parameter '{p.name}' has type {named}, which JSON Schema has no form for here")
            continue
        if p.default is p.empty:
            required.append(p.name)

    if issues:
        raise TypeError(
            "\n • ".join([f"{func.__name__} — schema generation failed:"] + issues)
        )

    return {"type": "object", "properties": props, "required": required}
