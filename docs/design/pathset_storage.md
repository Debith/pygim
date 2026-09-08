# pathlike: paths as one table

Why a path is a row of a table rather than an object, what that buys, and the
one boundary that was tried and dropped.

Owner: Debith · Evidence: `benchmarks/pathset_prototype.py` and
`benchmarks/path_store.py`, records under `benchmarks/results/`.

## The rule

**A path is an index, not an object.** A `path_table` (`path_table.h`) is the
mapping toolkit's trie of `(parent row, segment id)` over its interner of
segments (`mapping_toolkit.md`): every distinct component stored once, every
distinct path one row, the directory tree shared. Anchors (the absolute flag,
an authority) are root rows. The table is append-only, so a row never
dangles.

A `pygim.path` object is a handle `(table, row, pin)` (`adapter/pathview.h`):
name, parent, suffix, equality and hash are read by row; the core value
(`basic_file`, `core.h`) is built on demand for the operations whose rules
live there and interned back. A `PathSet` (`adapter/pathset.h`) is a member
bitmap over a table (`mapping/id_set.h`). A `PathStore`
(`adapter/path_store.h`) is a table with a Python face: `path(text, store=s)`
puts the row in s's table and every derived path stays there, so a lifetime
is chosen by argument.

Semantics come from the core, not from string tricks: a strategy's
`tokenise()` feeds exactly what `parse_into()` produces, `value(row)` is the
uri `file(text)` holds, `hash(row)` is that value's hash, and `render(row)`
its text — proven in constant evaluation over the flat interner in
`tests/static/pathlike_core_proofs.cpp`.

## What it buys, measured

1M paths, best of 3, garbage collector off; "objects" is a list of path
handles, "pathlib" a list of `PurePath`.

| Workload | table | pathlib |
|---|---|---|
| memory, bytes/path | ~100 (+ ~140 per live handle) | ~210 |
| `filter_suffix`, ns/path | ~11 | — |
| `a \| b`, `a & b` on one table, ns/element | 1–2 | 8–28 (Python sets) |
| `count_intersection`, ns/element | ~0.03 (a popcount) | — |
| `p.parent`, ns | ~300 | ~890 |
| `path(s)`, ns | ~430 | ~380 (parses nothing until used) |

Bulk operations and derived paths are where the table wins; a handle per
element still costs a Python object, so `scan()` reuses one for a pass.
Merging two tables maps the smaller set's rows into a copy of the larger
(`path_table::row_map`, memoised per row and per segment), ~300 ns/element.

## No Arrow boundary

An export of the rendered path column through the Arrow C Data Interface was
built and measured. The path column is not zero-copy — the trie must be
rendered first, at ~125 ms per million paths, more than pyarrow needs to
build the same array from Python strings — and only the dictionary-encoded
names column truly was. The list bridge (`to_list()` then a polars Series)
was a quarter slower than the Arrow route, and linking `libarrow` would
inherit the persistence extension's SONAME and runtime-seam problems
(`cpp_runtime_linking.md`). The export was removed; `to_list()` is the way
out. If parquet or IPC are ever needed without Python in the loop, they go in
a separate optional extension declaring the `arrow` dependency, as
`persistence` does.

## Rules to keep

- **Append-only, single writer.** Rows are never removed or renumbered;
  whoever inserts holds the GIL. The table has no lock.
- **Row ids are per table.** Two sets or paths may only be combined by row
  when their tables are the same object; otherwise rows are mapped by chain
  (`path_table::find` across tables, `row_map` for many).
- **No per-row heap object, ever.** Storage is structure-of-arrays throughout.

## Open

- Filters beyond suffix, name glob and absoluteness: depth, "under this
  directory" (a parent-row test), a regular expression on the name (once per
  distinct segment, since names are dictionary-encoded).
- Cross-table intersection still probes the other table once per member
  (~170 ns/element); a merge that walks both tries in parent order would make
  the probes sequential.
- One Python string per distinct segment, created on first request, would
  make `p.name` and `to_list()` hand back the same object each time (~30 ns
  instead of ~105) at ~60 bytes per distinct segment.
