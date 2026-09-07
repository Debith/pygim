# The mapping toolkit

The pybind-free tables every fast component is built from, the laws each
one keeps, and the two adapter-layer utilities that put their rows in front
of Python. Everything here is `constexpr` end to end, so the same code is a
runtime table and a compile-time proof.

Status: implemented · Owner: Debith · Date: 2026-09-08
Where: `src/_pygim_fast/mapping/` (core), `src/_pygim_fast/utils/` (hashing
and the adapters). Proofs: `tests/static/mapping_proofs.cpp` (compiled into
`pygim.registry` — see `ext.registry.toml`) and, for the path table's laws,
`tests/static/pathlike_core_proofs.cpp`.

## Why concepts, not one container

Two shapes of table recur across pygim and they are not the same thing:

- **storage** — a key -> value container that owns both. The caller chooses
  the value; entries come and go. Engines: `flat_storage` (sorted, constexpr,
  `ordered`) and `hash_storage` (`std::unordered_map`). The registry and the
  factory are written against it. (`storage.h`, `hash_storage.h`)
- **interner** — a table that owns the *bytes* of every distinct key once and
  *assigns* the value: the id is the insertion index. It is a bijection
  (bytes -> id, id -> bytes), it is looked up by bytes that may live anywhere,
  and it is append-only, so an id never moves and a row elsewhere may hold
  it. Those three properties are exactly what `storage` cannot express
  (owned-key `find`, caller-supplied value, mandatory `erase`) — which is why
  this is a second concept beside it rather than a storage engine.
  (`intern.h`)

The rule for this folder is pygim's: it is a generic library meant for
learning advanced approaches, so a component is written as the general thing
— a concept with engines, a template over its id, key, word or policy type —
even while one instantiation is all that exists, and its laws are proven over
more than one instantiation. Generality is the point, not something a second
consumer has to earn.

## The components

| Component | Concept / type | Engines | What it is | First consumers |
|---|---|---|---|---|
| `intern.h` | `interner` | `flat_interner` (sorted id index, binary search, small tables + constant evaluation), `hashed_interner` (open addressing, load <= 1/2, also constexpr-capable) | every distinct string once, dense ids in insertion order | `pathlike::basic_path_table` (segments) |
| `trie.h` | `trie` | — | hash-consed rows of `(parent, key)`: a shared prefix is one row; `child()` is find-or-add; `with_chain()` gathers a chain leaf-first into a stack buffer; `row_map` maps rows from another trie memoised per source row through a key translator | `basic_path_table` (the directory tree) |
| `id_set.h` | `basic_id_set<Id, Word>`; `id_set` = `<uint32_t, uint64_t>` | — | dense ids of any unsigned width in the machine's word: insertion-ordered members plus a bitmap; `where`, `united`, `intersected`, `subtracted` at a few ns per element; `count_united` & co answer the SIZE of an algebra result as a popcount over the bitmaps, 64 ids per step, no set built (20-90x the build at 1M paths) | `pathlike::PathSet` (`count_union`, `count_intersection`, `count_difference`) |
| `../utils/memory.h` | `memory::resident_bytes`, `peak_resident_bytes` | — | the process's resident memory as one syscall (Linux procfs pread on a descriptor opened once, Mach task info, Windows working set): a benchmark's before/after probe, never a per-object size — components report exact `bytes()`; `pygim.utils.rss_bytes()` et al. | `benchmarks/_bench.py` |
| `../utils/hash.h` | — | — | `fnv1a`, `mix_string`, `mix64`, `combine`, `slots_for`: one definition of every hash the tables share | core.h, intern.h, trie.h, the wiring adapters' key hashes |
| `../utils/flyweight.h` | `flyweight::token`, `stamped` | — | the `(owner, slot)` identity a store stamps on a value; not part of equality or hash; a copy carries it, so a reader validates it | `basic_file::interned()` |
| `../utils/flyweight_adapter.h` | `adapter::weak_slots<T>` | — | one weak reference per id -> the live Python object for that id; `id_of()` validates a token by checking the slot's referent IS the asking object; pins nothing | `pathlike::path_store` |
| `../utils/ambient_adapter.h` | `adapter::ambient<T>` | — | the current instance of a service (a pointer read on the hot path), `set()`, and a bound `use_<x>()` context manager; the holder is a leaked `py::object` | `pathlike::current` (the current store) |

### The path table as a policy over the toolkit

`pathlike::basic_path_table<Interner>` is the trie and an interner with the
path semantics on top: anchors as root rows, the strategy tokeniser feeding
segments, `value(row)`, `hash(row)`, `render(row)`, `parents(row)`.
`path_table` is `basic_path_table<hashed_interner>`. The interner is the
policy exactly as `flat_storage` / `hash_storage` are for a registry: the
proofs instantiate `basic_path_table<flat_interner>` in constant evaluation
and assert, over POSIX and Windows spellings, that

- `value(row)` is the `uri` `file(text)` holds,
- `hash(row)` is that file's `hash_value()`,
- `render(row)` is its `fspath()`,
- text and value meet at one row (`insert(text) == find(value) == insert(value)`),
- the table's `parent` is pathlib's parent, and two spellings of one value
  are one row while siblings share theirs.

Those are the facts `PathSet` (views compare and hash like files) and the
flyweight store (`parent` and `/` by row) rely on. Before this they were
comments in `path_table.h`; now a build that breaks one does not link.

## Laws (what `mapping_proofs.cpp` asserts)

**interner**, over both engines: ids are dense and in insertion order;
`intern` is idempotent; `find` never adds and answers `npos` for an absent
key; `operator[]` round-trips; the empty string and a prefix are distinct
keys; `reserve` and growth (200 keys, through several rehashes / re-sorts)
keep every earlier id.

**trie**: hash-consing (a shared prefix is one row, `child` is idempotent);
`parent`, `key`, `is_root`, `depth`, `root_of`; `find_child` never adds and
`no_key` is never found; `with_chain` is leaf-first and `for_chain`
root-first; a chain deeper than the stack buffer (100 rows) still walks;
`reserve` keeps every row; `row_map` in lookup mode maps absent rows to
`none` and in find-or-add mode creates them under the mapped parent, both
memoised.

**id_set**: `note` deduplicates and keeps insertion order; `has`; the three
algebra operations produce "mine, then theirs"; `where` filters; `sibling()`
is an empty set over the same width; the three counts agree with the built
sets at every bitmap-width mismatch, including against an empty set.

On popcount: the extensions compiling the toolkit pass `-mpopcnt` when the
compiler accepts it (`flags_if_supported`). Measured under the build's
`-march=nocona`, `std::popcount` without it is a library call at 3.4 ns per
word, an 8-lookup byte table 2.1 ns, the SWAR bit-parallel form 1.6 ns and the
instruction 1.1 ns — so the flag, not a table, is the fix; MSVC and ARM pick
the instruction on their own.

## Rules

- **Core headers are pybind-free and constexpr.** `mapping/` and
  `utils/hash.h`, `utils/flyweight.h` include nothing from pybind11; the two
  `*_adapter.h` files are the only ones that do.
- **Append-only, single writer.** Interners, tries and id_sets never remove
  or renumber. Whoever mutates one holds the GIL when a Python object can
  observe it (`pathset_storage.md`).
- **A `weak_slots` is owned by a Python-visible object, never a static.** Its
  destructor decrements references and must run under the GIL; an `ambient`
  holder is leaked for the same reason.
- **Tokens are validated, never trusted.** Only `weak_slots::id_of` may
  interpret a `flyweight::token`, and only after the slot's referent check.

## Next

- `hash_storage` should accept a transparent hash and equality so a
  `hash_storage<std::string, V>` can be probed with a `string_view`; the
  one-off transparent hash in `pathlike/adapter/materialize.h` (`KeyCache`)
  then goes away.
- The `each` module's `WeakKeyDictionary` flyweight and a current-container
  scope for `ioc` are the second consumers of `weak_slots` and `ambient`
  respectively; both are migrations, not new design.
- A flyweight policy (`basic_file<Strategy, Token>` with an empty token, a
  `no_flyweight` adapter) is the next thing the rule above asks for
  (`pathlike_flyweight.md`).
- The trie and the interners are the remaining non-templates over their id
  width; `basic_trie<RowId, Key>` and `basic_hashed_interner<Id>` would
  complete the pattern `basic_id_set` set.
