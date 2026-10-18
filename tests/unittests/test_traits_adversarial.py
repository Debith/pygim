# -*- coding: utf-8 -*-
"""Adversarial and state-preservation tests for pygim.traits.

The generated model (traits_adversarial.py) composes one hostile or unusual member per case and
checks, by trusted projection, that the call does what the contract says and changes nothing it
should not. The defect controls here substitute composers that violate one obligation each, so the
checks are shown to bite. The authored seeds cover the identity and aliasing family, which is about
relationships between whole classes rather than one member's name.
"""

import pytest

import callspace
import traits_adversarial
from pygim.traits import compose, traits_of
from traits_adversarial import ADVERSARIAL, make_target, make_trait, run_case, runs


def test_the_whole_adversarial_space_is_checked_and_holds():
    issues = callspace.check(ADVERSARIAL, runs()[ADVERSARIAL.name])
    assert not issues, issues


def test_every_legitimate_name_is_composed_not_refused():
    """The boundary: a name is never refused merely for looking unusual."""
    run = runs()[ADVERSARIAL.name]
    legitimate = {"greet", "__init__", "greet\x00x", "__slots__", "_" * 40, "x" * 300,
                  "ɡreet", "café", "é"}
    composed = {r["arguments"]["name"] for r in run.records if r["outcome"]["raised"] is None}
    assert legitimate <= composed


# ── defect controls: each substituted composer violates one obligation, which must be caught ──────

def _run_with(composer):
    """The adversarial model run against a broken composer; the cases it should now fail."""
    model, _ = traits_adversarial.MODELS[ADVERSARIAL.name]
    run = callspace.Run(model=model.name, advertised=callspace.advertise(model))
    for case in callspace.generate(model):
        outcome, want, oracle = run_case(case.arguments, composer=composer)
        run.records.append({"id": case.id, "arguments": case.arguments, "expected": want,
                            "oracle": oracle, "outcome": outcome, "passed": outcome == want})
    run.count = len(run.records)
    return run


def _leaky_compose(target, *traits, override=False):
    result = compose(target, *traits, override=override)
    target._leaked = True  # an attribute no case asked for
    return result


def _forgetful_compose(target, *traits, override=False):
    """Writes the members but drops the provenance record."""
    before = dict(vars(target))
    result = compose(target, *traits, override=override)
    if "__pygim_traits__" in vars(target) and "__pygim_traits__" not in before:
        delattr(target, "__pygim_traits__")
    return result


def _trait_mutating_compose(target, *traits, override=False):
    for trait in traits:
        if isinstance(trait, type):
            trait._touched = True  # a write into the source trait
    return compose(target, *traits, override=override)


@pytest.mark.parametrize("composer, obligation", [
    (_leaky_compose, "names changed"),
    (_forgetful_compose, "record written"),
    (_trait_mutating_compose, "trait untouched"),
], ids=["an extra write is caught", "a dropped record is caught", "a mutated trait is caught"])
def test_a_state_defect_is_caught(composer, obligation):
    run = _run_with(composer)
    failures = [r for r in run.records if not r["passed"]]
    assert failures, f"the {obligation} obligation did not catch {composer.__name__}"
    assert any(r["outcome"][obligation] != r["expected"][obligation] for r in failures)
    assert {i["kind"] for i in callspace.check(ADVERSARIAL, run)} & {"oracle_failures"}


def test_a_stale_bookkeeping_defect_is_caught():
    """A composer that leaves a half-built record behind on refusal must fail the cleanup obligation."""
    class Meta(type):
        pass

    def _stale(target, *traits, override=False):
        try:
            return compose(target, *traits, override=override)
        except Exception:
            # Leave a record of a member that was never written: stale bookkeeping.
            compose(target, make_trait("ghost", "function"), override=True)
            delattr(target, "ghost")
            raise

    run = _run_with(_stale)
    # On the refusal cases, a later good compose must still work; the ghost attribute is gone but its
    # record entry lingers, which the retry obligation detects as the record and the class disagreeing.
    refused = [r for r in run.records if r["expected"]["raised"] is not None]
    assert refused and any(not r["passed"] for r in refused)


# ── authored seeds: identity and aliasing (family 2) ─────────────────────────────────────────────

def _m(value):
    def member(self):
        return value
    return member


class TestIdentityAndAliasing:
    def test_two_distinct_classes_with_the_same_qualname_still_conflict_by_name(self):
        """A conflict is by member name, not by trait identity: two unrelated traits that happen to
        share a name (and even a qualname) cannot both define it without override."""
        a = type("Trait", (), {"m": _m("a")})
        b = type("Trait", (), {"m": _m("b")})  # same __name__/__qualname__, different class
        a.__qualname__ = b.__qualname__ = "Trait"
        target = type("Target", (), {})
        with pytest.raises(ValueError, match="both define m"):
            compose(target, a, b)
        assert "m" not in vars(target)

    def test_the_same_function_aliased_in_two_traits_is_still_a_conflict(self):
        """Even one function object shared under the same name by two traits is a name conflict."""
        shared = _m("shared")
        a = type("A", (), {"m": shared})
        b = type("B", (), {"m": shared})
        with pytest.raises(ValueError, match="both define m"):
            compose(type("Target", (), {}), a, b)

    def test_an_inherited_name_is_not_an_owned_name(self):
        """Composing a member the target only inherits is allowed and overrides the inherited one —
        it is not the 'already has' conflict, which is about the target's own namespace."""
        base = type("Base", (), {"greet": _m("base")})
        target = type("Child", (base,), {})
        compose(target, type("Trait", (), {"greet": _m("trait")}))
        assert "greet" in vars(target) and target().greet() == "trait"
        assert list(traits_of(target)) == ["greet"]

    def test_composing_one_trait_into_two_targets_gives_independent_members(self):
        """Each target gets its own rewritten copy; they do not share a function object, so neither
        target's bookkeeping or members leak into the other."""
        trait = type("Trait", (), {"greet": _m("hi")})
        first = type("First", (), {})
        second = type("Second", (), {})
        compose(first, trait)
        compose(second, trait)
        assert vars(first)["greet"] is not vars(second)["greet"]
        assert vars(first)["greet"] is not trait.__dict__["greet"]
        assert list(traits_of(first)) == list(traits_of(second)) == ["greet"]
