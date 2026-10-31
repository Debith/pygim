"""How autowiring reads a class's constructor (D67): from its ``__init__``'s code object and
``__annotations__`` when that is what ``inspect.signature`` and ``typing.get_type_hints`` would
give, and through them otherwise. inspect.signature(cls) took 14 µs a class and importing inspect
6 ms; the code object takes 0.2 µs. Each shape below is held to the algorithm the container used
until 2026-10-31 — inspect and get_type_hints, then the autowiring plan — written out here."""

import dataclasses
import functools
import inspect
import re
import sys
import typing
from abc import ABC
from typing import Annotated, Generic, NamedTuple, Optional, TypeVar

import pytest

from pygim.ioc import Container


class A:
    pass


class B:
    pass


UNSET = object()
T = TypeVar("T")


# --------------------------------------------------------------------------- the shapes
class Plain:
    def __init__(self, a: A, b: B = UNSET) -> None:
        pass


class KeywordOnly:
    def __init__(self, *, a: A, n: int = 3):
        pass


class Variadic:
    def __init__(self, a: A, *args, b: B, **kwargs):
        pass


class PositionalOnly:
    def __init__(self, a: A, /):
        pass


class Untyped:
    def __init__(self, x):
        pass


class UntypedDefault:
    def __init__(self, x=1, *, y=2):
        pass


class Strings:
    def __init__(self, a: "A", b: "B" = UNSET):
        pass


class StringReturn:
    def __init__(self, a: A) -> "None":
        pass


class BadReturn:
    def __init__(self, a: A) -> "NotDefinedAnywhere":  # noqa: F821
        pass


class BadHint:
    def __init__(self, a: "NotDefinedAnywhere"):  # noqa: F821
        pass


class Extras:
    def __init__(self, a: Annotated[A, "the a"], b: Optional[B] = None):
        pass


class NoneDefault:
    def __init__(self, a: A = None):
        pass


class NotAType:
    def __init__(self, a: 5 = 0):  # an annotation is any expression
        pass


class Inherits(Plain):
    pass


@dataclasses.dataclass
class Data:
    a: A
    n: int = 0


class New:
    def __new__(cls, a: A):
        return super().__new__(cls)


class NewAndInit:
    def __new__(cls, *args, **kwargs):
        return super().__new__(cls)

    def __init__(self, a: A):
        pass


class Meta(type):
    def __call__(cls, a: A):
        return super().__call__()


class Called(metaclass=Meta):
    pass


def logged(init):
    @functools.wraps(init)
    def wrapper(self, *args, **kwargs):
        return init(self, *args, **kwargs)
    return wrapper


class Wrapped:
    @logged
    def __init__(self, a: A):
        pass


class Signed:
    __signature__ = inspect.Signature([inspect.Parameter("b", inspect.Parameter.KEYWORD_ONLY, annotation=B)])

    def __init__(self, a: A = UNSET, b: B = UNSET):
        pass


class RaisingMeta(type):
    @property
    def __signature__(cls):
        raise ValueError("no signature here")


class Raises(metaclass=RaisingMeta):
    def __init__(self, a: A):
        pass


class Empty:
    pass


class OnlyStar:
    def __init__(*args, **kwargs):
        pass


class NoSlotForSelf:
    def __init__(**kwargs):  # inspect cannot bind it: no place for the instance
        pass


class ThisNotSelf:
    def __init__(this, a: A, *, self: B = UNSET):
        pass


class Mangled:
    def __init__(self, __a: A = UNSET):
        pass


class OfDict(dict):
    def __init__(self, a: A):
        super().__init__()


class DictFirst(dict, Plain):   # dict's own __init__ comes before Plain's
    pass


class Abstract(ABC):
    def __init__(self, a: A):
        pass


class Generic_(Generic[T]):
    def __init__(self, a: A):
        pass


class Pair(NamedTuple):
    a: A
    n: int = 0


SHAPES = [Plain, KeywordOnly, Variadic, PositionalOnly, Untyped, UntypedDefault, Strings, StringReturn,
          BadReturn, BadHint, Extras, NoneDefault, NotAType, Inherits, Data, New, NewAndInit, Called,
          Wrapped, Signed, Raises, Empty, OnlyStar, NoSlotForSelf, ThisNotSelf, Mangled, OfDict, DictFirst,
          Abstract, Generic_, Pair, super]


# --------------------------------------------------------------------------- the old algorithm
def inspected(cls):
    """The constructor's parameters as the container read them until 2026-10-31: (name, kind,
    annotation or None, has a default), or the error it raised."""
    try:
        signature = inspect.signature(cls)
    except Exception:
        return RuntimeError("autowire requires an inspectable class provider")
    try:
        hints = typing.get_type_hints(cls.__init__)
    except Exception:
        return RuntimeError("autowire could not resolve constructor type hints")
    specs = []
    for name, parameter in signature.parameters.items():
        if name == "self":
            continue
        annotation = hints.get(name, parameter.annotation)
        specs.append((name, parameter.kind, None if annotation is parameter.empty else annotation,
                      parameter.default is not parameter.empty))
    return specs


def planned(specs, registered):
    """(name, interface) for each argument the plan injects, in order, or the error it raises
    (core::plan_autowiring)."""
    if isinstance(specs, Exception):
        return specs
    injected = []
    for name, kind, annotation, has_default in specs:
        if kind in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD):
            continue
        if kind is inspect.Parameter.POSITIONAL_ONLY:
            return RuntimeError(f"autowire does not support positional-only parameter '{name}'")
        if annotation is None:
            if has_default:
                continue
            return RuntimeError(f"autowire requires a type annotation for parameter '{name}'")
        if not any(annotation is known for known in registered):
            if has_default:
                continue
            return KeyError(f"No provider registered for autowired dependency '{name}'")
        injected.append((name, annotation))
    return injected


def expected(cls):
    """What the old algorithm injects into `cls`, A and B registered — the interfaces, in order — or
    the error planning or the constructor raises."""
    plan = planned(inspected(cls), [A, B])
    if isinstance(plan, Exception):
        return plan
    try:
        cls(**{name: interface() for name, interface in plan})
    except Exception as error:   # the constructor's own refusal is part of the outcome
        return error
    return [interface for _, interface in plan]


def outcome(result):
    """A result or an error, comparable: the error's type and message, the key suffix left out."""
    if isinstance(result, Exception):
        said = result.args[0] if result.args else ""
        return type(result).__name__, re.sub(r" \[key: [^]]*\]$", "", str(said))
    return result


def wired(cls):
    """What the container injects into `cls`, A and B registered: the interfaces, in order."""
    container, asked = Container(), []
    container.register(A, lambda: asked.append(A) or A())
    container.register(B, lambda: asked.append(B) or B())
    container.register(cls, cls, autowire=True)
    try:
        container.resolve(cls)
    except Exception as error:   # the outcome under test, whatever it is
        return error
    return asked


@pytest.mark.parametrize("cls", SHAPES, ids=[cls.__name__ for cls in SHAPES])
def test_autowire_reads_each_constructor_as_inspect_does(cls):
    assert outcome(wired(cls)) == outcome(expected(cls))


@pytest.mark.skipif(sys.version_info < (3, 11), reason="before 3.11 get_type_hints made `x: T = None` Optional, so it is always asked")
def test_a_plain_constructor_is_read_without_inspect_or_typing(monkeypatch):
    """The common shape — a Python __init__ annotated with classes — never reaches inspect or typing:
    autowiring is what imports inspect in a program that does not (6 ms), and inspect.signature(cls)
    and get_type_hints cost 16 µs a class against the code object's 0.2."""
    plain = (Plain, KeywordOnly, Variadic, Inherits, Data, ThisNotSelf, OfDict, Abstract, Mangled)
    before = {cls: outcome(expected(cls)) for cls in plain}

    def refused(*args, **kwargs):
        raise AssertionError("inspect or typing was asked")

    monkeypatch.setattr(inspect, "signature", refused)
    monkeypatch.setattr(typing, "get_type_hints", refused)
    assert {cls: outcome(wired(cls)) for cls in plain} == before


@pytest.mark.skipif(sys.version_info >= (3, 11), reason="get_type_hints stopped making `x: T = None` Optional in 3.11")
def test_before_python_3_11_a_none_default_stays_optional():
    """get_type_hints made `a: A = None` Optional[A] until 3.11, so the plan injected nothing; the code
    object's path leaves that to get_type_hints there, and the behaviour is as before."""
    assert wired(NoneDefault) == []
