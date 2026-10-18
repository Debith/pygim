# -*- coding: utf-8 -*-
"""The generated call spaces of pygim.traits and pygim.utils.is_dunder, called whole and checked.

Each model (traits_callspace.py) is expanded into every call and run against pygim; the checker
(callspace.py) rebuilds the space from the model's declaration and reports what the run missed,
duplicated or added, a domain that shrank, and every outcome that differs from Python's answer.
The negative controls show the checker and the oracles can fail: a deleted case, a domain shrunk
consistently with its cases, and behavioural defects put in place of the real implementation.
"""

import copy
import dataclasses
import types

import pytest

import callspace
import python_names_callspace
import traits_callspace
from pygim import traits as pygim_traits
from pygim import utils
from pygim.traits import CodeBuilder, compose, traits_of
from traits_callspace import DIVERGENCES, MODELS, execute, runs

MODEL_NAMES = list(MODELS)


@pytest.mark.parametrize("name", MODEL_NAMES)
def test_the_whole_space_is_called_and_holds(name):
    model, _ = MODELS[name]
    issues = callspace.check(model, runs()[name])
    assert not issues, issues


@pytest.mark.parametrize("divergence", DIVERGENCES, ids=lambda d: d.model)
def test_each_known_divergence_still_happens(divergence):
    """A divergence names real failing cases; check() reports one that stopped failing."""
    records = [r for r in runs()[divergence.model].records if r.get("divergence") == divergence.finding]
    assert records and not any(r["passed"] for r in records)


def test_the_classes_steps_name_were_left_unchanged():
    """CodeBuilder's cases share one class per qualified name (traits_callspace.step_class): sound only
    because CodeBuilder reads their names and writes nothing; this says it wrote nothing."""
    runs()
    for qualname in traits_callspace.CLASSES.expand():
        cls = traits_callspace.step_class(qualname)
        assert set(vars(cls)) == {"__module__", "__dict__", "__weakref__", "__doc__"}
        assert cls.__qualname__ == qualname


# ── negative controls: the checker ───────────────────────────────────────────────────────────────


@pytest.mark.parametrize("name", ["pygim.traits.CodeBuilder: one step", "pygim.traits.compose: two traits"])
def test_a_deleted_case_is_missed_even_with_its_count_adjusted(name):
    model, _ = MODELS[name]
    run = copy.deepcopy(runs()[name])
    gone = run.records.pop(len(run.records) // 2)
    run.count -= 1
    issues = callspace.check(model, run)
    assert [i["kind"] for i in issues] == ["missing_calls"]
    assert issues[0]["example"] == callspace.key(gone["arguments"])


@pytest.mark.parametrize("name, parameter, dropped", [
    ("pygim.traits.CodeBuilder: refusals", "wrong", "none"),
    ("pygim.traits.compose: one trait", "target", "enum"),
])
def test_a_domain_shrunk_with_its_cases_is_still_caught(name, parameter, dropped):
    """The generator runs on a model with one atom fewer — its cases, ids and count all agree with
    the smaller domain — and the checker, holding the declared model, still finds the hole."""
    model, case_function = MODELS[name]
    shrunk = dataclasses.replace(model, parameters=tuple(
        dataclasses.replace(p, domain=callspace.Atoms(p.domain.name, tuple(v for v in p.domain.expand() if v != dropped),
                                                      p.domain.why)) if p.name == parameter else p
        for p in model.parameters))
    run = execute(shrunk, case_function)
    assert run.count == len(run.records) and not callspace.check(shrunk, run)  # consistent with itself
    kinds = {i["kind"] for i in callspace.check(model, run)}
    assert {"domain_shrunk", "missing_calls"} <= kinds


# ── negative controls: behavioural defects, put in place of the implementation ─────────────────


class FirstStepOnly:
    """A CodeBuilder that keeps only the first step — one that replaces instead of appending."""

    def __init__(self, code):
        self._builder, self._steps = CodeBuilder(code), 0

    def reown(self, source, target):
        if not self._steps:
            self._builder.reown(source, target)
        self._steps += 1
        return self

    def build(self):
        return self._builder.build()


class OuterOnly:
    """A CodeBuilder that rewrites the outer code object but not the code nested in it."""

    def __init__(self, code):
        self._code, self._builder = code, CodeBuilder(code)

    def reown(self, source, target):
        self._builder.reown(source, target)
        return self

    def build(self):
        return self._builder.build().replace(co_consts=self._code.co_consts)


def copying_compose(target, *traits, override=False):
    """compose() as a first attempt writes it: the documented refusals, then every member set on the
    target as it is — not rewritten for the target."""
    plan = {}
    if not isinstance(target, type):
        raise TypeError("compose() takes a class")
    for trait in traits:
        if not isinstance(trait, type):
            raise TypeError(f"compose takes classes as traits, not {type(trait).__name__}")
        name_of_trait = f"{trait.__module__}.{trait.__qualname__}"
        for name, member in vars(trait).items():
            if utils.is_dunder(name):
                continue
            if not isinstance(member, (types.FunctionType, property, classmethod, staticmethod)):
                raise TypeError(f"{trait.__qualname__}.{name} is {type(member).__name__}; a trait composes functions, "
                                "properties, classmethods and staticmethods")
            if name in plan and not override:
                raise ValueError(f"{plan[name][0]} and {name_of_trait} both define {name} "
                                 "(pass override=True to let the later win)")
            if name not in plan and name in vars(target) and not override:
                raise ValueError(f"{target.__qualname__} already has {name}; {name_of_trait} would replace it "
                                 "(pass override=True)")
            plan.pop(name, None)
            plan[name] = (name_of_trait, member)
    for name, (_, member) in plan.items():
        setattr(target, name, member)
    return target


def old_is_dunder(name):
    """pygim's dunder rule before it matched enum._is_dunder."""
    return len(name) > 4 and name.startswith("__") and name.endswith("__")


@pytest.mark.parametrize("name, implementation, caught_by", [
    ("pygim.traits.CodeBuilder: two steps", {"builder": FirstStepOnly}, "the step-by-step oracle"),
    ("pygim.traits.CodeBuilder: one step", {"builder": OuterOnly}, "the step-by-step oracle"),
    ("pygim.traits.compose: one trait", {"composer": copying_compose}, "the members written in the target"),
    ("pygim.utils.is_dunder", {"function": old_is_dunder}, "enum._is_dunder"),
], ids=["builder keeps one step", "builder skips nested code", "compose copies members unrewritten",
        "is_dunder's old rule"])
def test_a_behavioural_defect_is_caught(name, implementation, caught_by):
    model, case_function = MODELS[name]
    run = execute(model, case_function, **implementation)
    failures = [r for r in run.records if not r["passed"]]
    assert failures, f"{caught_by} let the defect through"
    assert {i["kind"] for i in callspace.check(model, run)} & {"oracle_failures", "divergence_not_observed"}
    assert not [i for i in callspace.check(model, run) if i["kind"].startswith(("missing", "unexpected", "domain"))]
    if "composer" in implementation:  # caught by behaviour, not only by the missing record
        assert any(r["outcome"].get("members") != r["expected"].get("members") for r in failures)


# ── drift: the public surface the models cover ───────────────────────────────────────────────────

# pybind11 gives no inspect.signature, only the docstring's first line: the declared signatures.
SIGNATURES = [
    ("compose", compose, "compose(target: type, *args, override: bool = False) -> type"),
    ("traits_of", traits_of, "traits_of(cls: type) -> object"),
    ("CodeBuilder", CodeBuilder.__init__, "__init__(self: pygim.traits.CodeBuilder, code: object) -> None"),
    ("CodeBuilder.reown", CodeBuilder.reown,
     "reown(self: pygim.traits.CodeBuilder, source: type, target: type) -> pygim.traits.CodeBuilder"),
    ("CodeBuilder.build", CodeBuilder.build, "build(self: pygim.traits.CodeBuilder) -> object"),
    ("is_dunder", utils.is_dunder, "is_dunder(name: str) -> bool"),
]
COVERED = {"CodeBuilder": "its models", "compose": "its models", "traits_of": "compose()'s models",
           "Provenance": "a result, in compose()'s models", "TraitRecord": "a result, in compose()'s models"}


def test_every_public_name_of_pygim_traits_is_modelled():
    assert {n for n in dir(pygim_traits) if not n.startswith("_")} == set(COVERED)


@pytest.mark.parametrize("name, function, signature", SIGNATURES, ids=[s[0] for s in SIGNATURES])
def test_each_modelled_signature_is_the_one_declared(name, function, signature):
    assert function.__doc__.strip().splitlines()[0] == signature


def test_the_every_build_proof_is_the_model_s():
    """tests/static/python_names_callspace_proofs.cpp is rendered from python_names_callspace."""
    assert python_names_callspace.PROOF.read_text(encoding="utf-8") == python_names_callspace.render_proof(), (
        "stale: run python tests/unittests/python_names_callspace.py")
