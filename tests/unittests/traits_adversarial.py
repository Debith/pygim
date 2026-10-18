# -*- coding: utf-8 -*-
"""Adversarial call spaces for pygim.traits: hostile and unusual inputs, generated from declared
domains, with the state a call may change modelled explicitly.

The threat boundary. compose() and CodeBuilder manipulate executable Python objects; they are not a
sandbox for arbitrary Python. The guarantees these cases check are: compose() does not crash the
interpreter on any input (native safety); on success only the documented target attributes and the
provenance record change; on a documented refusal the target is left exactly as it was; and neither
a refusal nor a hostile hook leaves stale bookkeeping that a later composition would trip over. A
legitimate name is never refused merely for looking unusual.

Each case is one compose() call generated from callspace domains. Its expected outcome comes from an
oracle independent of pygim: the documented refusals (the record name, a name the target already
owns, a non-str name) and, for whether an attribute name is settable at all, Python's own setattr on
a throwaway class — never from what compose() did. State is read through trusted projections only —
names and object identity, never a value's __eq__, __repr__ or __hash__ — so the snapshot itself
cannot be fooled by a hostile object, and sentinels on unrelated classes catch collateral writes.

Resource-abuse and crash-risk families (deep code nesting, huge trait sets) are not run here: they
live in bounded subprocesses (test_traits_regressions.py via bounded_child.py). Hostile-protocol
hooks that raise or re-enter are covered by authored seeds, not generated, because their outcome is
the open rollback policy question (docs/design/traits.md), not a settled contract.
"""

from __future__ import annotations

import functools

from callspace import Atoms, Exclusion, Model, Parameter, Run, advertise, generate
from pygim.traits import compose, traits_of

RECORD = "__pygim_traits__"

# ── adversarial domains ───────────────────────────────────────────────────────────────────────

NAMES = Atoms(
    "adversarial names",
    (
        "greet",                 # legitimate baseline: must not be refused for sitting among hostile ones
        "__init__",              # a legitimate dunder method name — not machinery, must compose
        "greet\x00x",            # an embedded NUL: the whole name is the attribute, not "greet"
        RECORD,                  # collides with compose()'s own bookkeeping
        "__dict__",              # a type slot no class can hold as a member (excluded, not expressible)
        "__class__",             # a slot Python forbids setting to a function (excluded)
        "__slots__",             # placeable post-creation as a plain attribute; must compose harmlessly
        "_" * 40,                # a long run of underscores (not a dunder by enum._is_dunder)
        "x" * 300,               # a long name
        "ɡreet",            # a confusable (Latin small script g) — a different name from "greet"
        "café",            # a combining mark: distinct from the precomposed form
        "é",                     # a non-ASCII but ordinary identifier character
    ),
    why="one of each name a caller might pass that could confuse the write, the conflict check or the "
        "record: a NUL that truncates, the record's own name, reserved type slots, over-long and "
        "non-ASCII names, and two legitimate names that must pass",
)
TARGETS = Atoms("targets", ("empty", "has greet", "has base"),
                why="a bare class; one that already defines the baseline name; one with a base, so an "
                    "inherited name is not mistaken for an owned one")
KINDS = Atoms("member kinds", ("function", "property"),
              why="the two kinds whose write path differs; the others reduce to a function inside")
OVERRIDE = Atoms("override", (False, True), why="both")

SIBLING_SENTINEL = "untouched"  # a lone attribute on an unrelated class; a stray write would change it


def _function(name):
    def member(self):
        return name
    member.__name__ = "m"
    member.__qualname__ = "Trait.m"
    return member


def _member(name, kind):
    value = _function(name)
    return property(value) if kind == "property" else value


@functools.lru_cache(maxsize=None)
def placeable(name):
    """Whether `name` can be a member of a class at all — some type slots (``__dict__``, ``__class__``)
    cannot, by Python's own rules, so no trait can carry them and compose() never meets them."""
    try:
        setattr(type("Probe", (), {"__module__": "p"}), name, _function(name))
        return True
    except Exception:  # noqa: BLE001
        return False


ADVERSARIAL = Model(
    name="pygim.traits.compose: adversarial names and state",
    call="compose(target, Trait{name: member}, override=override); traits_of(target)",
    parameters=(Parameter("target", TARGETS), Parameter("name", NAMES), Parameter("kind", KINDS),
                Parameter("override", OVERRIDE)),
    exclusions=(Exclusion("not expressible as a class member (Python forbids the slot)",
                          lambda a: not placeable(a["name"])),),
    notes=("Each call composes one member of the given name and kind. The expected outcome is from the "
           "documented refusals and Python's own setattr, never from compose(). State is checked by "
           "trusted projections (names and identity) with sentinels on unrelated classes.",),
)


def make_trait(name, kind):
    """A fresh trait carrying exactly one member of `name`, placed after creation so a slot name like
    ``__slots__`` is a plain attribute, not interpreted by the class machinery."""
    trait = type("Trait", (), {"__module__": "traits_adversarial.generated"})
    setattr(trait, name, _member(name, kind))
    return trait


def make_target(kind):
    """A fresh target and an unrelated sentinel class whose state must never change."""
    if kind == "empty":
        target = type("Target", (), {"__module__": "t"})
    elif kind == "has greet":
        target = type("Target", (), {"greet": _function("own"), "__module__": "t"})
    else:
        base = type("Base", (), {"inherited": _function("base")})
        target = type("Target", (base,), {"__module__": "t"})
    sibling = type("Sibling", (), {"keep": SIBLING_SENTINEL})
    return target, sibling


@functools.lru_cache(maxsize=None)
def _settable(name):
    """Python's own answer to whether `name` can be an attribute of an ordinary class — the oracle for
    which reserved slots a write is allowed to touch, independent of pygim."""
    probe = type("Probe", (), {})
    try:
        setattr(probe, name, _function(name))
        return None
    except Exception as error:  # noqa: BLE001 — the exception type is the oracle's answer
        return type(error).__name__


def expected(target_kind, name, override):
    """(outcome kind, the one name allowed to change) the contract predicts, with no call made.
    outcome is "compose" or an exception name; the second item is the attribute compose() may write
    (the member), always besides the record it keeps."""
    owned = {"empty": set(), "has greet": {"greet"}, "has base": set()}[target_kind]
    # A plain class always owns these type slots in its own dict (so composing one is "already has"):
    owned |= {"__doc__", "__module__", "__dict__", "__weakref__"}
    if name == RECORD:
        return "ValueError", None             # a trait cannot define the record
    if name in owned and not override:
        return "ValueError", None             # the target already has it
    forbidden = _settable(name)               # Python itself refuses some slots whatever compose does
    if forbidden is not None:
        return forbidden, None
    return "compose", name


# ── trusted projections of state ─────────────────────────────────────────────────────────────────


def own_names(cls):
    """The names in a class's own dict and the identity of each value — no user hook is called."""
    return {n: id(v) for n, v in dict(vars(cls)).items()}


def record_names(cls):
    """The names in a class's trait record, in order — TraitRecord's own iteration, no user hook."""
    return list(traits_of(cls))


def run_case(arguments, composer=compose):
    """(outcome, expectation, oracle): compose one adversarial member, reading state by projection."""
    target, sibling = make_target(arguments["target"])
    trait = make_trait(arguments["name"], arguments["kind"])
    before, trait_before, sibling_before = own_names(target), own_names(trait), own_names(sibling)
    kind, allowed = expected(arguments["target"], arguments["name"], arguments["override"])

    try:
        returned = composer(target, trait, override=arguments["override"])
        raised = None
    except Exception as error:  # noqa: BLE001 — what is raised is the outcome
        returned, raised = None, type(error).__name__

    after = own_names(target)
    added = {n for n in after if n not in before or after[n] != before[n]}
    outcome = {
        "raised": raised,
        "returned target": returned is target,
        "names changed": tuple(sorted(added - {RECORD})),
        "record written": RECORD in after,
        "in record": arguments["name"] in record_names(target),
        "trait untouched": own_names(trait) == trait_before,
        "sibling untouched": own_names(sibling) == sibling_before and sibling.keep == SIBLING_SENTINEL,
        "retry then good compose works": _cleanup_holds(target, arguments["name"]),
    }
    if kind == "compose":
        want = {"raised": None, "returned target": True, "names changed": (arguments["name"],),
                "record written": True, "in record": True, "trait untouched": True,
                "sibling untouched": True, "retry then good compose works": True}
        oracle = "contract+python"
    else:
        want = {"raised": kind, "returned target": False, "names changed": (), "record written": False,
                "in record": False, "trait untouched": True, "sibling untouched": True,
                "retry then good compose works": True}
        oracle = "contract+python"
    return outcome, want, oracle


def _cleanup_holds(target, composed_name):
    """After the call, a known-good composition onto the same target must still work and record its
    member — no refusal or partial write may leave stale bookkeeping behind."""
    good = type("Good", (), {"wave": _function("wave"), "__module__": "t"})
    try:
        compose(target, good, override=True)
    except Exception:  # noqa: BLE001
        return False
    record = record_names(target)
    return "wave" in record and traits_of(target)["wave"].trait.endswith("Good")


MODELS = {ADVERSARIAL.name: (ADVERSARIAL, run_case)}


# ── the case-family inventory ──────────────────────────────────────────────────────────────────
# Every adversarial family the request named, and how it is covered here: generated (a model),
# seeded (authored cases), subprocess (a bounded child), or not covered, each with the reason.

FAMILIES = (
    ("Names: NUL, confusables, combining marks, long underscore runs and names, non-ASCII, "
     "reserved type slots, collision with the record name", "generated",
     "traits_adversarial.ADVERSARIAL — the name domain; reserved slots Python forbids as members are "
     "excluded with that reason, and the record name and owned slots are refusals."),
    ("Names: non-str keys and lone surrogates", "generated+seeded",
     "A non-str key and a surrogate name are refused by compose() (TypeError / UnicodeEncodeError); "
     "the surrogate is in the name domain, the non-str key is an authored seed."),
    ("Identity and aliasing: distinct classes with identical names, repeated traits, an aliased "
     "function, inherited conflicts, independent copies per target", "seeded",
     "test_traits_adversarial.TestIdentityAndAliasing — these are relationships between whole classes, "
     "not one member's name, so they are authored, not generated."),
    ("Hostile protocol objects: a metaclass __setattr__ that raises or deletes the record mid-write",
     "seeded+subprocess",
     "test_traits_regressions (the record-deleting hook, in a bounded child) and the state model's "
     "cleanup obligation. A hook that raises mid-write is the open rollback policy question "
     "(docs/design/traits.md), so its partial-write outcome is surfaced, not asserted as a contract."),
    ("Code objects: nested functions, lambdas, closures, __class__/super cells, private locals vs "
     "attributes, reown chains", "generated",
     "traits_callspace.py — the CodeBuilder one-step and two-step models and the compose member bodies; "
     "a private local name is the known divergence recorded in traits.md."),
    ("Resource abuse: code nested past the recursion limit, and (bounded) large inputs", "subprocess",
     "test_traits_regressions runs the deep-nesting case in a bounded child; it now raises "
     "RecursionError instead of crashing. Wide trait sets and many compositions are not yet modelled."),
    ("Native memory safety of the C++ naming rules", "sanitizer",
     "tests/static/run_python_names_sanitizer.py drives utils/python_names.h over a seed corpus with "
     "bounded mutations under ASan+UBSan, with a planted-OOB negative control. No coverage-guided "
     "fuzzer is installed (no libFuzzer/AFL), so there is no feedback-driven search; this is reported, "
     "not faked."),
)


@functools.lru_cache(maxsize=None)
def runs():
    """The adversarial model run against pygim, once per process."""
    model, case_function = MODELS[ADVERSARIAL.name]
    run = Run(model=model.name, advertised=advertise(model))
    for case in generate(model):
        outcome, want, oracle = case_function(case.arguments)
        run.records.append({"id": case.id, "arguments": case.arguments, "expected": want, "oracle": oracle,
                            "outcome": outcome, "passed": outcome == want})
    run.count = len(run.records)
    return {model.name: run}
