import inspect
import typing as t


###############################################################################
# JSON‑Schema helpers
###############################################################################

def _type_to_schema(tp: t.Any) -> t.Dict[str, t.Any]:
    """Minimal Python‑type → JSON‑Schema mapping."""
    origin = t.get_origin(tp) or tp
    if origin is int:
        return {"type": "integer"}
    if origin is float:
        return {"type": "number"}
    if origin is bool:
        return {"type": "boolean"}
    if origin is str:
        return {"type": "string"}
    if origin is list:
        args = t.get_args(tp) or (t.Any,)
        return {"type": "array", "items": _type_to_schema(args[0])}
    return {"type": "string"}


def get_schema(func: t.Callable[..., t.Any]) -> t.Dict[str, t.Any]:
    """Return JSON‑Schema for *func*'s parameters, raising if issues exist."""
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
        props[p.name] = _type_to_schema(hints[p.name])
        if p.default is p.empty:
            required.append(p.name)

    if issues:
        raise TypeError("\n • ".join([f"{func.__name__} — schema generation failed:"] + issues))

    return {"type": "object", "properties": props, "required": required}