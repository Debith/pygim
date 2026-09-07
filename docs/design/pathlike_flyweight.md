# pathlike: the flyweight store behind path()

Why `pygim.path()` hands back the same object for the same path while anything
holds it, where that identity lives, how it meets the IoC container, and what
it costs.

Status: implemented (prototype tier, with PathSet) · Owner: Debith · Date: 2026-09-07
Evidence: `benchmarks/path_store.py`, records in `benchmarks/results/path_store.jsonl`.
Builds on `pathset_storage.md` (the table) and reuses what `docs/design/wiring_ioc/`
and the survey below found already present.

## The rule

**`path()` interns; `file()` constructs.** Every value that leaves the adapter
through `path()` or a derived-path operation (`parent`, `parents`, `/`,
`joinpath`, `with_*`, `glob`, `iterdir`, ...) is interned in the *current
store* and, if that value already has a live Python object, that object is
returned. The class constructor `file(...)` (and the typed `jsonfile(...)`,
...) is the raw value constructor and never interns — the `str` /
`sys.intern` split. A pinned path (`engine=`) is never interned either: a pin
is a per-object choice, not part of the value, and derived values inherit it.

```python
path("a//b.yaml") is path("./a/b.yaml")        # one value, one object
path("a/b.yaml").parent is path("a")           # derived paths too
file("a/b.yaml") is not path("a/b.yaml")       # the raw constructor
path("x.json", engine="toml") is not path("x.json", engine="toml")   # pins bypass
```

## What a store is

`pathlike.PathStore` (`adapter/path_store.h`) is two things over one
`path_table` (`path_table.h`):

- **the C++ cache is the table.** Every distinct segment once, every path a
  row — the same structure `PathSet` uses. Interning a value is
  `path_table::insert`; nothing here is a second dictionary of values.
- **the Python cache is one weak slot per row.** `m_slots[row]` holds a
  weakref to the row's live object (pybind11 instances are weak-referenceable
  by default; the survey confirmed `tp_weaklistoffset` is set on the shared
  base). Lookup is an array index, no hashing. The store pins nothing: an
  object dies with its last owner and the slot is refilled on the next
  request. Only the table grows.

Each object the store hands out is stamped with an opaque `(owner, slot)`
token (`basic_file::interned()`, core.h). The token is not part of the value,
equality or hash; a copy carries it along, so the store validates it against
its own slot's referent before trusting it (`path_store::row_of`). A raw
`file` copied from an interned one therefore falls back to the value route
rather than being mistaken for the interned object. With a valid token,
`parent` is `path_table::parent(row)`, `parents` is the chain, and
`p / "name"` for one plain component is `path_table::child_of(row, name)`:
no value is built and nothing is re-probed segment by segment. Multi-component
joins, absolute right-hand sides, `with_*`, and pinned objects take the value
route; the parity tests replay both routes over the corpus and the edge list.

Native path text is tokenised straight into the table
(`Strategy::tokenise`, proven equal to `parse_into` in the static proofs), so
a warm `path(s)` builds no value at all. Text with a scheme (`file://...`)
takes the constructor's parse, which owns the URI rules.

## The IoC container's part — and what it is not

The survey of `wiring/ioc/` before this design found two facts that shaped it:

1. **There is no scoped lifetime.** `Lifecycle` is `Transient | Singleton`
   and `test_ioc.py` asserts `"scoped"` is rejected. A "scope" can only mean
   one `Container` object's lifetime.
2. **The container stores and returns Python objects only** and validates
   `isinstance(instance, interface)` on resolve.

So a store is an ordinary Python object, and "scoping the path cache to a
container" is:

```python
container.register(PathStore, PathStore, lifecycle="singleton")
with pathlike.use_store(container.resolve(PathStore)):
    ...          # every path() in the block interns into that container's store
```

The store dies with the container. `pathlike.store()` is the current store
and `use_store()` is a nesting context manager; the module owns a default
store (`pathlike._default_store`) that is current until told otherwise. The
hot path does not resolve through the container — `current_store()` is a
pointer read — because a resolve per `path()` would cost more than the parse
it saves.

If a real `Scoped` lifetime is added to the container later (child scopes
with disposal), the store needs no change: register it scoped and
`use_store` the resolved instance.

## Measured (2026-09-07, 200k distinct paths, best of 3)

Cross-run numbers on this machine drift with host load; compare within a run.

| construct | ns/path |
|---|---|
| `pathlib.PurePath(s)` | 421 |
| `file(s)`, raw constructor | 788 |
| `path(s)` warm: object alive, lookup only | 371 |
| `path(s)` cool: row interned, object dead | 1,056 |
| `path(s)` cold: fresh store, table growing | 1,076 |

| derived | ns/op |
|---|---|
| `f.parent`, raw | 364 |
| `p.parent` warm | 208 |
| `p.parent` cool | 668 |
| `p / "x"` warm | 369 |
| `p / "x"` cool | 1,044 |

| memory (each variant in a fresh process) | bytes |
|---|---|
| a raw `file()` object | 540 |
| a live `path()` object | 539 |
| the store per interned path (table + slot) | 162 |
| live objects after `del` + `gc.collect()` | 0 |

Read it as:

- **Warm wins, cool costs.** A repeated value is a table lookup: 2x faster
  than the raw constructor, and a warm `parent` is 40% cheaper than the raw
  one. A value seen for the first time — or whose object has died — pays the
  interning probes plus a weakref allocation on top of construction: 1.3x to
  2.9x the raw cost. The flyweight is a win where paths repeat (a tree walked
  more than once, parents shared by siblings, lookups against a set) and a
  loss where every path is new and touched once.
- **No memory saving per live object.** A live object still holds its own
  value (539 vs 540 bytes). The saving is structural: N references to one
  object instead of N objects, and a dead value costs 162 bytes of table
  rather than 540 of object. A process that walks a large tree keeps the rows
  for the life of the store — sized in `PathStore.stats()`, bounded by the
  store's owner.
- **Speed was never the main argument.** Identity and the shared table are:
  `f in PathSet(..., store=s)` is a bitmap test, `view.to_file()` on a set
  over the store's table is a slot read, and the cross-table mapping that
  cost 300 ns/element in `pathset_storage.md` does not occur between a store
  and the sets made over it.

## What was reused, and what was not

*(2026-09-08: the pieces below that were pathlike-local are now toolkit
components — `mapping/intern.h`, `mapping/trie.h`, `mapping/id_set.h`,
`utils/flyweight_adapter.h`, `utils/ambient_adapter.h`, `utils/hash.h` — with
their laws proven at compile time. See `mapping_toolkit.md`.)*

- `path_table` / `segment_table` as the interning structure (no new hash
  table); `path_table::row_map` untouched; `KeyCache`
  (`adapter/materialize.h`) as the precedent for a bounded interning cache.
- The `each` module's `WeakKeyDictionary` flyweight (`each/adapter.h`) is
  the precedent for weak identity; this design uses the C API weakref per row
  instead of a dictionary because the row IS the key.
- The mapping toolkit's `storage` concept was checked and cannot host
  `segment_table` (owned-key `find`, no heterogeneous lookup, mandatory
  `erase`); that finding is recorded in `pathset_storage.md` and stands.
- `fnv1a` / `mix_string` stay where they are (`pathlike/core.h`); no
  duplicate hashing was found elsewhere to unify with.

## Rules to keep

- **Single writer under the GIL.** Interning and slot refills happen in
  adapter code holding the GIL; the table has no lock (`pathset_storage.md`).
- **The store validates, the core does not.** `interned()` is an opaque
  token; only `path_store::row_of` may interpret it, and only after checking
  the slot's referent is the asking object.
- **Nothing pins.** A store must never hold a strong reference to a path
  object; `stats()["live"]` after `gc.collect()` is the test.

## Next

1. **Cool-path cost.** ~600 ns over the raw constructor: the value is
   rebuilt from the row (`path_table::value`), then wrapped, then a weakref
   is allocated and the token stamped through a `py::cast`. Building the
   `file` from the text that is already at hand when it is text, and stamping
   without the cast, are the two obvious cuts.
2. **The old `pygim.pathset` module.** `file.pathset()` still renders glob
   results to strings and constructs `pygim.pathset.PathSet`; with a store
   and `PathSet(store=)`, it should return the pathlike `PathSet` over the
   current store's table. Folding the old `Filter`/`Query` vocabulary into the
   native filters is the "filter vocabulary" item of `pathset_storage.md`.
3. **A scoped lifetime in `ioc`** (child containers with disposal) would
   turn `use_store(container.resolve(PathStore))` into the container's own
   job. Independent of pathlike.
