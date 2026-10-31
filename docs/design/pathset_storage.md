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

A `pygim.path` object is a handle `(table, row, pin)` (`pathview.h`; bound in `adapter/path_bindings.h`).
Name, parent, suffix, equality and hash are read straight from the table, by
row. For the operations whose rules live in the core value (`basic_file`,
`core.h`), the handle builds that value from the row on demand, applies the
operation there, and interns the result back into the table. A `PathSet`
(`pathset.h`; bound in `adapter/pathset_bindings.h`) is a member bitmap over a table (`mapping/id_set.h`).
A `PathStore`
(`path_store.h`; bound in `adapter/path_store_bindings.h`) is a table with a Python face. `path(text, store=s)`
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

## Reading the members: `read_each` and `read_all_files`

The goal (Debith, 2026-10-25): read a set's files fast and in parallel, so that
while the disk is busy something uses the CPU — with as little as possible
under the GIL, and no Python object per file beyond a view.

The shape, chosen by experiment (a lab over 1,756 files of CPython's Lib,
32 MB, on a warm cache, a cold one — pages dropped with
`posix_fadvise(DONTNEED)` on ext4 — and the 9p mount `/mnt/c`; 16 logical
cores):

```
PathSet.read_each()                         Python: one FileContent view per file, in member order
  └─ batch_read (pathlike/batch_read.h)     the members' values, copied with the GIL held
       └─ ordered_pool (utils/parallel.h)   workers: read a file, run the Stage, put it in its slot
            ├─ file_reads.h                 one open, one fstat of the descriptor, the reads
            ├─ the width                    elastic: the consumer alone, then as the rest is worth
            └─ the window                   at most 1,024 results and ahead_bytes, drained by halves
```

| Decided | Measured (ms) | Rejected, and why |
| --- | --- | --- |
| A worker reads a file and runs its Stage itself | best or tied in every case | separate reader and CPU stages: up to 2x slower (hand-offs); `mmap` + `MAP_POPULATE`: 3-8x slower; kernel read-ahead hints (`WILLNEED` from a hinter thread): help only a lone consumer |
| The consumer works too: it reads the file it needs when nobody has | warm read 1.9, against 2.3 for the best fixed width | a pool the consumer only waits on: a hand-off per file |
| Elastic width: grow while one more thread saves more than the ~20 µs it costs to start | within 10-20% of the best fixed width on every storage | a probe of the first 32 files (90 ms on the 9p mount); throughput feedback (converges too slowly: 112 against 73 on CPU-heavy work) |
| Up to the cores for work that computes, up to max(32, 2 x cores) for work that waits — the share read by sampling the thread's CPU clock on every 16th task | warm read 1.9 (16 threads: 2.4, 32: 8.3); cold read 17.2 (16: 28.0) | a fixed cap: no one cap wins warm and cold; #57's "threads lose on a fast disk", true of Python threads only |
| A window of 1,024 results, bounded by bytes, drained by halves | CPU-heavy 67 against 131 with a 64-result window; 20,000 small files 11 against 144 when workers were woken per result | waking a worker per result handed out (a system call on the consumer's path each time); fewer than half the window's slots per wake-up (a herd); more threads than half the slots |
| Waiting on 32-bit atomics | 16 idle workers: 30 ms of CPU, not 390 | 64-bit atomics: libstdc++ parks their waiters on a shared table, spins and wakes all that hash alike |
| `__next__` lets the GIL go only when the result is not ready | | releasing it per file: a busy Python thread then takes it each time, and the loop waits up to a switch interval to get it back |
| Views: the bytes stay in C++, `memoryview` copies nothing | warm read 3.1, against 11.1 for `read_all_files` (which builds a str per file under the GIL) | a `bytes` or `str` per file |

Results end to end, against Python's `open().read()` loop: warm 3.1 against
16; cold 28 against 283; the 9p mount 424 against 5,280; cold with GIL-held
work on each file 28 against 292 (the work hidden behind the reads); 20,000
files of 260 bytes 23 against 152. Measured on 2026-10-25 by a lab (`pipelab`:
`lab.cpp`, `realbench.cpp`, `pybench.py`) that was a scratch experiment and is
not in the repository.

`benchmarks/pathset_read.py` reproduces the end-to-end rows: warm, cold
(`--cold`), the small files, and a `**` glob against pathlib's. On 2026-10-30
(CPython's Lib, 1,785 UTF-8 files, 30.9 MB; `read_each` views against the
loop's bytes): warm 4.2 against 19.2; cold 433 against 987; 20,000 small files
20.7 against 159 (`read_all_files`, a str each: 13.6); the glob 3.8 against
10.4. The cold rows differ from the lab's by more than the code changed: 31 MB
in 433 ms is about 70 MB/s, the virtual disk's own rate, where the lab's 28 ms
would be 1.1 GB/s — `posix_fadvise(DONTNEED)` drops the guest's pages only,
and under WSL2 the host's cache can still answer. A cold row says which it
measured only beside the device's rate.

### Direction, decided 2026-10-25 (#270, #271, #272)

`read_each` was built with Python as the consumer of every file. Debith then
set the rule for every pygim operation: *"The entire pipeline — walking files,
reading them, parsing them, building the ast tree — are pure c++. Python is
simply single command to start it... Python is essentially just showing and
inspecting results."* What follows from it for this component:

| Id | Decided | Rejected |
| --- | --- | --- |
| #270 | an operation runs to completion in the core; one Python call starts it, with the GIL released throughout; Python objects only on request, to inspect | Python, or the adapter, taking part in the run |
| Q23 | the parsed tree is pygim's own, shown through views; `to_ast()` at most, to check completeness against CPython | Python `ast` objects as the face |
| D19 | `PathSet`, `pathview` and `PathStore` cores move out of `adapter/` | a walk that needs adapter code |
| D21 | `read_each`'s streaming iterator and its GIL hand-over go; `FileContent` stays as the view of a finished result's bytes | Python as the per-file consumer |
| D22 | the work stays on one branch of its own until it merges | a branch per part |
| D26 | no engine bridge: no polars IO plugin, no Arrow stream pulled from Python | any Python consumer during the run |
| D27, Q30 (#273) | pathlike is not where a parse starts: the parser and its corpus are the core component `code`, whose own call starts a parse; consumers (similarity, the inventory, pygim.testing) use its core in C++, pathlike walking and reading underneath | `pathlike.parse_python`; a parse reachable only inside similarity |
| Q24, Q28, Q29 | Ctrl-C and SIGTERM owned by a core `interrupt_scope` for the run: the handler flags and writes a byte to a self-pipe, the caller sleeps on it with no timeout, workers stop at an item's edge, the previous handlers come back, then the signal is raised again (`std::raise`) once the handlers are given back, and `PyErr_CheckSignals` runs Python's own; the partial result is dropped | polling; waiting on Python's handler |

Pushdown is the starting call's arguments (columns, path filters, node kinds),
so the core does only the work asked for. Proposed and not yet confirmed (D23):
the finished corpus laid out as Arrow arrays and exported through the C Data
Interface, its buffers owned by a C++ shared reference so a consumer may free
them from any thread without the GIL.

Open: opening by `openat` relative to a directory saves 10-15% on deep paths
(no full path walk) but needs a bounded cache of directory descriptors;
io_uring is Linux-only and refused by Docker's default seccomp; a `Stage`
that parses (the Python parser, D8) is the next consumer of the seam.

## Rules to keep

- **Append-only, single writer.** Rows are never removed or renumbered.
  Whoever inserts holds the GIL, and the GIL is what serialises the writers —
  the table has no lock of its own.
- **Work without the GIL owns its values.** Code that releases the GIL never
  reads the table: it copies what it needs first, with the GIL held.
  `read_each` and `read_all_files` are the example — `PathSet::paths()` copies
  each member's text into one `path_list` (one text, a NUL after each path), then
  a `batch_read` (`pathlike/batch_read.h`) reads them released, while another
  thread may be adding rows
  (`test_pathset_read_each.py` and `test_pathset_read_all_files.py` race
  exactly that).
- **Row ids are per table.** Two sets or paths may only be combined by row
  when their tables are the same object; otherwise rows are mapped by chain
  (`path_table::find` across tables, `row_map` for many).
- **No per-row heap object, ever.** Storage is structure-of-arrays throughout.

## Open

- **D49, decided 2026-10-30 (Debith: "yes"): `glob()` is pathlib's.** Python 3.14's rules, with
  `recurse_symlinks=False`: `[seq]` classes; `.` dropped and `..` a step; a trailing `/` keeps
  directories, a final `**/` real ones only; `**` yields the directory it starts in and never goes
  through a link to a directory (any other segment follows one); `?` and a class match a character
  (a UTF-8 code point), not a byte; `case_sensitive=` as pathlib's, any case on Windows, where a
  backslash also separates; an empty pattern or `.` alone is a ValueError, one that is not relative
  a NotImplementedError. test_glob_walk.py pins pathlib 3.14.4's results on its tree (33 patterns,
  4 in any case, 4 refusals) and, on 3.14 and later, asks the live pathlib too. Not pathlib's:
  `recurse_symlinks=True` — pathlib follows a link loop until the OS refuses at 40 links, so pygim
  needs its own answer to loops first — and any case folds ASCII letters only, where pathlib's
  `os.path.normcase` folds all of Unicode on Windows. The walk's `**/**` link hops are gone with it.
- **D50, open: one way to read, streaming included.** Today there are two: `read_all_files()`
  returns every member's text at once, and `read_each()` hands over a view per file as it is read.
  Debith, 2026-10-31: "`__iter__` could support streaming directly. I am too tired to think this
  now. My thinking is we need to support streaming, but not from two different functions." What a
  decision has to meet: D21 (2026-10-25) took `read_each`'s streaming out of the plan because
  Python was the consumer of every file (#270), so streaming wants a case where the consumer
  cannot wait for the whole set — a set larger than memory, or a first result needed early;
  iterating a `PathSet` yields its member paths today, so contents from `__iter__` change what
  iterating a set of paths means, or need an object of their own to iterate (what `read_each`
  returns now); and `read_all_files()` already reads with the GIL released (a `batch_read`,
  `pathlike/batch_read.h`). Not decided; nothing changes until Debith decides.
- **D64, a recorded gap: a device in a set is opened.** A read opens a member and then asks
  fstat what it opened — one open and one fstat a file, no stat by path first, so nothing can
  change between the check and the read. A device among the members is therefore opened though it
  is left out (`O_NONBLOCK`, and `O_NOCTTY` so no tty becomes the controlling terminal): a waiting
  FIFO writer wakes (shown 2026-10-30: pathlib's `is_file()` loop leaves it blocked), a tape
  rewinds, a serial port's open resets an Arduino, `/dev/watchdog` arms. Measured 2026-10-30,
  20,000 small files warm on one thread: the read as it is 88 ms (ext4) and 97 ms (tmpfs); `O_PATH`,
  fstat, then open again, 65% and 62% slower; stat by path, then open, 37% and 38% slower. Debith,
  2026-10-30: "likely something that can be detected upon creating the pathset. But, how deep can
  we go..? Might be just gap we record for now." How deep it can go: (1) a set made by a walk has
  each member's kind from the directory listing (`d_type`, `kind_from_listing`) at no cost, so a
  read could leave a device out unopened; (2) a set made from given paths knows no kind — a stat
  each at creation is the measured 37%, moved, not saved; (3) either is a snapshot: a path can
  become a FIFO between the set's making and the read, so the read keeps its non-blocking open and
  its fstat whatever (1) or (2) say. Until a case asks for it, the read opens and says so.
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
