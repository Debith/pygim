# Mapping Toolkit — Design

Status: DRAFT for review · Target branch: `core/gimdict-merge`

## Goal

One mapping toolkit: fast C++ storage engines under a single Python interface,
with every capability — mutability, merging, layering, lifecycle hooks — as a
composable **trait** over a shared **storage concept**. `gimdict`, `Registry`
and the D-D-2024 character-sheet map become *instantiations* of the toolkit,
not separate towers.

Two standing rules inherited from the rest of pygim:

- **Surface honesty** — a capability that is absent has *no methods*, never
  silently-no-op ones. A frozen map has no `__setitem__`; a hookless map has
  no `add_on_register`. Misuse is a compile error (C++) or `AttributeError`
  (Python), not a quiet nothing.
- **Evidence first** — semantics are proven by compile-time suites
  (`tests/static/`), backends are chosen by the benchmark harness
  (`benchmarks/results/` series), and the Python surface is differential-tested
  against `dict`.

## Layer architecture

```
Layer 3  Python bindings   bind_mapping<M>, curated combos, engine= factory
Layer 2  traits            mutable, merge, layers, hooks, strict   (mixins)
Layer 1  gimmap<S,Ts…>  assembly; the FROZEN base surface; freeze/thaw
Layer 0  storage engines   flat, hash, interned…; py_mapping adapter
```

The key inversion: **immutable is the base, mutability is a trait.**

```
gimmap<S>                          → Mapping   (frozen, hashable)
gimmap<S, mutable>                 → MutableMapping
gimmap<S, mutable, merge>          → gimdict (today's API)
gimmap<S, mutable, merge, layers>  → character-sheet map
```

## Layer 0 — storage concept and engines

Pybind-free (`mapping/storage.h`). The concept every engine satisfies:

```cpp
template <typename S>
concept storage =
    std::movable<S> &&
    requires(S s, const S cs, typename S::key_type k, typename S::mapped_type v) {
        { cs.find(k) } -> std::same_as<const typename S::mapped_type*>;
        { s.find(k) }  -> std::same_as<typename S::mapped_type*>;
        { s.insert(k, std::move(v)) };
        { s.erase(k) } -> std::same_as<bool>;
        { cs.size() }  -> std::convertible_to<std::size_t>;
        { cs.items() };            // iterable of pairs
    };
```

Each engine additionally declares `static constexpr bool ordered` — whether
`items()` order is deterministic. This is part of the engine's *contract*, and
the Python docstring states it (dict users expect insertion order; flat gives
sorted order; hash gives none — honesty over imitation).

Engines, in build order:

| engine | structure | wins at | constexpr | source |
|---|---|---|---|---|
| `flat_storage` | sorted contiguous vector | small n, iteration, frozen | yes | extracted from `DynamicMergeMap`'s `FlatMap` |
| `hash_storage` | hash map (initially `std::unordered_map`; vendoring e.g. `ankerl::unordered_dense` only if the benchmark justifies it) | large n point lookup | no | new |
| `interned_storage` | string keys interned (KeyCache learnings) | repeated-key documents | no | later, benchmark-gated |
| `py_mapping_storage` | adapter over ANY Python `MutableMapping` | traits over plain `dict` | no | adapter layer (pybind) |

`flat_storage` being constexpr is what lets the static proof suites run — the
semantics are proven once on the flat engine; other engines are covered by the
runtime differential tests (identical trait code, so semantics can't diverge).

## Layers 1–2 — gimmap and traits

Traits are plain structs with **deducing-this** methods (C++23; the toolkit
requires `__cpp_explicit_this_parameter` — GCC 14+, MSVC 19.32+; local dev has
GCC 14.2. If a CI leg's compiler lacks it, that leg's fallback is the CRTP
spelling of the same traits, but this is a contingency, not the plan).

```cpp
struct mutable_trait {
    constexpr void set(this auto& self, const auto& key, auto value) {
        self.storage().insert(key, std::move(value));
    }
    constexpr bool erase(this auto& self, const auto& key) {
        return self.storage().erase(key);
    }
};
```

```cpp
template <storage S, typename... Traits>
class gimmap : public Traits... {
    static_assert(traits_dependencies_ok<Traits...>());   // layers ⇒ merge+mutable
    S m_storage;
public:
    // The FROZEN base surface — always present:
    //   contains, get/at, size, items, keys, values, operator==
    // Trait interface (documented internal):
    constexpr S&       storage()       noexcept { return m_storage; }
    constexpr const S& storage() const noexcept { return m_storage; }

    // Cross-type transitions:
    //   freeze(): moves storage into gimmap<S>            (drops traits, cheap)
    //   thaw<Ts...>(): copies storage into a mutable variant
};
```

Trait presence is detected structurally (`std::derived_from<M, mutable_trait>`
→ `concept has_mutable<M>`), which drives both dependency checks and the
binding layer's `if constexpr` surface emission.

### merge_trait — the strategy machinery (fold-now)

Whole-map merge is an **operation, not a product**: `a | b` folds immediately
and returns the FROZEN base map — a merged result IS a snapshot, so there is
no separate "mergedict" product. Assembly flows stay layered until observed;
what keeps fold-in-place merging alive as its own capability is **unbounded
accumulation** (QuickTimer's PhaseMap: thousands of `Sum` merges into the
same keys — layers would store every contribution, O(N) memory and O(N)
reads, where `merge_in` folds to one value, O(1)).

- **Resolution** (which strategy applies: per-key table → default; the merge
  *target* decides) lives in the trait — shared, proven by static asserts,
  never duplicated again. Stateful, so the trait is keyed:
  `merge_trait<K>`.
- **Application** (how two values combine) is a customization point found by
  ADL: `merge_combine(strategy, lhs, rhs)`. The core provides the arithmetic
  overloads (from `DynamicMergeMap::combine`); the adapter layer provides the
  `py::object` overload (`PyNumber_Add`, deep-dict recursion, list union —
  from `PyGimDict::apply`). One resolution semantics, two value domains.

Surface depends on which traits are present:

- frozen + merge: `merged(other)` / `operator|` → **`gimmap<S>` (frozen)** —
  value-semantics algebra, no mutation anywhere
- with `mutable`: additionally `merge_in(key, value)` and `merge_with(other)`
  — the in-place accumulator surface

Guidance: **assembly = layered; accumulation = merge-in-place.** `layers`
requires `merge` because observe/snapshot fold through this machinery.

### layer_trait  (requires merge + mutable)

The layered channel from the `core/gimdict-merge` rework, unchanged in
semantics: base channel + source-tagged contributions, `apply`, `remove`
(O(footprint) via the reverse index), `operator<< / >>`, `sources`,
`footprint`, `observe`.

**`snapshot()` returns `gimmap<S>`** — the frozen base type. "A NEW frozen
point-in-time view" stops being a docstring and becomes the type system:
live sheet = MutableMapping, observed sheet = Mapping.

### hooks_trait / strict_trait  (later — Registry convergence)

`hooks_trait` ports `HooksBundle` (the policy object remains the mechanism for
calls woven *inside* operations); `strict_trait` ports
`register_or_override`'s duplicate/override semantics. With both, `Registry`
becomes `gimmap<hash_storage, mutable, strict, hooks>` behind its existing
adapter — Python API unchanged. Per the surface-honesty rule, hook
registration methods exist only when the trait is present (fixes the current
silent-discard on `Registry(hooks=False).add_on_register`).

## Layer 3 — Python surface

Curated combos (each a real pybind class; the factory hides variant dispatch):

`gimdict(...)` is a **factory** (the `path()` pattern): the kwarg is the
trait's name, the returned type is the curated combo, and adapter-level C++
inheritance (the pathlike typed-files trick) makes the variants a family —
`isinstance(x, pygim.gimmap)` holds for everything the factory makes.

| spelling | composition | protocol |
|---|---|---|
| `gimdict({...}, frozen=True)` | `gimmap<engine>` | `Mapping`, hashable* |
| `gimdict({...})` | `mutable + merge` | `MutableMapping` — **existing API preserved**, `test_gimdict.py` must pass unchanged |
| `gimdict({...}, layers=True)` | `mutable + merge + layers` | gimdict family + provenance surface; `.snapshot() →` frozen |

\* hashable like tuple: hashing raises if a value is unhashable.

Transitions mirror the C++ type moves: `.freeze()`, `.thaw()`,
`.with_layers()`. Engine choice mirrors pathlike's precedent — same keyword,
same honesty about what's underneath:

```python
d = pygim.gimdict({"hp": 10}, engine="flat")     # or "hash", default "auto"
```

**pathlike hands decoded data to a factory — it never learns trait names.**
`read(into=callable)` calls any callable with the decoded object
(`json.loads(object_hook=…)` precedent); preconfigured variants are
`functools.partial(gimdict, layers=True, int="sum")`. The C++ mirror is
`read_as<T>()` — the type is the factory — dispatching construction through
a `builder<T>` ADL customization point (gimmap builder inserts pairs; the
reflection-era builder fills struct members, proven in
`experiments/reflection_bindings.cpp`). `read_as<gimmap<…>>` needs the C++
document value type — a step-4 design item.

`engine="auto"` picks by the benchmark-measured crossover (a constant sourced
from the results series, not a guess). Runtime engine choice = `std::variant`
over engine instantiations per class (the Registry pattern). Combinatorics are
controlled by curation: variants exist per exposed class, not per possible
combo.

Later phase: `pygim.merged(any_mapping, int="sum")` — merge trait over a plain
Python dict via `py_mapping_storage`. Same trait code; zero duplication.

`bind_mapping<M>` emits the protocol via `if constexpr (has_mutable<M>)` etc.
When reflection reaches CI (see `experiments/reflection_bindings.cpp` — the
shape is already proven on GCC 16.1), the emission loop is generated instead
of hand-written; nothing above it changes.

## Proofs, tests, benchmarks

- `tests/static/mapping_core_proofs.cpp` — constexpr suites on `flat_storage`:
  storage laws, merge resolution, layered reversibility/footprint (ported from
  `dynamic_merge_map.h`'s `compile_tests`), and **negative composition
  proofs**: `static_assert(!has_set<gimmap<flat_storage<K,V>>>)` — the
  frozen type provably lacks mutation. Wired into ext sources (an unwired
  static test file tests nothing).
- Python: protocol conformance + differential vs `dict` on mixed workloads;
  `test_gimdict.py` green throughout (API contract); frozen hashability;
  ordering contracts per engine.
- `benchmarks/mapping_bench.py` → `results/mapping_bench.jsonl`, one record
  per merge to main: construct / lookup / iterate / merge vs `dict` baseline,
  n ∈ {8, 64, 1k, 100k}, per engine, frozen vs mutable. The honest goal is
  published crossovers, not blanket "faster than dict" claims — per-op access
  through the binding loses to dict; typed/bulk/frozen/semantic paths are
  where wins live.

## Build order (each step lands independently, everything stays green)

1. **Skeleton**: `storage` concept + `flat_storage` (extracted) + `gimmap`
   + `mutable_trait` + static proofs + bench harness. No Python changes.
2. **merge_trait**: resolution extracted; `DynamicMergeMap` keeps its public
   shape (thin wrapper or alias) so `QuickTimer`/existing users are untouched.
3. **Python rebind**: the `gimdict` factory over the toolkit (variant over
   engines); `frozen=True` appears; `test_gimdict.py` unchanged and green.
4. **layer_trait**: `gimdict(layers=True)`, `snapshot() → frozen`; then pathlike
   `read → gimdict` lands on top (the pinned question resumes here).
5. **hooks/strict traits**: Registry convergence; silent-no-op fix.
6. **Reflection**: `bind_mapping` generation + `[[=MergeStrategy]]` member
   annotations, gated on `__cpp_reflection` (dev loop: `gcc16` conda env).

## Open questions / risks

- Apple Clang's deducing-this status decides whether the macOS CI leg needs
  the CRTP fallback (verify before step 1 merges).
- `py::object`-keyed storages (identity semantics) are deferred — string keys
  cover documents and registries; revisit if a use case appears.
- Free-threaded Python: frozen maps are trivially safe for concurrent reads;
  mutable trait under 3.13t needs the same audit pathlike deferred.
- Plain `read()` default (dict vs gimdict) — benchmark-gated, decided at step 4.
