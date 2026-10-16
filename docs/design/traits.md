# Traits: behaviour composed into classes

Status: proposed 2026-10-16 — the code builder is built, composition is not; decisions open
(below) · Owner: Debith · Branch: `core/traits`
Where: `src/_pygim_fast/traits/` (core, adapter, bindings), public as `pygim.traits`; proofs in
`tests/static/traits_proofs.cpp`

Three words this document assumes:

- **trait** — a class whose methods are written to be copied into other classes. It is never a
  base class: nothing inherits from it.
- **compose** — copy a trait's members into a target class, each rewritten so it behaves exactly
  as if it had been written in the target.
- **quantity** — a value with a magnitude and a unit that must match for arithmetic and
  comparison: money (amount, currency), a duration, a weight.

In one line: pygim copies a trait's methods into a class and rewrites them to belong there, so
behaviour is written once and composed where it is needed, without inheritance.

## Scenarios

### 1. A quantity gets its arithmetic from traits

`Money` today writes four operators (`__add__`, `__sub__`, `__mul__`, `__lt__`), the alias
`__rmul__`, a guard decorator for them and `functools.total_ordering` — about 40 lines of
`__notes__/value_object/money.py`. Every other quantity would write them again. With traits it
declares what it is, and the arithmetic is composed in:

```python
@quantified_value_object(magnitude="amount", unit="currency")
@dataclass(frozen=True, slots=True)
class Money:
    amount: int
    currency: Currency
```

| | Today | With traits |
|---|---|---|
| `+`, `-`, `*`, `3 * m`, `<`, `<=`, `>`, `>=` | written beside `Money`, about 40 lines | composed from `Additive`, `Scalable`, `Ordered` |
| `price + 5` | `TypeError`, from the guard decorator's `NotImplemented` | the same, written once in the trait |
| `eur + usd` | `ValueError: cannot combine EUR with USD` | the same, from the trait |
| a second quantity (`Duration`) | writes all of it again | one decorator line |

### 2. A value without a unit gets no arithmetic

`OrderId` is a value object but not a quantity, so nothing composes `Additive` into it, and
`OrderId("a") + OrderId("b")` is a `TypeError` — the class has no `__add__` at all. A capability
a class was not given is absent, never a method that raises: the rule the mapping toolkit's first
design called *surface honesty*.

### 3. Composing by hand

```python
Example = gim_type("Example") << Data << Report
```

`Report.summary` reads `self.__total`. Inside `Report`, Python compiled that name as
`_Report__total`; composed into `Example`, the copy reads `_Example__total`, which is where
`Data.__init__` stored it. A trait method that calls `super().summary()` reaches `Example`'s next
class in the method resolution order, not `Report`'s.

### 4. A conflict is refused, and the class stays as it was

Composing a trait that defines `__add__` into a class that already has one raises
`ValueError: Money already has __add__; Additive would replace it (pass override=True)`, and
nothing is copied — not even the members checked before the conflict was found.

### 5. Where a method came from

`traits_of(Money)` answers, for each composed member, the trait it came from, the file and line
where the trait defined it, and where it was composed.

## What composing does

```
compose(target, trait, override = false):
    plan = for each member of trait, by its kind                  # function, property,
                                                                  # classmethod, staticmethod
        refuse if target has the member's name and not override   # checked for every member
        rewrite the member for target:                            # before any is written
            private names, in its code and every nested code:
                _Trait__x  ->  _Target__x
            the __class__ cell  ->  target                        # so super() resolves in target
            qualified name      ->  Target.name
    write every rewritten member into target's own namespace
    record (target, name) -> (trait, where defined, where composed)
```

Two passes, so a refusal leaves the target unchanged: the plan is built and checked in full before
the first member is written.

The kinds of member, and what composing does with each:

| Member in the trait | What is copied | Rewritten |
|---|---|---|
| function | a new function on the rewritten code | private names, `__class__` cell, qualified name |
| `property` | a new property over its rewritten getter, setter and deleter | each accessor, as a function |
| `classmethod`, `staticmethod` | a new wrapper over the rewritten function | the function inside |
| a function written in C++ | the callable, wrapped so it binds as a method | nothing: it has no Python code |
| anything else (a constant, a nested class) | refused in the first iteration | — |

The kinds are a pack of descriptors, one per kind, never an if-chain on the member's type (#134):
adding a kind is adding a descriptor, and the build proves the pack covers every kind.

## The code builder (built)

The rewrite of a function's code is a builder of its own, `pygim.traits.CodeBuilder`: the one
place that touches Python's code objects, usable apart from composition.

```python
code = CodeBuilder(Trait.nested.__code__).reown(Trait, Target).build()
```

| Call | What it does | Cost |
|---|---|---|
| `CodeBuilder(code)` | starts from a code object, which is never changed; anything else is a `TypeError` naming its type | one type check |
| `.reown(source, target)` | records a move from class `source` to class `target`; returns the builder, so steps chain | two strings per class |
| `.build()` | a new code object with every step applied in order: `co_names` by `core.h`'s `reowned()`, `co_qualname` (Python 3.11 and later) by `requalified()`, in the code and every code object nested in its constants | one `code.replace()` per code object |

Steps apply to each name in turn, so `reown(A, B).reown(B, C)` moves A's private names to C. The
one exception is a name A's code already spelt as B's own: after the first step it reads as B's,
so the second moves it on, where a direct `reown(A, C)` leaves it as B's.
`traits_proofs.cpp` states the law with that exception.

What the builder leaves as it is, and why:

- the `__class__` cell — the closure belongs to the function, not to its code, so composing
  rebinds it (D5);
- local variable names — mangled too (`__z = 1` is the local `_Trait__z`), but they never leave
  the function, and a nested function that closes over one names the same spelling (checked: the
  moved code still returns the value its lambda closed over).

A keyword argument's name in a call is not on the list because Python does not mangle it:
`g(__x=1)` inside `Trait` passes `'__x'` (checked).

## Where the work happens

```
pygim.traits                 compose, gim_type, gimmick, traits_of,
  (Python: routing only)     quantified_value_object, the quantity traits
        │
bindings.cpp                 registers, nothing else
        │
adapter (pybind11)           CodeBuilder: the rewrite through code.replace(); the member-kind pack;
                             the __class__ cell; gim_type as a heap type whose base is
                             type, built the way pybind11 builds its own metaclass;
                             provenance in a RegistryCore
        │
core (pybind-free,           Python's name-mangling rule, both ways; the override policy;
  constexpr, proven)         a trait's requirements (Additive needs a magnitude and a unit)
```

The core holds every rule that is not Python's object model, so it can be proven by
`static_assert`. The laws, for `tests/static/traits_proofs.cpp`:

- mangling follows CPython's `_Py_Mangle`: only names that start with two underscores and do not
  end with two are mangled; the class name loses its leading underscores; a class name of only
  underscores mangles nothing; a dotted name is untouched;
- unmangling a name mangled for one class and mangling it for another is the rewrite above, and
  a name that was not mangled survives it unchanged;
- the override policy refuses a present name without `override` and replaces it with it;
- a trait's requirements are met or the composition is refused, naming the missing role.

## Decisions to make

### D1 — the metaclass, or composing into any class

| Option | What it is | Trade-off |
|---|---|---|
| A. Only `gim_type` | as the archive: a class must be made by the metaclass to take traits, `cls << trait` | a class cannot have two metaclasses, so an `Enum`, an ABC or a pybind11 class cannot take traits; and `dataclass(slots=True)` re-creates the class, running the metaclass's `__new__` a second time (measured: 2 calls for one class) |
| **B. `compose()` on any class, `gim_type` as the opt-in syntax** (recommended) | the core operation takes any class; `gim_type` and `gimmick`, in C++, give `<<` to classes that choose them | two entries to one operation, but one is the operation and the other its syntax, as `+` is for `__add__`; `quantified_value_object` composes into a dataclass with no metaclass |

### D2 — what the quantity traits are written in

| Option | What it is | Trade-off |
|---|---|---|
| **A. C++** (recommended) | `Additive`, `Scalable`, `Ordered` as C++ callables, composed as methods | behaviour lives in C++ (definition of done); needs the wrapper that makes a C++ callable bind as a method, unverified for pybind11 functions |
| B. Python trait classes | written as `Report` above | proves the copy path end to end; the arithmetic would be pygim behaviour in Python, which the definition of done rules out |

Either way, the claim that it is fast enough is a measurement: an A/B in one process of a
composed `+` against the hand-written one (`benchmarks/`).

### D3 — names

The module `pygim.traits`. For the decorator, `quantified_value_object` (the owner's name) or
`quantity` (Fowler's, *Analysis Patterns*: a magnitude with a unit) — global #105 asks for the
domain's word.

### D4 — a conflict

Refuse by default, replace with `override=True`: the registry's rule for a duplicate key, so a
user meets one rule in both places.

### D5 — zero-argument `super()` inside a trait

Rebind the `__class__` cell to the target (recommended; measured: `super().hello()` in a copied
trait method returned `"trait+base"` after rebinding, `TypeError` before), or refuse such a trait.

## Decided by the evidence

The archive's implementation (`pygim-archive`, branch `release`, 2023-11-25:
`pygim-common/_pygim/_magic/_gimmick.py`, `_traits.py`, `_patch.py`) is the starting point.
What changes, each from a measurement or a rule:

| The archive | Here | Why |
|---|---|---|
| rebuilt `types.CodeType` from a hand-kept table of arguments for Python 3.7–3.12 | `code.replace(...)` | the language's own since 3.8, so no table breaks on the next release (global #111) |
| rewrote the names of the top-level code only | every nested code object as well | measured: a lambda inside a trait method still read `_Trait__secret` and raised `AttributeError` |
| copied the closure as it was | the `__class__` cell rebound to the target | measured: `super()` raised `TypeError` |
| functions only | a pack of member kinds | properties and class methods are members a trait needs |
| provenance recorded only under `__debug__` | always, in a `RegistryCore` | `python -O` silently dropped it; every keyed store is a `RegistryCore` (#131) |
| Python | C++ core and adapter | the definition of done: behaviour lives in C++ |

## Open

- **Static types.** Composed methods are invisible to type checkers, as anything added at run time
  is. `pygim stubs` writes pygim's own stubs; whether it should also write stubs for a user's
  composed classes, or a typing construct should declare them, is open.
- **The DDD home.** `_pygim/_core/protocols.py` holds the DDD protocols, unexported. Its
  `ValueObject` requires only `__eq__`, so a list, a dict and `object()` all pass
  `isinstance(x, ValueObject)` (measured). `quantified_value_object` gives that protocol something
  real to check.
- **The supported Pythons, 3.9 to 3.14.** A code object has `co_qualname` only from 3.11; on 3.9
  and 3.10 the function's `__qualname__` carries the name alone.

## Sequence of work

1. core: mangling (built, proven), the override policy and requirements, with their proofs.
2. adapter: `CodeBuilder` (built); then `compose()` for functions — the `__class__` cell, refusal
   before any write, provenance — and the other member kinds.
3. `gim_type` and `gimmick` in C++, with `<<`.
4. The quantity traits and `quantified_value_object`; the notes' `Money` rewritten on them as the
   first user, and an example under `docs/examples/traits/`.
5. Unit tests through public names, the stub, the changelog.
