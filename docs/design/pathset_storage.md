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
an authority) are root rows. The table is append-only: a row is never removed
and never renumbered, so a row id held anywhere stays valid — it never
dangles.

A `pygim.path` object is a handle `(table, row, pin)` (`adapter/pathview.h`).
Name, parent, suffix, equality and hash are read straight from the table, by
row. For the operations whose rules live in the core value (`basic_file`,
`core.h`), the handle builds that value from the row on demand, applies the
operation there, and interns the result back into the table. A `PathSet`
(`adapter/pathset.h`) is a member bitmap over a table (`mapping/id_set.h`).
A `PathStore`
(`adapter/path_store.h`) is a table with a Python face. `path(text, store=s)`
puts the row in s's table, and every path derived from it stays in that table
too — so the argument chooses the table a path and its derivations live in.
A row is never removed, so it lives exactly as long as its table does, and
choosing the table is choosing how long those rows stay alive.

Semantics come from the core, not from string tricks. The strategy's
`tokenise()`, which feeds the table its segments, produces exactly what the
core's `parse_into()` produces; `value(row)` is the uri `file(text)` holds;
`hash(row)` is that value's hash; and `render(row)` is its text. Each of
these equalities is proven in constant evaluation over the flat interner in
`tests/static/pathlike_core_proofs.cpp`.

## What it buys, measured

1M paths, best of 3, garbage collector off; "table" is the PathSet holding
every path as one table, "pathlib" a list of `PurePath`. (The benchmark also
runs a third arm, a list of pygim path objects; its numbers are in
`benchmarks/results/pathset_prototype.jsonl` and are not shown here.)

| Workload | table | pathlib |
|---|---|---|
| memory, bytes/path | ~100 (+ ~140 per live handle) | ~210 |
| `filter_suffix`, ns/path | ~11 | — |
| `a \| b`, `a & b` on one table, ns/element | 1–2 | 8–28 (Python sets) |
| `count_intersection`, ns/element | ~0.03 (a popcount) | — |
| `p.parent`, ns | ~300 | ~890 |
| `path(s)`, ns | ~430 | ~380 (parses nothing until used) |

Bulk operations and derived paths are where the table wins. A handle per
element still costs a Python object each, so `scan()` creates one handle and
re-points it at each row for the length of a pass.
Merging two tables maps the smaller set's rows into a copy of the larger
(`path_table::row_map`, memoised per row and per segment), ~300 ns/element.

## No Arrow boundary

An export of the rendered path column through the Arrow C Data Interface was
built and measured. The path column is not zero-copy: a path exists as trie
rows, so its text must be rendered before it can be handed over, and that
render costs ~125 ms per million paths — more than pyarrow needs to build
the same array from Python strings. Only the dictionary-encoded names column
truly was zero-copy. The list bridge (`to_list()` then a polars Series)
was a quarter slower than the Arrow route. But keeping the Arrow route means
linking `libarrow`, and that link would inherit the persistence extension's
SONAME and runtime-seam problems (`cpp_runtime_linking.md`). The export was
removed; the slower `to_list()` is the way out. If parquet or IPC are ever needed without Python in the loop, they go in
a separate optional extension declaring the `arrow` dependency, as
`persistence` does.

## Rules to keep

- **Append-only, single writer.** Rows are never removed or renumbered.
  Whoever inserts holds the GIL, and the GIL is what serialises the writers —
  the table has no lock of its own.
- **Row ids are per table.** Two sets or paths may only be combined by row
  when their tables are the same object; otherwise rows are mapped by chain
  (`path_table::find` across tables, `row_map` for many).
- **No per-row heap object, ever.** Storage is structure-of-arrays throughout.

## Open

- Filters beyond suffix, name glob and absoluteness: depth, "under this
  directory" (a parent-row test), a regular expression on the name (names are
  dictionary-encoded — each distinct segment is stored once — so the
  expression runs once per distinct segment, not once per path).
- Cross-table intersection still probes the other table once per member
  (~170 ns/element); a merge that walks both tries in parent order would make
  the probes sequential.
- One Python string per distinct segment, created on first request, would
  make `p.name` and `to_list()` hand back the same object each time (~30 ns
  instead of ~105) at ~60 bytes per distinct segment.
