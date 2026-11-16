# -*- coding: utf-8 -*-
"""A bounded call space described as data, generated into concrete calls, and checked.

Words this module assumes:

- **domain** — the finite set of values one argument takes. A *manual* domain is a set of atoms
  chosen by hand, with the reason they were chosen; a *generated* domain is expanded from declared
  factors and bounds by a factory (every combination, duplicates dropped).
- **model** — one function's parameters, each with a domain, and the exclusions: combinations that
  are not calls of the model, each with its reason.
- **call space** — the product of the parameters' domains, less what the exclusions remove.

Two code paths, so one checks the other. ``generate`` expands the space depth-first, the way the
C++ reflection probe does, and is what the tests execute. ``required`` rebuilds it from the model's
declaration with ``itertools.product``; ``check`` compares a run — the cases executed and the
domains the generator advertised — with that rebuild, so a missing, duplicated or unexpected call
and a domain that shrank (even consistently with its cases) are all reported.

Domain values are data (strings, numbers, tuples): a value that must be an object — a class, a
code object — is made from its data by a factory when the case runs, fresh for every case.
"""

from __future__ import annotations

import hashlib
import itertools
import json
from dataclasses import dataclass, field
from typing import Callable

# ── domains ──────────────────────────────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class Atoms:
    """A manual domain: values chosen by hand, and why these."""

    name: str
    values: tuple
    why: str
    provenance = "manual"

    def expand(self):
        return tuple(self.values)

    def describe(self):
        return {"name": self.name, "provenance": self.provenance, "why": self.why, "count": len(self.values)}


@dataclass(frozen=True)
class Grammar:
    """A generated domain: every combination of the factors' values made into one value by `make`,
    in factor order, duplicates dropped where first seen — less the combinations `keep` refuses,
    which `dropped` says why."""

    name: str
    factors: tuple  # ((factor name, domain), ...)
    make: Callable
    why: str
    keep: Callable = None
    dropped: str = ""
    provenance = "generated"

    def expand(self):
        seen = {}
        for combination in itertools.product(*(domain.expand() for _, domain in self.factors)):
            if self.keep is None or self.keep(*combination):
                seen.setdefault(self.make(*combination), None)
        return tuple(seen)

    def describe(self):
        described = {"name": self.name, "provenance": self.provenance, "why": self.why, "count": len(self.expand()),
                     "factors": [{"factor": factor, **domain.describe()} for factor, domain in self.factors]}
        if self.keep is not None:
            described["dropped"] = self.dropped
        return described


@dataclass(frozen=True)
class Either:
    """A domain made of other domains' values, in order, duplicates dropped where first seen."""

    name: str
    parts: tuple
    why: str
    provenance = "generated"

    def expand(self):
        return tuple(dict.fromkeys(value for part in self.parts for value in part.expand()))

    def describe(self):
        return {"name": self.name, "provenance": self.provenance, "why": self.why, "count": len(self.expand()),
                "parts": [part.describe() for part in self.parts]}


@dataclass(frozen=True)
class Sequences:
    """A generated domain: every sequence of `element` values of length 0 to `max_length`."""

    name: str
    element: object
    max_length: int
    why: str
    provenance = "generated"

    def expand(self):
        atoms = self.element.expand()
        result, layer = [()], [()]
        for _ in range(self.max_length):
            layer = [prefix + (atom,) for prefix in layer for atom in atoms]
            result += layer
        return tuple(result)

    def describe(self):
        return {"name": self.name, "provenance": self.provenance, "why": self.why, "count": len(self.expand()),
                "max_length": self.max_length, "element": self.element.describe()}


# ── the model ────────────────────────────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class Parameter:
    name: str
    domain: object


@dataclass(frozen=True)
class Exclusion:
    """Combinations that are not calls of the model: `applies(arguments)` says which, `reason` why."""

    reason: str
    applies: Callable


@dataclass(frozen=True)
class Obligation:
    """What a reduced space must still contain: `unmet(cases)` lists what is missing from it."""

    name: str
    unmet: Callable


@dataclass(frozen=True)
class Model:
    name: str
    call: str
    parameters: tuple
    exclusions: tuple = ()
    obligations: tuple = ()
    notes: tuple = ()

    def excluded_by(self, arguments):
        """The first exclusion that removes `arguments`, or None."""
        return next((e for e in self.exclusions if e.applies(arguments)), None)


def key(arguments):
    """A call's arguments, canonically: what the checker compares calls by."""
    return json.dumps(arguments, sort_keys=True, ensure_ascii=False, default=repr)


def case_id(model, arguments):
    """A case's id: stable across runs and orderings, as it comes from the arguments alone."""
    return f"{model.name.rsplit('.', 1)[-1]}-{hashlib.sha1(key(arguments).encode()).hexdigest()[:10]}"


@dataclass(frozen=True)
class Case:
    id: str
    arguments: dict


def generate(model):
    """The model's calls, expanded depth-first over its parameters; excluded combinations dropped."""
    domains = [(p.name, p.domain.expand()) for p in model.parameters]

    def expand(index, arguments):
        if index == len(domains):
            if model.excluded_by(arguments) is None:
                yield Case(case_id(model, arguments), dict(arguments))
            return
        name, values = domains[index]
        for value in values:
            arguments[name] = value
            yield from expand(index + 1, arguments)
        del arguments[name]

    return list(expand(0, {}))


@dataclass
class Run:
    """What a generator advertised and what was executed: one record per case."""

    model: str
    advertised: dict  # parameter name -> the domain values the generator expanded
    records: list = field(default_factory=list)  # dicts: id, arguments, expected, oracle, outcome, passed
    count: int = 0


def advertise(model):
    return {p.name: list(p.domain.expand()) for p in model.parameters}


# ── the checker: the declaration rebuilt, independently of generate() ──────────────────────────


def rebuild(domain):
    """`domain`'s values from its declaration, by a second implementation."""
    if isinstance(domain, Atoms):
        return list(domain.values)
    if isinstance(domain, Grammar):
        values = [domain.make(*combination)
                  for combination in itertools.product(*(rebuild(d) for _, d in domain.factors))
                  if domain.keep is None or domain.keep(*combination)]
        return list(dict.fromkeys(values))
    if isinstance(domain, Either):
        return list(dict.fromkeys(itertools.chain.from_iterable(rebuild(part) for part in domain.parts)))
    if isinstance(domain, Sequences):
        atoms = rebuild(domain.element)
        return [combination for length in range(domain.max_length + 1)
                for combination in itertools.product(atoms, repeat=length)]
    raise TypeError(f"no rebuild for a {type(domain).__name__} domain")


def required(model):
    """{key: arguments} of every call the model requires, and {reason: count} of what it excludes."""
    names = [p.name for p in model.parameters]
    calls, excluded = {}, {}
    for values in itertools.product(*(rebuild(p.domain) for p in model.parameters)):
        arguments = dict(zip(names, values))
        exclusion = model.excluded_by(arguments)
        if exclusion is None:
            calls[key(arguments)] = arguments
        else:
            excluded[exclusion.reason] = excluded.get(exclusion.reason, 0) + 1
    return calls, excluded


def _same(a, b):
    return key(a) == key(b)


def check(model, run):
    """Issues with `run` against `model`'s declaration; empty when the run is the whole space."""
    issues = []
    for p in model.parameters:
        declared = rebuild(p.domain)
        advertised = run.advertised.get(p.name)
        if advertised is None:
            issues.append({"kind": "domain_not_advertised", "parameter": p.name})
            continue
        if len({key(v) for v in advertised}) != len(advertised):
            issues.append({"kind": "duplicate_domain_values", "parameter": p.name})
        declared_keys, advertised_keys = {key(v) for v in declared}, {key(v) for v in advertised}
        if declared_keys - advertised_keys:
            issues.append({"kind": "domain_shrunk", "parameter": p.name,
                           "missing": sorted(declared_keys - advertised_keys)[:3],
                           "count": len(declared_keys - advertised_keys)})
        if advertised_keys - declared_keys:
            issues.append({"kind": "domain_grew", "parameter": p.name,
                           "extra": sorted(advertised_keys - declared_keys)[:3]})
    calls, _ = required(model)
    executed = [key(r["arguments"]) for r in run.records]
    missing = sorted(set(calls) - set(executed))
    unexpected = sorted(set(executed) - set(calls))
    if missing:
        issues.append({"kind": "missing_calls", "count": len(missing), "example": missing[0]})
    if unexpected:
        issues.append({"kind": "unexpected_calls", "count": len(unexpected), "example": unexpected[0]})
    if len(set(executed)) != len(executed):
        issues.append({"kind": "duplicate_calls", "count": len(executed) - len(set(executed))})
    ids = [r["id"] for r in run.records]
    if len(set(ids)) != len(ids):
        issues.append({"kind": "duplicate_ids"})
    if run.count != len(run.records):
        issues.append({"kind": "count_mismatch", "advertised": run.count, "records": len(run.records)})
    failed = [r for r in run.records if not r["passed"] and not r.get("divergence")]
    if failed:
        issues.append({"kind": "oracle_failures", "count": len(failed), "example": failed[0]})
    resolved = [r for r in run.records if r["passed"] and r.get("divergence")]
    if resolved:  # a known divergence that no longer happens: the model must say so
        issues.append({"kind": "divergence_not_observed", "count": len(resolved), "example": resolved[0]})
    for obligation in model.obligations:
        unmet = obligation.unmet([r["arguments"] for r in run.records])
        if unmet:
            issues.append({"kind": "unmet_obligation", "obligation": obligation.name, "unmet": unmet[:3]})
    return issues


def summary(model, run):
    """The numbers a report states about one model and one run."""
    calls, excluded = required(model)
    product = 1
    for p in model.parameters:
        product *= len(rebuild(p.domain))
    oracles, divergences = {}, {}
    for record in run.records:
        oracles[record["oracle"]] = oracles.get(record["oracle"], 0) + 1
        if record.get("divergence") and not record["passed"]:
            divergences[record["divergence"]] = divergences.get(record["divergence"], 0) + 1
    return {"model": model.name, "product": product, "excluded": excluded, "required": len(calls),
            "executed": len(run.records), "passed": sum(r["passed"] for r in run.records),
            "oracles": dict(sorted(oracles.items())), "divergences": divergences, "issues": check(model, run)}


# ── the readable view: one input changed at a time, from the generated cases ───────────────────


def one_input_comparisons(model, records, baseline):
    """For each parameter, the executed cases that differ from `baseline`'s arguments in that
    parameter alone — an explanation of one input's effect, drawn from the generated product."""
    groups = []
    for p in model.parameters:
        rows = [r for r in records
                if all(_same(r["arguments"][q.name], baseline[q.name]) for q in model.parameters if q.name != p.name)]
        if len({key(r["arguments"][p.name]) for r in rows}) > 1:
            groups.append({"varied": p.name, "fixed": {q.name: baseline[q.name] for q in model.parameters
                                                       if q.name != p.name}, "rows": rows})
    return groups
