# -*- coding: utf-8 -*-
"""The call spaces of ``pygim.traits`` — ``CodeBuilder``, ``compose()`` with ``traits_of()`` — and of
``pygim.utils.is_dunder``, as data; their calls generated; their expectations from Python itself.

No Python value can be enumerated, so every class and code object a case needs is made by a declared
factory from data: a source template compiled inside a class named by the case, a class made from
its qualified name, a target made from its source. Classes are made fresh for every case; a code
object, which nothing can change, is compiled once per spec and shared.

What is manual and what is generated:

- manual: the code templates, the kinds of name in them, the owners and classes; the target
  sources, the member kinds and bodies, the member names; the wrong arguments;
- generated: the steps (every move between two classes), the traits (every member body under every
  name), every sequence of traits up to two, and every call — the full product of each model.

Two models are reductions, said in their notes: ``CodeBuilder: two steps`` crosses chains with one
template and the names a step can move; ``compose(): two traits`` crosses two traits with the member
kinds that decide a second trait's effect. Each is the full product of its own domains, which the
checker verifies, and the one-step and one-trait models cover what they leave out.

The expectations, never pygim's answers:

- CodeBuilder: each name and qualified name moved step by step by the name rules' oracle, which is
  Python's compiler (``python_names_callspace``); for a connected chain also the method compiled
  directly inside the final target, compared whole. The refusals: the stub's written contract.
- compose(): what is refused, written and recorded, by the stub's written contract; how each
  composed member behaves, by the same members written into the target's own source and compiled.
- is_dunder: ``enum._is_dunder``.
"""

from __future__ import annotations

import dataclasses
import enum
import functools
import sys
import textwrap
import types

import python_names_callspace as names_oracle
from callspace import Atoms, Either, Exclusion, Grammar, Model, Parameter, Run, advertise, generate
from pygim import utils
from pygim.traits import CodeBuilder, compose, traits_of

GENERATED = __name__ + ".generated"  # the module every class made here says it comes from
HAS_QUALNAME = sys.version_info >= (3, 11)  # co_qualname is Python 3.11's

# ── CodeBuilder ──────────────────────────────────────────────────────────────────────────────────

CODE_TEMPLATES = {
    "attribute": "def f(self):\n    return self.{n}",
    "global": "def f(self):\n    return {n}",
    "lambda": "def f(self):\n    return (lambda: self.{n})()",
    "nested def": "def f(self):\n    def g():\n        return self.{n}\n    return g",
    "import": "def f(self):\n    import {n}",
    "local": "def f(self):\n    {n} = 1\n    return {n}",
}
TEMPLATES = Atoms("code templates", tuple(CODE_TEMPLATES),
                  why="each place a method writes a name: an attribute, a global, inside a lambda and inside a "
                      "nested function, an import, a local variable")
CODE_NAMES = Atoms(
    "names in the code", ("__x", "__x__", "_x", "x", "_Trait__x", "_Target__x", "__pkg.mod"),
    why="one of each kind the name rules tell apart — private, dunder, internal, public, spelt for the owner, "
        "spelt for a target, dotted; the rules themselves are proven over the whole name domain",
)
OWNERS = Atoms("owners of the code", ("Trait", "_Trait", "__"),
               why="the class the code is compiled in: a plain name, a leading underscore mangling drops, only "
                   "underscores (which mangle nothing)")
CLASSES = Atoms("classes", ("Trait", "_Trait", "__", "Target", "Outer.Target"),
                why="a class by its qualified name, its name the last part: each owner, a target, and a target "
                    "nested in another class")
STEP = Grammar("steps", (("source", CLASSES), ("target", CLASSES)), make=lambda source, target: (source, target),
               why="every move from one class to another, itself included")
ONE_STEP = Either("one step or none", (Atoms("no step", (None,), why="build() with nothing to do"), STEP),
                  why="build() alone, or after one reown()")
MOVABLE = Atoms("names a chain can move", ("__x", "_Trait__x", "_Target__x", "x"),
                why="the names a step can respell, and a public one it never does")

CODE_BUILDER_ONE_STEP = Model(
    name="pygim.traits.CodeBuilder: one step",
    call="CodeBuilder(template(name) compiled in class owner).reown(*step).build()",
    parameters=(Parameter("template", TEMPLATES), Parameter("name", CODE_NAMES), Parameter("owner", OWNERS),
                Parameter("step", ONE_STEP)),
    exclusions=(Exclusion("a dotted name is written only by an import",
                          lambda a: "." in a["name"] and a["template"] != "import"),),
)
CODE_BUILDER_CHAINS = Model(
    name="pygim.traits.CodeBuilder: two steps",
    call="CodeBuilder(attribute(name) compiled in class owner).reown(*steps[0]).reown(*steps[1]).build()",
    parameters=(Parameter("name", MOVABLE), Parameter("owner", OWNERS), Parameter("steps[0]", STEP),
                Parameter("steps[1]", STEP)),
    notes=("A reduction: every chain of two steps, crossed with the attribute template only and with the names a "
           "step can move. build() applies the same move to every name of every code object in the tree, which "
           "the one-step model shows over every template and kind of name.",),
)
REFUSED_AT = Atoms("the argument that is wrong", ("code", "source", "target"),
                   why="each argument CodeBuilder takes, wrong alone")
WRONG = Atoms("wrong arguments", ("text", "function", "none", "instance"),
              why="source text instead of code, the function instead of its code, None, an instance instead of its "
                  "class")
CODE_BUILDER_REFUSALS = Model(
    name="pygim.traits.CodeBuilder: refusals",
    call="CodeBuilder(code).reown(source, target), one argument wrong",
    parameters=(Parameter("position", REFUSED_AT), Parameter("wrong", WRONG)),
)


def make_class(qualname):
    """A new, empty class with this qualified name; its name is the last part."""
    cls = type(qualname.rsplit(".", 1)[-1], (), {"__module__": GENERATED})
    cls.__qualname__ = qualname
    return cls


def _class_source(qualname, body):
    """`body` inside ``class <qualname>:`` — nested classes for a dotted qualified name."""
    parts = qualname.split(".")
    lines = []
    for depth, part in enumerate(parts):
        lines.append("    " * depth + f"class {part}:")
    return "\n".join(lines) + "\n" + textwrap.indent(body, "    " * len(parts)) + "\n"


def _codes(code):
    """`code` and every code object nested in it, depth-first."""
    yield code
    for constant in code.co_consts:
        if isinstance(constant, types.CodeType):
            yield from _codes(constant)


@functools.lru_cache(maxsize=None)
def compiled_method(template, name, owner):
    """The code of method ``f`` — template `template` writing `name` — compiled in class `owner`."""
    module = compile(_class_source(owner, CODE_TEMPLATES[template].format(n=name)), f"<{owner}>", "exec",
                     dont_inherit=True)
    return next(c for c in _codes(module) if c.co_name == "f")


def _moved(name, steps):
    for source, target in steps:
        name = names_oracle.expected("reowned", {"from": source.rsplit(".", 1)[-1],
                                                 "to": target.rsplit(".", 1)[-1], "name": name})[0]
    return name


def _requalified(qualname, steps):
    for source, target in steps:
        qualname = names_oracle.expected("requalified", {"from": source, "to": target, "qualname": qualname})[0]
    return qualname


_UNCHANGED = ("co_code", "co_varnames", "co_cellvars", "co_freevars", "co_argcount", "co_flags", "co_name",
              "co_filename", "co_firstlineno")


def _shape(code):
    """What build() promises about a tree: each code object's names and qualified name."""
    tree = list(_codes(code))
    return {"co_names": [list(c.co_names) for c in tree],
            "co_qualname": [c.co_qualname for c in tree] if HAS_QUALNAME else None}


def _rest(code):
    """Everything else about a tree, which build() leaves as it was."""
    return [tuple(getattr(c, field) for field in _UNCHANGED) for c in _codes(code)]


def _direct_applies(name, owner, steps):
    """Whether Python's compiler can answer the whole build directly, by compiling the method in the
    last target: each step moves the code from the class it then belongs to, and the source does not
    write a name already spelt as some class's private name (``self._Trait__x`` compiles to what
    ``self.__x`` does, so no code object can tell which was written; the compiler keeps the one
    written, a move cannot)."""
    classes = {owner} | {c for step in steps for c in step}
    if any(names_oracle.expected("unmangled", {"class_name": c.rsplit(".", 1)[-1], "name": name})[0]
           for c in classes):
        return False
    for source, target in steps:
        if source != owner:
            return False
        owner = target
    return True


@functools.lru_cache(maxsize=None)
def _compiled_in_target(template, name, target):
    code = compiled_method(template, name, target)
    local = [[list(c.co_varnames), list(c.co_cellvars), list(c.co_freevars)] for c in _codes(code)]
    return _shape(code), local


def expected_build(template, name, owner, steps):
    """(the expected tree, its oracle): each name and qualified name moved by the name rules."""
    original = compiled_method(template, name, owner)
    tree = list(_codes(original))
    shape = {"co_names": [[_moved(n, steps) for n in c.co_names] for c in tree],
             "co_qualname": [_requalified(c.co_qualname, steps) for c in tree] if HAS_QUALNAME else None}
    return {"tree": shape, "rest unchanged": True, "original unchanged": True, "built twice, equal": True}, "compiler"


@functools.lru_cache(maxsize=None)
def step_class(qualname):
    """The class a step names. CodeBuilder only reads a class's __name__ and __qualname__, so one class
    per qualified name serves every case; test_traits_callspace checks no run changed any of them."""
    return make_class(qualname)


def build_case(arguments, builder=CodeBuilder, fresh=step_class):
    """(the outcome, the expectation, its oracle, the compiler's direct answer or None) of one call."""
    template = arguments.get("template", "attribute")
    name, owner = arguments["name"], arguments["owner"]
    steps = ([arguments["step"]] if arguments.get("step") else []) if "step" in arguments else \
        [arguments["steps[0]"], arguments["steps[1]"]]
    code = compiled_method(template, name, owner)
    before = _shape(code), _rest(code)
    made = builder(code)
    for source, target in steps:
        made = made.reown(fresh(source), fresh(target))
    built = made.build()
    outcome = {"tree": _shape(built), "rest unchanged": _rest(built) == before[1],
               "original unchanged": (_shape(code), _rest(code)) == before, "built twice, equal": made.build() == built}
    want, oracle = expected_build(template, name, owner, steps)
    direct = None
    if _direct_applies(name, owner, steps):
        compiled_shape, compiled_local = _compiled_in_target(template, name, steps[-1][1] if steps else owner)
        direct = {"tree": compiled_shape == outcome["tree"],
                  "local names": compiled_local == [[list(c.co_varnames), list(c.co_cellvars), list(c.co_freevars)]
                                                    for c in _codes(built)]}
    return outcome, want, oracle, direct


def wrong_value(kind):
    """A fresh wrong argument of this kind."""
    if kind == "text":
        return "return self.__x"
    if kind == "function":
        return types.FunctionType(compiled_method("attribute", "x", "Trait"), {})
    if kind == "instance":
        return make_class("Target")()
    return None


REFUSAL_MESSAGES = {"text": "CodeBuilder takes a code object, not str",
                    "function": "CodeBuilder takes a code object, not function",
                    "none": "CodeBuilder takes a code object, not NoneType",
                    "instance": "CodeBuilder takes a code object, not Target"}


def refusal_case(arguments, builder=CodeBuilder):
    position, wrong = arguments["position"], arguments["wrong"]
    code = wrong_value(wrong) if position == "code" else compiled_method("attribute", "__x", "Trait")
    source = wrong_value(wrong) if position == "source" else make_class("Trait")
    target = wrong_value(wrong) if position == "target" else make_class("Target")
    # The stub's contract: a code object, and classes; reown()'s conversion message is pybind11's, not ours.
    want = {"raises": "TypeError", "message": REFUSAL_MESSAGES[wrong] if position == "code" else None}
    try:
        builder(code).reown(source, target).build()
        outcome = {"raises": None, "message": None}
    except Exception as error:  # noqa: BLE001 — what is raised is the outcome
        outcome = {"raises": type(error).__name__, "message": str(error) if position == "code" else None}
    return outcome, want, "contract"


# ── compose() and traits_of() ────────────────────────────────────────────────────────────────────

TARGET_SOURCES = {
    "plain": "class Person:\n    __kind = 'person'\n    def __init__(self):\n        self.__name = 'ann'\n{members}",
    "with a base": "class Base:\n    def hello(self):\n        return 'base'\n\n"
                   "class Child(Base):\n    __kind = 'child'\n    def __init__(self):\n        self.__name = 'ann'\n"
                   "{members}",
    "with greet": "class Person:\n    __kind = 'person'\n    def __init__(self):\n        self.__name = 'ann'\n"
                  "    def greet(self):\n        return 'own'\n{members}",
    "frozen slots dataclass": "@dataclasses.dataclass(frozen=True, slots=True)\nclass Point:\n    x: int = 4\n{members}",
    "enum": "class Colour(enum.Enum):\n    RED = 'red'\n{members}",
}
TARGET_CLASS = {"plain": "Person", "with a base": "Child", "with greet": "Person", "frozen slots dataclass": "Point",
                "enum": "Colour"}
TARGETS = Atoms("targets", (*TARGET_SOURCES, "not a class"),
                why="a plain class; one with a base; one that defines a name a trait has; a frozen slots dataclass "
                    "(rebuilt by its decorator); an Enum (a metaclass of its own); an instance instead of a class")

MEMBER_SOURCES = {
    ("function", "plain"): "def {name}(self):\n    return '{trait}'",
    ("function", "private"): "def {name}(self):\n    return '{trait}:' + self.__name",
    ("function", "super"): "def {name}(self):\n    return '{trait}:' + super().hello()",
    ("property", "private"): "@property\ndef {name}(self):\n    return '{trait}:' + self.__name",
    ("property", "super"): "@property\ndef {name}(self):\n    return '{trait}:' + super().hello()",
    ("classmethod", "private"): "@classmethod\ndef {name}(cls):\n    return '{trait}:' + cls.__kind",
    ("staticmethod", "plain"): "@staticmethod\ndef {name}():\n    return '{trait}'",
    ("constant", "plain"): "{name} = 1",
}
MEMBERS = Atoms("members", tuple(MEMBER_SOURCES),
                why="each kind a trait may hold, each with the bodies that tell copying from rewriting — a private "
                    "name, a zero-argument super(), the class's own private name — and a constant, which it may not")
MEMBER_NAMES = Atoms("member names", ("greet", "hello"),
                     why="greet: one target defines it already; hello: one target inherits it")
NOT_A_TRAIT = Atoms("not a trait", ("not a class",), why="a function given where a class should be")
TRAIT = Either("traits", (Grammar("trait classes", (("member", MEMBERS), ("name", MEMBER_NAMES)),
                                  make=lambda member, name: (*member, name), why="every member under every name"),
                          NOT_A_TRAIT), why="a class holding one member, or not a class")
SECOND_MEMBERS = Atoms("members that decide a second trait's effect",
                       (("function", "private"), ("property", "private"), ("constant", "plain")),
                       why="a function, a property, and a member compose() refuses")
SECOND_TRAIT = Either("traits, for two", (Grammar("trait classes", (("member", SECOND_MEMBERS), ("name", MEMBER_NAMES)),
                                                  make=lambda member, name: (*member, name),
                                                  why="every such member under every name"), NOT_A_TRAIT),
                      why="a class holding one member, or not a class")
OVERRIDE = Atoms("override", (False, True), why="both")
TRAIT_CLASS_NAMES = ("TraitA", "TraitB")  # the trait in each position


def _needs_slots(arguments):
    return arguments["target"] == "frozen slots dataclass" and sys.version_info < (3, 10)


def _slots_super(arguments):
    traits = [t for k, t in arguments.items() if k.startswith("trait") and isinstance(t, tuple)]
    return arguments["target"] == "frozen slots dataclass" and any(t[1] == "super" for t in traits)


TRAIT_EXCLUSIONS = (
    Exclusion("dataclass(slots=True) is Python 3.10's", _needs_slots),
    Exclusion("Python itself cannot answer: zero-argument super() written in a slots dataclass raises TypeError "
              "(CPython gh-90562), so the member written there is no oracle", _slots_super),
)
COMPOSE_ONE = Model(
    name="pygim.traits.compose: one trait",
    call="compose(target, *traits[:1], override=override); traits_of(target)",
    parameters=(Parameter("target", TARGETS),
                Parameter("traits[0]", Either("one trait or none", (Atoms("none", (None,), why="no traits"), TRAIT),
                                              why="compose() with no trait, or one")),
                Parameter("override", OVERRIDE)),
    exclusions=TRAIT_EXCLUSIONS,
)
COMPOSE_TWO = Model(
    name="pygim.traits.compose: two traits",
    call="compose(target, traits[0], traits[1], override=override); traits_of(target)",
    parameters=(Parameter("target", TARGETS), Parameter("traits[0]", SECOND_TRAIT), Parameter("traits[1]", SECOND_TRAIT),
                Parameter("override", OVERRIDE)),
    exclusions=TRAIT_EXCLUSIONS,
    notes=("A reduction: a second trait meets the first through its names (a conflict, which trait wins, what the "
           "record says) and its kind (a refusal stops the call), so two traits are crossed over a function, a "
           "property and a constant under each name; every member body is crossed with every target in the "
           "one-trait model.",),
)

_NAMESPACE = {"dataclasses": dataclasses, "enum": enum}


@functools.lru_cache(maxsize=None)
def _compiled(source, filename):
    return compile(source, filename, "exec", dont_inherit=True)  # as written: no __future__ of ours


def _executed(source, filename):
    namespace = {"__name__": GENERATED, **_NAMESPACE}
    exec(_compiled(source, filename), namespace)  # noqa: S102 — the sources are this module's templates
    return namespace


def make_target(kind):
    """(a new target, a factory of its instances) — or a wrong value and None."""
    if kind == "not a class":
        return make_class("Person")(), None
    cls = _executed(TARGET_SOURCES[kind].format(members="    pass"), f"<target {kind}>")[TARGET_CLASS[kind]]
    return cls, (lambda: cls.RED) if kind == "enum" else cls


def _member_source(spec, trait_name):
    kind, body, name = spec
    return MEMBER_SOURCES[(kind, body)].format(name=name, trait=trait_name)


def make_trait(spec, position):
    """A new trait class in this position — or, for "not a class", a new function."""
    if spec == "not a class":
        return types.FunctionType(compiled_method("attribute", "x", "Trait"), {})
    name = TRAIT_CLASS_NAMES[position]
    source = f"class {name}:\n" + textwrap.indent(_member_source(spec, name), "    ") + "\n"
    return _executed(source, f"<trait {name}>")[name]


def _trait_specs(arguments):
    return [arguments[k] for k in ("traits[0]", "traits[1]") if arguments.get(k) is not None]


def expected_compose(arguments, own):
    """The stub's contract: what compose() refuses, in the order it checks, and else what it writes
    and records — with the winning members, (trait, position, spec) by name. `own` is the target's
    own namespace before the call: the input, not an answer."""
    if arguments["target"] == "not a class":
        return {"raises": "TypeError", "message": None}, {}  # pybind11 converts it; its wording is not ours
    target_name = TARGET_CLASS[arguments["target"]]
    seen = {}
    for position, spec in enumerate(_trait_specs(arguments)):
        trait = f"{GENERATED}.{TRAIT_CLASS_NAMES[position]}"
        if spec == "not a class":
            return {"raises": "TypeError", "message": "compose takes classes as traits, not function"}, {}
        kind, _, name = spec
        if kind == "constant":
            return {"raises": "TypeError", "message": f"{TRAIT_CLASS_NAMES[position]}.{name} is int; a trait composes "
                                                       "functions, properties, classmethods and staticmethods"}, {}
        if name in seen and not arguments["override"]:
            return {"raises": "ValueError", "message": f"{seen[name][0]} and {trait} both define {name} "
                                                        "(pass override=True to let the later win)"}, {}
        if name not in seen and name in own and not arguments["override"]:
            return {"raises": "ValueError", "message": f"{target_name} already has {name}; {trait} would replace it "
                                                        "(pass override=True)"}, {}
        seen.pop(name, None)  # a later trait's member replaces an earlier one, in place and in order
        seen[name] = (trait, position, spec)
    return ({"returns": "the target", "written": sorted(seen),
             "record": {name: trait for name, (trait, _, _) in sorted(seen.items())}}, seen)


def _read(owner, make, spec):
    """(how reading the member behaves, its function's qualified name) on a new instance."""
    kind, _, name = spec
    raw = owner.__dict__[name]
    function = getattr(raw, "fget", None) or getattr(raw, "__func__", None) or raw
    try:
        value = getattr(make(), name)
        outcome = ["value", value if kind == "property" else value()]
    except Exception as error:  # noqa: BLE001 — what is raised is the outcome
        outcome = ["raises", type(error).__name__]
    return outcome, function.__qualname__


def written_class(arguments, winners):
    """The target with the winning members written into its own source, compiled: how each member
    behaves if written there — Python's answer, not pygim's."""
    kind = arguments["target"]
    members = "\n".join(textwrap.indent(_member_source(spec, TRAIT_CLASS_NAMES[position]), "    ")
                        for _, position, spec in winners)
    cls = _executed(TARGET_SOURCES[kind].format(members=members), f"<written {kind}>")[TARGET_CLASS[kind]]
    return cls, (lambda: cls.RED) if kind == "enum" else cls


def _defined(trait, name):
    raw = trait.__dict__[name]
    code = (getattr(raw, "fget", None) or getattr(raw, "__func__", None) or raw).__code__
    return f"{code.co_filename}:{code.co_firstlineno}"


def compose_case(arguments, composer=compose, recorder=traits_of):
    """(the outcome, the expectation, its oracle) of one compose() call, then traits_of()."""
    target, make = make_target(arguments["target"])
    specs = _trait_specs(arguments)
    traits = [make_trait(spec, position) for position, spec in enumerate(specs)]
    trait_members = [dict(vars(t)) if isinstance(t, type) else None for t in traits]
    own = dict(vars(target)) if isinstance(target, type) else {}
    want, winners = expected_compose(arguments, own)
    try:
        composed_at = f"{__file__}:{sys._getframe().f_lineno + 1}"
        returned = composer(target, *traits, override=arguments["override"])
    except Exception as error:  # noqa: BLE001 — what is raised is the outcome
        outcome = {"raises": type(error).__name__, "message": str(error) if want.get("message") else None}
        if "raises" in want and isinstance(target, type):
            outcome["nothing written"] = dict(vars(target)) == own
            want = {**want, "nothing written": True}
        return outcome, want, "contract"
    written = sorted(n for n, v in vars(target).items() if n not in own or own[n] is not v)
    record = recorder(target)
    outcome = {"returns": "the target" if returned is target else repr(returned), "written": written,
               "record": {name: record[name].trait for name in record}}
    if "raises" in want:
        return outcome, want, "contract"
    winners = list(winners.values())
    written_cls, written_make = written_class(arguments, winners)
    outcome["members"] = {spec[2]: _read(target, make, spec) for _, _, spec in winners}
    want["members"] = {spec[2]: _read(written_cls, written_make, spec) for _, _, spec in winners}
    outcome["provenance"] = {name: [record[name].defined, record[name].composed] for name in record}
    want["provenance"] = {spec[2]: [_defined(traits[position], spec[2]), composed_at] for _, position, spec in winners}
    outcome["traits untouched"] = all(m is None or dict(vars(t)) == m for t, m in zip(traits, trait_members))
    want["traits untouched"] = True
    return outcome, want, "contract+compiler"


# ── pygim.utils.is_dunder ────────────────────────────────────────────────────────────────────────

IS_DUNDER = Model(
    name="pygim.utils.is_dunder",
    call="is_dunder(name)",
    parameters=(Parameter("name", Either("names and not names", (
        names_oracle.NAMES, Atoms("not text", (None, 1, b"__x__"), why="None, a number, bytes")),
        why="the name rules' whole name domain, and values that are not text")),),
)


def is_dunder_case(arguments, function=utils.is_dunder):
    name = arguments["name"]
    want = ({"returns": enum._is_dunder(name)}, "enum") if isinstance(name, str) else \
        ({"raises": "TypeError"}, "contract")
    try:
        outcome = {"returns": function(name)}
    except Exception as error:  # noqa: BLE001 — what is raised is the outcome
        outcome = {"raises": type(error).__name__}
    return outcome, want[0], want[1]


# ── what pygim does that its contract or Python does not: found by these spaces, not yet decided ─


@dataclasses.dataclass(frozen=True)
class Divergence:
    """Calls whose outcome differs from the expectation for a known reason, kept visible: each case
    it names must still fail (else the check reports it no longer observed), and no other may."""

    model: str
    finding: str
    applies: object  # arguments -> bool


def _private_local(arguments):
    """A method binding a private local name, moved to a class that spells it otherwise."""
    if arguments.get("template") not in ("local", "import") or not arguments.get("step"):
        return False
    owner, (_, target) = arguments["owner"], arguments["step"]
    if not _direct_applies(arguments["name"], owner, [arguments["step"]]):
        return False
    local = arguments["name"].split(".")[0]  # `import a.b` binds `a`

    def spelt(cls):
        return names_oracle.expected("mangled", {"class_name": cls.rsplit(".", 1)[-1], "name": local})[0]

    return spelt(owner) != spelt(target)


DIVERGENCES = (
    Divergence(CODE_BUILDER_ONE_STEP.name,
               "CodeBuilder respells co_names and qualified names but not a private LOCAL name: co_varnames, "
               "co_cellvars and co_freevars keep the old class's spelling (`_Trait__x`), where the method compiled "
               "in the target has `_Target__x`. Behaviour is unchanged — locals are found by position, and a cell "
               "and its free variable keep matching names — but locals() shows the old spelling, against the "
               "docstring's 'respells every private name'.", _private_local),
    Divergence(IS_DUNDER.name,
               "pygim.utils.is_dunder accepts bytes — pybind11's string caster converts them — though its "
               "signature says `name: str`; is_dunder(b'__x__') is True.", lambda a: isinstance(a["name"], bytes)),
)


# ── running a model ──────────────────────────────────────────────────────────────────────────────


def execute(model, case_function, **implementation):
    """Every generated call of `model`, run by `case_function` against `implementation` (pygim's
    own unless a test substitutes one), each judged against its expectation."""
    run = Run(model=model.name, advertised=advertise(model))
    divergences = [d for d in DIVERGENCES if d.model == model.name]
    for case in generate(model):
        result = case_function(case.arguments, **implementation)
        outcome, want, oracle = result[:3]
        record = {"id": case.id, "arguments": case.arguments, "expected": want, "oracle": oracle,
                  "outcome": outcome, "passed": outcome == want}
        if len(result) > 3:
            record["compiler"] = result[3]
            if result[3] is not None and not all(result[3].values()):
                record["passed"] = False
        known = next((d.finding for d in divergences if d.applies(case.arguments)), None)
        if known:
            record["divergence"] = known
        run.records.append(record)
    run.count = len(run.records)
    return run


MODELS = {
    CODE_BUILDER_ONE_STEP.name: (CODE_BUILDER_ONE_STEP, build_case),
    CODE_BUILDER_CHAINS.name: (CODE_BUILDER_CHAINS, build_case),
    CODE_BUILDER_REFUSALS.name: (CODE_BUILDER_REFUSALS, refusal_case),
    COMPOSE_ONE.name: (COMPOSE_ONE, compose_case),
    COMPOSE_TWO.name: (COMPOSE_TWO, compose_case),
    IS_DUNDER.name: (IS_DUNDER, is_dunder_case),
}


@functools.lru_cache(maxsize=None)
def runs():
    """Each model's run against pygim, once per process: {model name: Run}."""
    return {name: execute(model, case_function) for name, (model, case_function) in MODELS.items()}
