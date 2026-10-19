# Traits: behaviour composed into classes

Status: `CodeBuilder`, `compose()` and `traits_of()` built 2026-10-16; `gim_type`, `gimmick` and
the quantity traits not yet; D3 open (below) · Owner: Debith · Branch: `core/traits`
Where: `src/_pygim_fast/traits/` (adapter, bindings), public as `pygim.traits`; the naming rules it
relies on in the base header `src/_pygim_fast/utils/python_names.h`, proven in
`tests/static/python_names_proofs.cpp`

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

## What composing does (built)

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
| `.build()` | a new code object with every step applied in order: `co_names` by `python_names.h`'s `respelt()` — a name no step moves is passed on as the same object — and `co_qualname` (Python 3.11 and later) by `requalified()`, in the code and every code object nested in its constants | one `code.replace()` per code object, by vectorcall; per name a comparison, and an allocation only for a re-spelt name. Measured (`benchmarks/traits_code_builder.py`): 1.69 µs for a method naming 40 attributes, against 1.19 µs for Python's own `code.replace()` walking the same code objects |

Steps apply to each name in turn, so `reown(A, B).reown(B, C)` moves A's private names to C. The
one exception is a name A's code already spelt as B's own: after the first step it reads as B's,
so the second moves it on, where a direct `reown(A, C)` leaves it as B's.
`python_names_proofs.cpp` states the law with that exception.

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
adapter (pybind11)           CodeBuilder: the rewrite through code.replace(); compose(): the member-kind pack;
                             the __class__ cell; gim_type as a heap type whose base is
                             type, built the way pybind11 builds its own metaclass;
                             provenance in a RegistryCore
        │
core (pybind-free,           utils/python_names.h, a base header: Python's naming rules —
  constexpr, proven)         the dunder rule (enum._is_dunder) and the private-name rule
                             (_Py_Mangle), both ways; the override policy and a trait's
                             requirements (Additive needs a magnitude and a unit), still to come
```

The core holds every rule that is not Python's object model, so it can be proven by
`static_assert`. The laws, in `tests/static/python_names_proofs.cpp`:

- mangling follows CPython's `_Py_Mangle`: only names that start with two underscores and do not
  end with two are mangled; the class name loses its leading underscores; a class name of only
  underscores mangles nothing; a dotted name is untouched;
- unmangling a name mangled for one class and mangling it for another is the rewrite above, and
  a name that was not mangled survives it unchanged;
- the override policy refuses a present name without `override` and replaces it with it;
- a trait's requirements are met or the composition is refused, naming the missing role.

The coverage is generated, not listed. Each function's call space is described as data — domains
of argument values, a few atoms chosen by hand and the rest expanded from declared factors and
bounds, plus the exclusions with their reasons (`tests/unittests/callspace.py`) — and expanded
into every call; a checker rebuilds the space from the declaration and fails a run that misses,
duplicates or adds a call, or whose domain shrank. The expectations come from Python, never from
pygim: `enum._is_dunder`, Python's compiler, or the written contract where the compiler cannot be
asked.

- `utils/python_names.h`: the model is `tests/unittests/python_names_callspace.py` — every function,
  each parameter's domain chosen by its role, 8,806 calls. Every build proves the whole product at
  compile time (`tests/static/python_names_callspace_proofs.cpp`, generated from the model).
  `tests/static/run_python_names_callspace.py` (GCC 16, `-freflection`) finds the functions and
  their parameters by reflection — one without a model does not compile — calls the product
  natively, measures gcov's branches and conditions, and runs the negative controls: a deleted
  case, a shrunken domain, five behavioural mutants of the header (caught natively and by the
  every-build proof), an added function.
- `CodeBuilder`, `compose()` and `traits_of()`, `is_dunder`: the models are
  `tests/unittests/traits_callspace.py`, 11,260 calls run by `test_traits_callspace.py`, with code
  objects and classes made by factories and fresh targets per case, and defects put in place of
  the implementation as negative controls.

[The cases page](traits_cases.md) is rendered from the same cases — the models, one-input
comparisons from a baseline, what the cases found, the authored seed rows of
`tests/unittests/traits_cases.py`, and what is not covered; the suite fails when it is stale.
Functions with identical parameter domains and required inputs share a comparison table, with
one expected-result column per function. Results are joined by exact arguments, not row order.
Every argument stays visible; each shared baseline appears once and changed cells are bold.
Expectation-source counts remain per function outside the table. Models with different call
spaces stay separate, and this presentation does not change the generated coverage.

### Adversarial testing and the threat boundary (2026-10-18)

`compose()` and `CodeBuilder` manipulate executable Python objects; they are not a sandbox for
arbitrary Python. What is guaranteed, and tested adversarially: the interpreter is never crashed by
any input; on success only the documented target attributes and the provenance record change; on a
documented refusal the target is left exactly as it was; and neither a refusal nor a hostile hook
leaves stale bookkeeping a later composition would trip over. A legitimate name is never refused for
looking unusual. The hostile inputs are generated from declared domains
(`tests/unittests/traits_adversarial.py`, run by `test_traits_adversarial.py`), state is read only
through trusted projections (names and object identity, never a value's `__eq__`/`__repr__`/`__hash__`),
and unrelated classes carry sentinels so a stray write is seen. The C++ naming rules are driven under
AddressSanitizer and UBSan over a seed corpus with bounded mutations
(`tests/static/run_python_names_sanitizer.py`); no coverage-guided fuzzer is installed, which that
runner reports rather than feigns. The case-family inventory, with what is generated, seeded, run in
a subprocess or sanitized, is on [the cases page](traits_cases.md).

Six defects the adversarial work found were fixed test-first (seeds in
`tests/unittests/test_traits_regressions.py`, each failing before its fix):
a member name with an embedded NUL was written truncated (now the whole name is written, from the
key's real characters, so the conflict check and the write agree); a `str`-subclass key was composed
under its `__str__` result (now under its own characters); a trait member named `__pygim_traits__`
and a hook deleting the record mid-write crashed the interpreter (fixed first by refusing the name
and attaching the record last); a code object nested past the recursion limit crashed `build()` (now
a `RecursionError`, via a recursion guard); `compose()` with nothing to compose wrote an empty record
(now it is a true no-op); and an existing non-record `__pygim_traits__` gave a cast error (fixed
first by a clear `TypeError`). A non-str member name and a lone-surrogate name are now refused with a
clear error. The two record defects had one cause, a record kept where a member can be: since
2026-10-19 the record is kept off the class (below), the guards are gone, and the seeds now check
that a member or attribute of that name is an ordinary one.

**Decided — no rollback on a partial failure (2026-10-18).** When a write partway through a
multi-member composition fails (a slot Python refuses, or a hostile `__setattr__` that raises on a
later member), the members already written are left in place; `compose()` does not undo them.
Debith: rolling back "would lead to even more unexpected behavior" — un-setting attributes the
caller's hooks saw created, firing those hooks again in reverse, is less predictable than stopping
where the failure happened. The record is noted only after every member is written, so
bookkeeping is never left half-written; only the member attributes up to the failure are. `compose()`
keeps the two-pass guarantee for the failures it controls (a bad trait, a conflict) — those are all
detected before any write — so a partial write happens only
when an attribute write pygim cannot foresee is rejected by Python or by the target's own hooks.
`test_traits_regressions.py::TestNoRollbackOnAPartialFailure` pins this.

Open policy question — self-reported identity. A class whose metaclass misreports its own
`__name__`/`__qualname__` (for example a metaclass `__name__` property that returns a different
string) is believed: `CodeBuilder` mangles private names and builds qualified names from whatever
the class reports as its name. The question is whether `compose()`/`CodeBuilder` should instead
read the real `__name__` through `type.__dict__` to defeat such a lie, or keep trusting the class's
declared identity (today's behaviour — a class's name is whatever it says it is, as everywhere else
in Python). It matters only for a deliberately deceptive metaclass; an honest class reports its
name truthfully. No test pins a wrong-answer case; it is left as the open choice.

## Decided (2026-10-16)

| | Decision | Rejected, and why |
|---|---|---|
| D1 | Any class takes a trait: `compose()` is the operation. `gim_type` and `gimmick`, in C++, give the classes that choose them built-in support: `<<`, ownership transfer when a function is assigned to the class, a record from the moment the class is made | only `gim_type` classes taking traits: a class has one metaclass, so an `Enum`, an ABC or a pybind11 class could never take one (measured: "metaclass conflict" for `Enum` and for `ABC`), and `dataclass(slots=True)` runs a metaclass's `__new__` twice (measured) |
| D2 | the quantity traits are C++ callables, composed as methods | Python trait classes: pygim's behaviour lives in C++ (the definition of done) |
| D4 | a name the target's own namespace holds, or two traits of one call both define, is refused; `override=True` replaces, the later trait winning — the registry's rule for a duplicate key | replacing silently: a composed member would hide the class's own without a word |
| D5 | the `__class__` cell is rebound to the target, so a zero-argument `super()` resolves there | refusing traits that use `super()`: rebinding works (measured: `"trait+base"` after, `TypeError` before) and keeps a trait ordinary code |

Python's naming rules are not trait-specific, so they are not traits' (decided the same day): the
dunder rule and the private-name rule live in `utils/python_names.h`, a dependency-free base header,
each matched to where Python applies it — `enum._is_dunder`, the compiler's `_Py_Mangle` (global
#113). Rejected: keeping them in a traits core (the rules belong to one family that `each`,
`pygim.utils` and four hand-written Python call sites on `core/memory` already use), and folding them
into `core_utils.h` (one more subject in a header of joining, units and rates).

## Decided (2026-10-19) — the record, kept off the class

From the review of PR #38. The record is what `traits_of(cls)` reads: for each composed member, the
trait it came from, where the trait defined it, and where it was composed.

| | Decision | Rejected, and why |
|---|---|---|
| R1 | For any class `compose()` accepts, the record lives in a side table in C++ (`traits/adapter/compose.h`, `RecordStore`): a `RegistryCore` over the hashed engine (#131), keyed by the class's address, each entry holding a weak reference to its class whose callback removes the entry while the class is destroyed, before its memory is freed. A class made by `gim_type` will carry its record as a private member instead, beside the unique id it is to have (open, below) | **the class attribute `__pygim_traits__`:** visible, so a trait member or the class's own attribute of that name collided with it (F2 crashed the interpreter, F6 broke `compose()`), and guarding the name only refused the collision; **a private slot on every class:** CPython gives a class none — its layout is its metaclass's, and a metaclass cannot gain a field from Python (measured: `nonempty __slots__ not supported for subtype of 'type'`); **a `WeakKeyDictionary`,** functools.singledispatch's table: it hashes and compares the class, so a metaclass whose `__hash__`/`__eq__` lie merges two classes' records (the test asks and is never answered); **no eviction:** CPython reuses a freed class's address almost every time (200 of 200 measured), so a dead class's record would be read as the new class's own (the test sees the reuse and fails when the callback does nothing) |
| R2 | Composing a class that has a record carries the record along: in `compose(class1, class2)`, where `class2` was composed from traits, each member is attributed to the trait it first came from and where that trait defined it, and composed at the new call; `class2`'s own members are attributed to `class2` | attributing every member to `class2`: `class1` would forget the traits behind it, so `class1 << class2` would lose what `class2 << trait1 << trait2` knew |
| R4 | The record is accurate (D15, Debith: "A, accurately"): each entry keeps the object `compose()` left in the class — read from its own namespace after the writes, so a metaclass hook that stores something else is still followed — and `traits_of(cls)` lists only the members `cls` still holds as that object. A member replaced or deleted since is the class's own; R2 carries an entry only while the composed class still holds it, so `class1 << class2` attributes a member `class2` replaced to `class2` | **a log of what `compose()` once wrote:** after `Middle.greet = custom`, `traits_of(Middle)` still named the trait, and R2 carried that into every class composed from `Middle`, crediting `custom` to the trait and its line (found 2026-10-19, reproduced); **marking a replaced entry instead of dropping it:** a record that lists what is no longer there is the inaccuracy, kept |
| R3 | No name is reserved: a member named `__pygim_traits__`, or a class's own attribute of that name, is an ordinary one; the guards that refused them (F2, F6) are removed | keeping them: with the record off the class the collision cannot happen, so they would refuse a legitimate name for nothing |

`RegistryCore` gained `remove(key)` for R1, proven in `tests/static/registry_core_proofs.cpp`; every
storage engine already had `erase`. `tests/unittests/test_traits_record.py` pins R1, R2 and R4.

Still to show, before the quantity traits are built: that a composed C++ `+` is fast enough — an
A/B in one process against the hand-written one (`benchmarks/`) — and that a pybind11 function can
be composed as a method at all (it is not a descriptor, so it needs a wrapper that binds it).

## Open — D3: is a decorator enough for a quantity?

For behaviour, yes, and it is the safest hook: it runs once, on the finished class. The
alternative — a base class taking class keywords, `class Money(Quantity, magnitude="amount",
unit="currency")` — meets `dataclass(slots=True)`, which rebuilds the class and runs the base's
`__init_subclass__` again *without* the keywords (measured: the final class saw
`magnitude=None, unit=None`).

For static types, no — nothing that adds methods at run time is: Pyright and mypy would still
reject `price + tax`.

| Option | You write | Trade-off |
|---|---|---|
| A. the decorator alone, applying `dataclass` itself | `@quantified_value_object(magnitude="amount", unit="currency")` | one declaration; the operators invisible to type checkers |
| B. the decorator and a declaration-only base | the same, plus `class Money(Quantity)`, where `Quantity` exists only in pygim's stub and declares the operators | typed; two things to write, so the decorator refuses a class without the base |
| C. a base with class keywords, no decorator | `class Money(Quantity, magnitude=…, unit=…)` | typed; caught by the rebuild above |

With it, the names: the module is `pygim.traits`; the decorator `quantified_value_object` (the
owner's name) or `quantity` (Fowler's, *Analysis Patterns*) — global #105 asks for the domain's
word.

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
| provenance recorded only under `__debug__` | always, in a `TraitRecord` (a `RegistryCore` over the flat engine), kept off the class and dropped when the class is collected (2026-10-19, below) | `python -O` silently dropped it; every keyed store is a `RegistryCore` (#131) |
| Python | C++ core and adapter | the definition of done: behaviour lives in C++ |

## Open

- **Found by the generated cases (2026-10-18), not decided.** (1) `CodeBuilder` respells `co_names`
  and qualified names but not a private *local* name: `co_varnames`, `co_cellvars` and
  `co_freevars` keep `_Trait__x` where the method compiled in the target has `_Target__x`.
  Behaviour is unchanged (locals are found by position; a cell and its free variable still
  match), but `locals()` shows the old spelling, against "respells every private name". Fix it
  (respell those three too), or narrow the docstring to the names that are looked up. (2)
  `pygim.utils.is_dunder` accepts `bytes` — pybind11's string caster converts them — though its
  signature says `name: str`: refuse them, or say so. Both are declared divergences in
  `tests/unittests/traits_callspace.py`; the check fails when either stops happening.
- **The Python call sites of the naming rules,** on `core/memory`: `_interface.py` (a public method;
  a trailing underscore escaping a name), `_inventory.py` (a public module) and `test_layering.py`
  (an internal name) each write a rule by hand. Once this branch is in, they use `pygim.utils`, with
  the internal and the escaped name added to `python_names.h` beside the dunder and the private name.
- **The uninitialised-instance guard.** `pygim::adapter::refuse_uninitialised(m)` (`utils/initialised.h`,
  pygim #150) turns a method called on an instance made by `Cls.__new__(Cls)` into a `TypeError`
  instead of a crash. It is on `core/pathlike-markdown` (PR #37), not on `main`; whichever of that PR
  and this one lands second adds it as the last statement of `traits/bindings.cpp`, and #37's
  `test_uninitialised_instances.py` enforces it.
- **Static types.** Composed methods are invisible to type checkers, as anything added at run time
  is. `pygim stubs` writes pygim's own stubs; whether it should also write stubs for a user's
  composed classes, or a typing construct should declare them, is open.
- **The DDD home.** `_pygim/_core/protocols.py` holds the DDD protocols, unexported. Its
  `ValueObject` requires only `__eq__`, so a list, a dict and `object()` all pass
  `isinstance(x, ValueObject)` (measured). `quantified_value_object` gives that protocol something
  real to check.
- **Unique class ids.** `gim_type` is to give each class an id unique over the process's life —
  unlike `id()`, which a new class reuses (above). Proposed: one id space, the `gim_type` member for
  its classes and a serial in the record's side table for every other; not taking over class
  creation for all classes (a metaclass for every class conflicts with `Enum`, `ABC` and pybind11
  classes, as D1 measured). Awaiting a decision.
- **The supported Pythons, 3.9 to 3.14.** A code object has `co_qualname` only from 3.11; on 3.9
  and 3.10 the function's `__qualname__` carries the name alone.

## Sequence of work

1. core: mangling (built, proven), the override policy and requirements, with their proofs.
2. adapter: `CodeBuilder` (built); `compose()` and `traits_of()` (built): functions, properties,
   classmethods and staticmethods, the `__class__` cell, refusal before any write, the record.
3. `gim_type` and `gimmick` in C++, with `<<`.
4. The quantity traits and `quantified_value_object`; the notes' `Money` rewritten on them as the
   first user, and an example under `docs/examples/traits/`.
5. Unit tests through public names, the stub, the changelog.
