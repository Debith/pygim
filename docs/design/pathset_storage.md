# pathlike: PathSet as one table

Why many paths are stored as a single table rather than as objects, what the
prototype benchmark showed that design is good for, and the decision to defer
the Arrow boundary until there is a use that needs it.

Status: decision · Owner: Debith · Date: 2026-09-06
Evidence: `benchmarks/pathset_prototype.py`, records in
`benchmarks/results/pathset_prototype.jsonl` (1M and 10M paths, Ryzen 7 5800X).

## The rule

**A path in a set is an index, not an object.** A `PathSet` owns a shared
`path_table` and a member list. The table stores every distinct segment once
(`segment_table`, dictionary encoding) and every distinct path as one row
`(parent row, segment id)` — a hash-consed trie, so the directory tree is
stored once and a file path costs eight bytes plus its share of the unique
names. Views (`fileview`) are `(table, row)` and copy nothing; the table is
append-only, so a row index never dangles. A filtered set shares its parent's
table and is only a member list plus a bitmap.

## What the structure buys, measured

Best of 3, 1M paths, garbage collector off. "Objects" is a list of
`pygim.path` instances; "pathlib" is a list of `PurePath`.

| Workload | PathSet | objects | pathlib |
|---|---|---|---|
| build, ns/path | 574 | 1,030 | 456 |
| memory, bytes/path | 104 | 492 | 209 |
| `filter_suffix`, ns/path | 11 | 179 | — |
| `filter_name` (glob), ns/path | 6 | — | — |
| `a \| b`, `a & b` on one table, ns/element | 1–2 | 8–28 (Python sets) | — |
| `x in set`, ns/probe | 869 | 685 | — |
| `for v in ps.scan()`, ns/element | 92 | 7 | — |
| `v.parent` / `f.parent`, ns/element | 544 | 771 | — |

Read it as three facts:

1. **Bulk operations are where the table wins.** Filters run ten to thirty
   times faster than a Python loop, and algebra between sets over the same
   table is a bitmap operation. Memory is half of pathlib and a fifth of a
   list of path objects.
2. **Per-element Python access is the weak spot.** Handing out a view costs a
   Python object per element; a list only bumps a refcount. `scan()` halves
   that by reusing one view, and once an attribute is touched the two sides
   are within noise. The API should push users toward table-level operations
   and keep per-element loops for the tail.
3. **Merging two independent tables is the worst number in the run.** Union
   across tables cost 561 ns/element because every row of one table is
   re-rendered and re-inserted into the other. Sets derived from one parent
   merge for free; sets from two scans of different roots do not, yet.

## Decision: no Arrow boundary, for now

The prototype exported `paths()` and `names()` through the Arrow C Data
Interface and read columns back with `from_arrow()`. The benchmark section
that measured it does not justify keeping it:

| Step, 1M paths | ms |
|---|---|
| render `paths()` then hand to pyarrow | 146 |
| `pyarrow.array(list_of_str)` from existing strings | 42 |
| `to_list()` then `polars.Series` (the bridge without Arrow) | 212 |
| `polars.from_arrow(pyarrow.array(ps.paths()))` (the bridge with Arrow) | 162 |
| `PathSet.from_arrow(series)` (re-parse every path) | 542 |

- The full-path column is **not** zero-copy: the table stores a trie, so the
  text has to be rendered first, and that render is slower than pyarrow
  building the same array from Python strings. Only `names()` was genuinely
  zero-copy, because its dictionary layout is the table's own layout.
- Arrow gave no speed to anything PathSet already does. A native
  `filter_suffix` is an order of magnitude cheaper than render-then-polars.
- The one thing Arrow provides is a hand-off to DataFrame tools, and the
  plain Python-list bridge is only a quarter slower for that. When someone
  needs a DataFrame, `to_list()` is fine until proven otherwise.
- Linking `libarrow` would be worse than the C interface, and the repo
  already carries the reasons: SONAME coupling to the build-time pyarrow, a
  hard import of a ~100 MB package, RPATH-into-site-packages resolution that
  breaks editable installs, and the two-runtimes seam behind the 2026-09-01
  segfault. See `cpp_runtime_linking.md`. If PathSet ever needs parquet or
  IPC on its own, that goes in a separate optional extension declaring the
  `arrow` dependency, as `persistence` does; the pathlike core stays free
  of it.

So: the export machinery (`arrow_c_abi.h`, the `column` / `arrow_column`
types and their capsules, `PathSet.paths()`, `names()`, `from_arrow()`, the
"arrow boundary" benchmark section and its tests) is removed from the
prototype. The `to_list()` bridge stays.

## What is kept from that work

- **The column layouts.** The segment interner is text plus offsets;
  the trie's rows are fixed-width integers. Those are good
  structure-of-arrays shapes on their own merits, and they happen to be
  Arrow's `large_utf8` and dictionary layouts, so the door is not closed. Do
  not add per-row heap objects to the table. (Since 2026-09-08 both are
  toolkit components — `mapping/intern.h`, `mapping/trie.h`, and the set is
  `mapping/id_set.h`: see `mapping_toolkit.md`; `path_table` is the path
  policy over them.)
- **Stable row identity per table.** Everything cheap — filters, algebra,
  views, membership — rests on a row number meaning the same path for the
  life of the table. Any future export or query language should carry row
  ids rather than rendered text, because a row id comes back into a set as
  a member-list-plus-bitmap operation at ~1 ns/row and a string comes back
  through the parser at ~500 ns.
- **Append-only, single writer.** Rows are never removed or renumbered.
  Mutation happens through `add`/`extend` on the owning thread; views and
  derived sets only read. Any cache that is added to the table later (a
  rendered-text cache was considered) must respect this or bring its own
  lock; the current table has none and should stay that way until a
  measured need appears.

## Optimisations applied (2026-09-06), measured A/B

Same process, same corpus (1M paths), old and new implementation compiled
side by side and interleaved, best of 15; cross-run numbers on this machine
drift by 30-50% with host load, so only same-process ratios are trusted.

| Change | Where | Before | After |
|---|---|---|---|
| render: one chain walk, string sized up front, bare-root anchor text cached | `path_table::render` | 129 ms | 83 ms |
| sized construction reserves both hash tables (`PathSet.reserve`, automatic for sized iterables) | `path_table::reserve` | 768 ms | 664 ms |
| the four authority-less anchor rows cached instead of re-interned per path | `path_table::anchor_row` | (in the build figure above) | |
| hash/value/head_of share the same single walk | `path_table::with_chain` | 52 ms | 52 ms (no change, no regression) |
| cross-table union copies the larger table and maps only the other set; the mapper memoises per segment id as well as per row | `PathSet::union_with`, `path_table::row_map` | 1,121 ms | ~600 ms |

Two findings from the A/B worth keeping:

- The chain walk must hand rows out **leaf first** and let callers iterate
  backwards. A root-first version with a `std::reverse` cost 12% on `hash`
  against the recursive original; leaf-first is exactly even.
- A cross-table union is now a memcpy of the larger table plus one
  hash-consing insert per row of the smaller, ~300 ns/element. What remains
  is the cache-missing probe into a large table per inserted row; the string
  hashing is gone.

## Next

The effort Arrow would have taken goes to the two gaps the benchmark found:

1. **A native filter vocabulary.** Without an expression engine on the far
   side of a boundary, the table must answer the questions people actually
   ask: suffix, name glob, absolute/relative (present, and since 2026-09-08
   composable as `Filter` predicates — `ext`, `name`, `absolute` with `&`,
   `|`, `~` — through the lazy `Query` the old `pygim.pathset` module had),
   plus depth, "under this directory" (a parent-row test, cheap in a trie),
   and a regular expression on the name (runs once per distinct segment, not
   per path, because names are dictionary-encoded).
2. **Cross-table merge, the rest of the way.** Union now maps segment ids
   once and links rows parent-first into a copy of the larger table (see the
   A/B above). Intersection still probes the other table once per member of
   this one (~170 ns/element). Both are bounded by random probes into a
   large hash table; the next step, if it matters, is a merge that walks
   both tries in parent order so the probes become sequential.
3. **Per-element Python access.** A fresh `fileview` per element costs a
   pybind11 instance (~200-300 ns). `scan()` halves it. A lighter holder,
   or a batch accessor (`names()`, `parents()` as lists in one call), is the
   remaining lever; the table itself is not the cost.

Both are measured by the existing benchmark: sections 3 and 4 already hold
the baselines, and `_results.py` records each run against the commit.
