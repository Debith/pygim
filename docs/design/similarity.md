# Similarity: deterministic all-pairs comparison of functions and classes

`pygim.similarity` measures how alike the functions, methods and classes of a
code base are. It compares every unit with every other unit of its kind, and it
does so mechanically: the same input gives the same numbers on every run, every
machine and every thread count. One rule shapes the design: **languages never
meet.** A Python function is compared with Python functions and a C++ function
with C++ functions. Nothing ever compares across.

## Using it

### From the command line

`pygim similar [PATHS]` walks the paths (the current directory by default),
builds one index per language it finds, and prints each language's pairs:

```text
$ pygim similar pkg -t 0.5 -m 5
skipped: pkg/bad.py:1: invalid syntax: expected a name

python: 3 function pair(s) scoring >= 0.5
score    a                           b
-------  --------------------------  --------------------
1.000 =  grand_total  pkg/cart.py:1  near  pkg/cart.py:7
1.000 =  grand_total  pkg/cart.py:1  total  pkg/shop.py:1
1.000 =  near  pkg/cart.py:7         total  pkg/shop.py:1
```

- `-t/--threshold` (default 0.9): the score a pair must reach. `=` after a
  score marks an exact pair: identical up to local names. `-t 0` needs `-n`:
  every pair of every unit would be ranked in memory before the first printed.
- `-k/--kind function|class`: functions (methods included) or classes.
- `-m/--min-size` (default 20): units of fewer syntax-tree nodes are left out;
  one-line accessors are identical without being duplicates.
- `-n/--limit`: at most this many pairs a language, the best ones.
- `-x/--exclude GLOB` (repeatable): matched against each file's path and each
  of its components, so `-x third_party` leaves out a directory anywhere.
- `--check`: exit 1 when any pair is shown, as a gate in CI.

A file a language's reader cannot read is reported on a `skipped:` line and
left out; it does not stop the run.

### From Python

```python
from pygim import similarity

ix = similarity.Index("python")                 # one language an index
left_out = ix.add_files(paths)                  # read, parsed, fingerprinted on every core
top = ix.pairs(threshold=0.8, min_size=30)      # a pyarrow.Table, best first
best = ix.pairs(limit=1000)                     # only the best 1,000: most comparisons skipped
for batch in ix.stream_pairs(batch_size=1 << 20):
    ...                                         # every pair, unranked, in bounded memory
```

- `pairs()` ranks: it needs every kept pair before the first one can be
  emitted, so it holds them in memory (24 bytes each, plus the table).
- `pairs(limit=N)` keeps only the best N, and the sweep skips what could not
  make the cut.
- `stream_pairs()` does not rank: batches of exactly `batch_size` rows come in
  a fixed order while the sweep runs, the way to a billion pairs.
- `units()` is a table too; a pair's `a` and `b` are unit ids that join it.
- `columns=` picks the pair columns; `["a", "b", "score"]` is 16 bytes a row
  instead of 56.

`docs/examples/similarity/example_01_find_duplicates.py` walks through each of
these with asserts.

## What is measured

A unit — a function, a method, or a class — becomes a tree, the tree a set of
subtree hashes, and two sets a score.

```text
def f(a): return a + 1          FunctionDef ─┬─ arguments ── arg  $0     (bind)
def g(b): return b + 1                       └─ Return ── BinOp ─┬─ Name $0   (use)
                                                                 ├─ Add
   one tree once locals are renamed:                             └─ Constant "int"
   f and g score 1.0 (exact)
                                 fingerprint: a hash per subtree of two or more
                                 nodes, and the root — FunctionDef, arguments,
                                 Return, BinOp (leaves never count)
```

1. **A frontend turns source into a language-neutral tree.** A node has a
   `kind`, an optional `label`, and a `role` that says what the label means:
   `bind` for a local, `use` for a name read, `attr` for a label kept
   verbatim, `constant` for a literal's type, and `syntax` for no label.
   - *Python* reads the tree pygim.code's parser builds in C++
     (`python_source.h`): Python 3.14's grammar whatever Python runs pygim,
     CPython's node kinds and fields in `_fields` order, no Python object
     anywhere. It drops docstrings, annotations, `ctx` and type comments, and
     reduces constants to their type.
   - *C++* is pygim's own tokenizer plus a reader of the bracket structure
     (`cpp_source.h`). It needs no compiler and no libclang.
2. **Names are resolved** (`names.h`). Every name that is not a local is
   replaced by what it stands for, statically, so the same thing done through
   different imports is one tree: `os.path.join(p)`, `path.join(p)`,
   `osp.join(p)` and `j(p)` are all `Ref "os"` → `.path` → `.join`.
   - Python scopes them as Python does, but for comprehensions, which are no
     scope here. A name assigned in a function (and not declared `global` or
     `nonlocal`) is local; others are looked up through the enclosing
     functions (class bodies skipped), then the module, which binds every
     import, every module-level definition (`<module>.<name>`) and every
     module-level alias (`list = list` is the builtin, `getcwd = os.getcwd` is
     `os.getcwd`). A function's defaults and decorators, and a class's bases,
     are resolved where the definition stands. `builtins.len` is `len`, and
     import statements inside units are dropped.
   - Relative imports start from the module's package. `add_file` names the
     module as the import system would (the file's stem under every parent
     directory with an `__init__.py`); `module=` names it outright.
   - C++: namespace aliases and using-declarations anywhere in the file bind
     names. A qualified name is one `qname` leaf with its alias expanded, so
     `std::move`, `::std::move` and `using std::move; move` meet unless the
     unit binds `move` itself.
   - What can't be decided statically stays as written: a name bound two
     ways (try/except imports), `from m import *`, C++ `using namespace`,
     runtime aliases (`os.path` is `posixpath`), and a relative import with
     no known package.
3. **Locals are renamed** by their order of first appearance; every other
   label is kept. Renaming a parameter changes nothing; calling a different
   global does.
4. **The fingerprint** is a Merkle hash per subtree — its node and its
   children's hashes, in order, its size folded in — for every subtree of two
   or more nodes, and the root. Single leaves are left out: unrelated code
   shares plenty of names and literal types. A subtree repeated in a unit
   holds a value of its own each time, so the fingerprint is a set. The hashes
   are pygim's FNV-1a and splitmix64 (`utils/hash.h`), never `std::hash` or
   Python's salted `hash()`.
5. **The score** is the Dice coefficient over the two sets,
   `2 * shared / (weight(A) + weight(B))`: `shared` the values both hold,
   `weight` how many each holds. It is an exact integer ratio; the float is
   only its correctly rounded quotient. Each subtree counts once, not by its
   size ("Why each subtree counts once", below).
6. **A class** is its header — bases, decorators, members that are not
   functions — plus its methods. Two classes' methods are paired by a
   maximum-weight assignment (the Hungarian algorithm on exact integer
   weights), and the class score is the same Dice ratio: header overlap plus
   matched overlap over the whole weight of both, so an unmatched method counts
   fully against it.

## How a query runs

### The sweep

Every query is a sweep over one kind's units, sorted by weight, lightest
first. Row r pairs unit r with every heavier unit after it:

```text
           partners, heavier →
rows        u1   u2   u3   u4   u5         ceiling(w_r, w_j) — the best score
 u0          ●    ●    ●    ●    ●         two weights allow — only falls along
 u1               ●    ●    ●    ●         a row: the first partner below the
 u2                    ●    ●    ×         bar ends it, and no later one could
 u3                         ●    ×         have been kept
 u4                              ×
```

The cut never changes the result. Rows are spread over every core, each
worker's into its own collector, and their union is ranked by one total order
(best score first, then the ids): any partition of the rows gives the same
list (`sweep_partition_law`, `tests/static/similarity_proofs.cpp`).

The plan is chosen once per query (`sweeps.h`: `best_pairs`, `with_stream`),
and from there on by type: a sweep or a stream is instantiated with the way
its kind's rows are counted, so no row asks which kind it is.

- **Functions at a fixed threshold t ≥ 0.5: the prefix filter**
  (`token_index`, `filtered_sweep`). Every unit's tokens — its fingerprint's
  values — are ranked rarest first. If Dice(x, y) ≥ t, the two share a token
  among the first |x| − ⌈t·|x|/(2 − t)⌉ + 1 of x's (Bayardo, Ma and Srikant,
  WWW 2007), so only units whose prefixes share a rare subtree are compared:

  ```text
  x  [ r1  r7  r9 | c3  c4  c8 ]     r: rare tokens, c: common ones
  y  [ r7  c2 | c3  c5 ]             | ends each prefix
        └─ shared in both prefixes: offered;  pairs sharing only common tokens never are
  ```

  The bounds are computed with t a little lowered (`rounding_margin`), so a
  pair whose ratio is exactly t — which `value >= t` keeps — is never cut.
- **Functions below 0.5: counted rows** (`function_counts`). An index of every
  token gives a row's shared count with all its partners at once from posting
  lists, where merging every pair cost every pair's length.
- **A limit: the floor, and the ladder.** Each worker's collector raises its
  floor to its own N-th best score as it fills; the ceiling cut ends rows
  against it, so most comparisons of a top-N query are never made. From 4,096
  functions a limit first tries the prefix filter at falling thresholds
  (1.0, 0.95, 0.9, …) until one finds N pairs — every pair at a threshold ranks
  above every pair below it. Rows are claimed lightest first: exact copies
  raise the floor at once.
- **Classes: counted, then assigned** (`class_counts`). A `class_index` holds
  every token of the classes' own subtrees and of their methods, class by
  class; a row takes its header counts and its methods' counts with its
  partners' from posting lists, in tiles of partners sized to the L2, and
  solves only each pair's method assignment. A class pair costs its method
  matrix, not its place, and the heaviest classes come last, so without a
  limit class rows are claimed heaviest first.

The token index is built on every core: five steps of tasks over disjoint data
— the rows measured; the tokens written straight into buckets of their hashes'
top bits and each bucket sorted; equal tokens' groups ranked by a stable
counting sort; each row's prefix cut; each rank's rows gathered — and any cut
into tasks builds the same index (`token_index_build_law`).

### Ranked results

The sorted parts are never merged into one copy. A batch's boundaries are
global ranks, found by bisecting over the parts (`select`); every batch is
merged from its own slices with a heap, 4,096 pairs at a time, and each chunk
written to the batch's Arrow columns while it is still in the L2. All batches
are merged at once, and they are the same for any number of parts.

### Streams

A stream emits the sweep in its order, cut into batches of exactly
`batch_size` rows, the same for any thread count. There are two pipelines:

- **Threshold 0: placed.** Every pair is kept, so each pair's place in the
  result is known before it is scored (`sweep_positions`). The result is cut
  into tiles inside batches; workers claim tiles in order and write each
  pair's columns straight into its batch at its place:

  ```text
  places  0 ──────────────────────────────────────────────────── n(n-1)/2
  batches │◄──────────── batch 0 ────────────►│◄──────── batch 1 ────────
  tiles   │ t0  │ t1  │ t2   │ t3  │ t4      │ t5  │ t6  │ …
           a tile: 3/4 of a worker's L2 of rows at most, and a class tile at
           most 1/64 of a worker's share of the work; it starts on a multiple
           of 64 places within its batch, so no two tiles write one byte of
           the bit column or one cache line of the others
  ```

  A batch is handed over when its last tile is written: no collector, no copy,
  no join. At most a few tiles run past the batch being read (an eighth of the
  L3), so the reader takes them from cache.
- **A threshold: collected.** Rows go in blocks to the workers' collectors,
  and the reader joins blocks into batches. Function rows from 0.5 come from
  the prefix filter, each row's partners sorted back into sweep order
  (`filtered_order_law`); their blocks are cut by the work a filtered row
  walks — its prefix's posting lists. Below 0.5, blocks hold 512K candidate
  pairs; a class block at most 1/64 of a worker's share of the work.

Memory is bounded by `batch_size` and the cores — a few batches being
written, or two blocks a worker — not by the result.

## Threads, state and locks

An `Index` and every stream it opened share an `index_state`
(`adapter/index_state.h`): one corpus, the lock that guards it, a generation,
and what each corpus state gives every query (the units' names, files and
lines as the results' dictionaries; each kind's sweep order). Every way to the
corpus is a command that takes the lock it needs: `write()` exclusive, moving
the generation on; `read()` and `read_at()` shared. Every method lets the GIL
go first.

- An `add()` waits for the queries reading the corpus, and they for it.
- A stream keeps the generation it opened at, and sweeps each block or tile
  through `read_at`: an `add()` after it opened fails the stream
  (`ArrowInvalid`) instead of mixing two corpora.
- A stream holds the index's state, so it outlives the `Index`; its workers are
  joined when it goes.
- The workers parallel steps borrow (`utils/parallel.h`) belong to the process
  that started them: a forked child runs its own, and a stream opened before a
  fork fails in the child.

`tests/static/run_tests_under_sanitizers.py` runs the Python tests against
builds instrumented with AddressSanitizer and UndefinedBehaviorSanitizer, or
ThreadSanitizer.

## Results are Arrow

`units()` and `pairs()` return `pyarrow.Table`s, `stream_pairs()` a
`RecordBatchReader`. `adapter/tables.h` builds them in C++ with the GIL
released and hands them over through the Arrow PyCapsule protocol, without a
copy (`utils/arrow_stream.h`, shared with `datagen`). No row is a Python
object until someone asks for it.

- `a` and `b` are unit ids (insertion indices), so `pairs` joins `units.id`.
  A method's `owner` column holds its class's id.
- Names and files are dictionary-encoded: one dictionary of distinct
  qualnames and one of distinct origins, each shared by its `a_` and `b_`
  columns and by every result of one corpus state. Polars reads them as
  `Categorical`.
- Every column except `owner` is non-null. An empty result keeps its schema.

The extension is `pygim._similarity` and links Arrow. `pygim/similarity.py`
imports pyarrow before loading it, as `pygim/persistence.py` does, so the
loader resolves libarrow from the pyarrow package.

## Why each subtree counts once

One edited token changes the hash of every subtree that encloses it. Those
are the largest subtrees, and the root is the largest of all. The first
version weighted each subtree by its size, so one edit cost about a third of
a function's weight. A near-duplicate scored like a stranger, and on CPython
the score histogram had almost nothing between 0.80 and 1.00.

The measurement used 600 real stdlib functions (30-400 nodes). Each got one
structural edit, and the edited copy was scored against the original, next
to 1,000 unrelated pairs. A Python prototype of the fingerprint compared the
weightings:

| weight per shared subtree | unrelated p99 | one-edit copies, median | separation (worst edit p10 - unrelated p99) |
| ------------------------- | ------------- | ----------------------- | ------------------------------------------- |
| size (first version)      | 0.10          | 0.60-0.64               | +0.15 |
| 1                         | 0.35          | 0.88-0.92               | +0.23 |
| sqrt(size) / log2(1+size) | 0.21-0.22     | 0.80-0.86               | +0.23 / +0.25 |
| **1, leaves left out**    | **0.10**      | **0.83-0.88**           | **+0.33** |

The shipped implementation confirms it on the same functions
(`benchmarks/similarity_mutants.py`, on CPython's `Lib`, 2026-10-29): one-edit
copies score a median of 0.87 (flipped operator), 0.84 (renamed attribute),
0.84 (dropped statement) and 0.86 (duplicated statement); unrelated pairs a
median of 0.04 and a 99th percentile of 0.22, and the pairs at that tail are
genuinely related codec boilerplate (`getregentry`).

## Measured

On 16 logical cores (Ryzen 7 5800X, WSL2), 2026-10-29; each run is a record
in `benchmarks/results/`.

CPython 3.12.6 (`a4a2d2b0d85`), `benchmarks/similarity_cpython.py`:

| corpus | files (left out) | functions | classes | read, parsed, fingerprinted | every pair streamed | ranked ≥ 0.8, ≥ 30 nodes |
| --- | --- | --- | --- | --- | --- | --- |
| stdlib | 690 | 15,967 | 2,625 | 0.04 s, 7.5 MiB | 127,464,561 in 1.34 s, 88 MiB | 2,823 (2,237 exact) in 0.020 s |
| Lib/test | 1,098 (12) | 44,885 | 11,115 | 0.09 s, 24 MiB | | 33,344 (30,507 exact) in 0.043 s |
| the C core | 812 (1) | 16,223 | 302 | 0.06 s, 6.5 MiB | 131,584,753 in 1.30 s, 31 MiB | 11,393 (553 exact) in 0.020 s |

A stream's time includes the benchmark's own score histogram per batch in
pyarrow; drained alone, the stdlib's 127M pairs take 0.2 s with four columns.
Of the C core's near pairs, 4,883 are Argument Clinic output (`*/clinic/*`,
boilerplate by design).

LLVM's `llvm/{lib,include,tools,utils}` (7,090 C++ files),
`benchmarks/similarity_tree.py`: 140,856 functions in 0.22 s, 51 MiB; ranked
at 0.8 and ≥ 30 nodes, 1,533 exact and 634 near pairs in 0.1 s; the top 1,000,
all exact copies, in 0.05 s.

`benchmarks/similarity_sweep.py`, synthetic and seeded: every pair of 2,000
functions ranked in 0.09 s (Python) and 0.07 s (C++). Of 12,000 one-line
functions' 71,994,000 pairs, each mode in a fresh process, its peak RSS above
the built index:

| mode | seconds | peak RSS |
| --- | --- | --- |
| `pairs()`, every column | 1.38 | 5.4 GiB |
| `pairs(columns=["a", "b", "score"])` | 1.01 | 2.7 GiB |
| `pairs(limit=1000)` | 0.24 | 6 MiB |
| `stream_pairs()`, every column | 0.26 | 279 MiB |
| `stream_pairs(columns=["a", "b", "score"])` | 0.09 | 91 MiB |

Streams at a threshold, best of seven: Lib/test's functions at 0.5 in 148 ms
and at 0.8 in 33 ms; its 62M class pairs at threshold 0 in 1.11 s.

## Decisions taken

- **Structural trees, not tokens or bytecode.** Bytecode changes with every
  interpreter. A flat token stream loses nesting: `if a: x` and `if a: pass;
  else: x` share every token. The Python frontend wraps each list field as its
  own node (`If.orelse`) for that reason.
- **Dice over subtree sets, not tree edit distance.** It is linear after the
  sort and fast enough for every pair of a large corpus. A large shared block
  counts once for every subtree inside it, which is what "duplicated code"
  means. Tree edit distance (APTED) is cubic and stays an option for
  re-ranking the top pairs.
- **A heuristic C++ reader, not libclang.** libclang would be a build and
  runtime dependency on every platform in the matrix, and it needs the
  project's include paths and macros to parse at all. The reader is
  deterministic and fails loudly: unbalanced brackets are a `parse_error`
  naming the file and line, and `pygim similar` reports and skips such a file.
- **Types count, names do not.** In C++, a parameter's type is a used
  identifier or keyword and its name is a bound one. `int twice(int v)` and
  `long twice(long v)` therefore differ, while `twice(int v)` and
  `doubled(int w)` are identical.
- **A unit's own name never counts.** A renamed copy is still a copy.
- **The plan is the core's.** Which way a query runs — filter, ladder, counted
  rows, placed or collected stream — is decided in `sweeps.h`, beside the
  sweeps it picks between; the adapter translates.

## Rules to keep

- Every hash and every sort is total and platform-independent. A new
  frontend emits trees of atoms, made by the core's `atom`, and hashes
  nothing itself.
- The Python grammar is pygim.code's (3.14), whatever interpreter runs
  pygim, so Python scores are reproducible across interpreters; they change
  only when the parser's grammar does.
- Adding a language means adding a header in `adapter/languages/` that
  declares a `language_info` and `parse(std::string_view text, const
  source_context& where) -> parsed_source<L>` (Python's also takes a read
  file's bytes over, `parse(std::string&&, ...)`). Its extensions must not
  overlap another language's; the build refuses that.
- A change to how pairs are found keeps every result: the proofs, and digests
  of every ranked and streamed pair on real corpora, before and after.

## Open

- Python type aliases and re-exports (`from pkg import x` where `pkg/__init__`
  re-exports `pkg.impl.x`) resolve to the spelling used, not the defining
  module; following them needs every module in the index, a second pass.
- C++ methods defined outside the class body (`int Box::far() {}`) are
  compared as functions but not attached to their class.
- Commutative operands (`a + b` vs `b + a`) are not canonicalised.
- Of every `#if` chain one branch is read (the first, or the next after an
  `#if 0`); code whose braces balance only across branches is skipped.
- The first query of an index builds what the others reuse (the units'
  columns, the sweep orders; 25-30 ms on LLVM).

## Appendix: how it got here

Each step below kept every result: the proofs, and digests of every ranked and
streamed pair on the corpora named, before and after.

### 2026-10-23: the first field test, CPython on 4 cores

CPython `main` (`b1e32ff`), the frontend then walking the interpreter's `ast`:

- **Reading the sources.** The stdlib is 14,583 functions, `Lib/test` 56,457
  and the C core 22,319. The C reader rejected 1 of 1,087 files,
  `Include/internal/pycore_fileutils_windows.h`, whose `extern "C" {` is
  never closed. The Python frontend rejected 95 of 2,081 files, syntax newer
  than the Python 3.13 that ran it (`lazy import`, t-strings, `except A, B:`)
  or deliberately invalid test data — the reason the Python frontend now reads
  through pygim.code's own 3.14 parser.
- **Every pair, streamed.** 106,324,653 stdlib pairs took 15.6 s and
  249,057,721 C pairs took 28.8 s, at about 330 MiB each.
- **Ranked pairs, score ≥ 0.8 and ≥ 30 nodes.** `Lib/test` (~1.6B candidate
  pairs) took 97 s at 82 MiB; the stdlib 4.8 s, C 9.2 s.
  - Exact duplicates: stdlib 1,384, C 352, tests 10,837. For example,
    `_pydatetime._is_leap` = `calendar.isleap`, `make_version_info` is copied
    across four C modules, and `fileno` is shared by bz2/lzma/zstd.
  - Near-duplicates (0.8 ≤ score < 1): stdlib 460, tests 4,097, C 10,667, of
    which 8,185 are Argument Clinic output (`*/clinic/*`, boilerplate by
    design). Examples: `_PyObject_GC_Resize` in `gc.c` and
    `gc_free_threading.c` (0.85), asyncio's two transports' `_fatal_error`
    (0.91), and the blake2/hmac CPU-feature init (0.95).

`benchmarks/similarity_sweep.py` on the same 4 cores: 2,000 seeded synthetic
functions a language parsed in 0.25 s (Python) and 0.04 s (C++), and all
1,999,000 pairs ranked in 0.5 s and 0.4 s. Of 12,000 one-line functions'
71,994,000 pairs: the ranked table with every column 12.3 s at 6.5 GiB, the
top 1,000 3.2 s at 32 MiB, the stream of every column 2.6 s at 457 MiB.

### 2026-10-25: pygim.code's parser, fewer pairs compared, each one faster

The Python frontend reads pygim.code's tree in C++ instead of walking `ast`
through the C API. It emits the same trees: on CPython 3.12.6 every unit and
all 46,707 ranked pairs (score ≥ 0.8, ≥ 30 nodes) came out the same; only type
parameters gained the `default_value` field 3.13 added, one "None" leaf each
(176 units, no pair changed). `add_files` reads, parses and fingerprints on
worker threads with the GIL released, and appends in the order given.

- **A prefix filter for a fixed threshold.** `Lib/test` at 0.8 and ≥ 30
  nodes: 9.7 s to 0.24 s.
- **Shared counts by index for every pair.** A stream and an unfiltered sweep
  read each row's shared counts from posting lists of every token: the work is
  the sum of all pairs' shared counts (286M for the stdlib's 127M pairs)
  instead of a merge per pair (about 90 steps each).
- **Eight-byte subtrees and a branch-free merge.** A subtree is its Merkle
  hash, which folds in its size, so a merge compares one integer a step,
  galloping when one side is 16 times the other: 2.7 s to 1.7 s for the
  stdlib's 127M merges.
- The stream's workers build their block's Arrow columns themselves; with
  `columns=["score"]` the stdlib's 127M pairs stream in 0.6 s, where they took
  3.2 s.

CPython 3.12.6 (`a4a2d2b0d85`) on 16 logical cores (Ryzen 7 5800X, WSL2), the
build before against this one, three runs each interleaved, median (range),
seconds:

| section | before | after |
| --- | --- | --- |
| parse stdlib (690 files) | 2.602 (2.586-2.624) | 0.056 (0.052-0.057) |
| parse tests (1,098 files) | 7.510 (7.467-7.560) | 0.128 (0.127-0.131) |
| parse C core (812 files) | 0.742 (0.716-0.752) | 0.183 (0.180-0.190) |
| every stdlib pair streamed (127,464,561), with a Python histogram | 3.657 (3.585-3.798) | 1.796 (1.760-1.815) |
| every C pair streamed (131,600,976), with a Python histogram | 3.109 (3.108-3.116) | 1.766 (1.760-1.773) |
| ranked ≥ 0.8, ≥ 30 nodes: stdlib | 1.181 (1.171-1.187) | 0.020 (0.020-0.030) |
| ranked: tests | 10.336 (10.269-10.603) | 0.111 (0.111-0.124) |
| ranked: C core | 0.901 (0.892-0.903) | 0.020 (0.020-0.020) |
| **whole field test** | **30.22 (29.88-30.27)** | **4.08 (4.05-4.13)** |

LLVM (`llvm-project` at `c56fffc0e`: `llvm/{lib,include,tools,utils}` and
`clang/{lib,include,tools}`, one index per language — 9,781 C/C++ files,
140,686 and 88,455 functions — and 362 Python files), same machine:

| section | before | after | result (the same in both) |
| --- | --- | --- | --- |
| parse llvm C++ (7,090 files) | 4.38 (4.36-4.39) | 0.65 (0.63-0.65) | 140,686 functions, none left out |
| ranked ≥ 0.8, ≥ 30 nodes: llvm C++ | 34.34 (33.99-34.35) | 0.17 (0.17-0.18) | 1,547 exact, 629 near |
| top 1,000: llvm C++ | 3.19 (3.04-3.35) | 0.19 (0.19-0.19) | all 1,000 exact copies |
| parse clang C++ (2,691 files) | 2.05 (2.05-2.05) | 0.28 (0.27-0.29) | 88,455 functions, 1 left out (an unclosed `{`) |
| ranked: clang C++ | 7.61 (7.53-7.71) | 0.07 (0.07-0.07) | 983 exact, 908 near |
| top 1,000: clang C++ | 1.48 (1.46-1.61) | 0.08 (0.07-0.08) | all 1,000 exact copies |
| **whole run** | **53.52 (53.49-53.54)** | **1.49 (1.48-1.51)** | |

### 2026-10-26: a tree of atoms

After a profile of both frontends (gprof; one thread; CPython 3.14.3's `Lib`,
36 MB, and LLVM's 7,090 C++ files, 141 MB):

- **A tree node is three atoms' worth, not two strings.** A kind or a label is
  an `atom`, the 64-bit hash of its text, made once where the text is met
  often. A node is 24 bytes where it was 72, building a tree moves no strings,
  and a fingerprint folds integers.
- **Keywords and punctuators by bucket.** A word is compared only with the
  keywords of its first byte and length (at most five), a punctuator only with
  those of its first character (at most four).
- **The source checks 64 bytes a step** (pygim.code's `utils/byte_scan.h`).

| one thread | before | after |
| --- | --- | --- |
| C++ tokenize, LLVM | 1,730 ms | 315 ms |
| `parse_cpp`, LLVM (tokens, units, trees) | 2,573 ms | 895 ms |
| Python frontend, `Lib` (parse, scopes, trees) | 1,013 ms | 750 ms |
| fingerprints, `Lib` | 266 ms | 136 ms |
| source checks, `Lib` | 33 ms | 10 ms |

The LLVM field test went 1.43 → 0.87 s, CPython's 4.16 → 3.88 s.

### 2026-10-27: placed streams

A stream at threshold 0 writes each pair at its place (above). On 16 threads,
two runs each interleaved, `benchmarks/similarity_sweep.py`'s 72M pairs:
every column 1.53-1.57 s at 1.7 GiB → 0.26 s at 280-310 MiB; `(a, b, score)`
0.54-0.55 s → 0.06-0.07 s. Drained through pyarrow, the stdlib's 127M pairs
1,203-1,227 → 221-239 ms (a, b, shared, total), Lib/test's billion 8.99-9.18 →
1.65-1.66 s. The same day the threshold ladder stopped keeping every pair it
found: `pairs(limit=1000)` 1.68 s at 3.2 GiB → 0.28-0.34 s at 7 MiB.

A ranked table costs its rows, and first-touch page faults on gigabytes of
fresh memory dominated its time; through pyarrow's default allocator
(mimalloc) a first ranked call took 13.4 s where `ARROW_DEFAULT_MEMORY_POOL=system`
took 5.6 s. Arrow's own switch is the knob; the extension uses the pool Arrow
is configured with.

### 2026-10-28: every core for the index, cost for the classes, end to end

- **The token index on every core** (above): LLVM's 140,686 functions, 79-114
  → 44-57 ms at t = 0.8, 124-144 → 51-56 ms at t = 0. Its arrays are
  `unfilled_vector`s, whose resize writes nothing, so their pages fault in on
  the threads that fill them — on 4 KB pages: huge pages faulted by 16 threads
  at once were slower (59 against 47 ms).
- **The assignment** takes a class row's tile of method counts through an
  accessor, in 32-bit costs while the weights are under 2^28 (the potentials
  stay within the largest weight, the reduced costs within twice it);
  `assignment_enumeration_law` checks it against every pairing of 400 seeded
  matrices. Lib/test's classes at 0.5, 178-180 → 143-159 ms.
- **Class work cut by cost.** Lib/test's 11,115 class rows, timed one by one:
  the last tenth of the sweep holds 1% of the pairs and 45% of the time.
  Simulated on those times, 16 workers took 814 ms with blocks of equal pairs,
  where an even share is 418 ms, and 434 ms with blocks of equal cost.
  `stream_pairs(kind="class")` of Lib/test's 62M pairs, 1,827 → 1,154-1,161 ms.
- Measured and not taken for the assignment: splitting matrices into
  components (742,231 of the stdlib's 744,955 stay whole), a DP over subsets
  and closed forms for small sides (2-5%), AVX2 for the search's scans,
  memoising identical method sets (98.7-100% of the work is distinct).

A sampler on `perf_event_open` then profiled whole Python calls across their
threads, which the stage profiles had not:

| found (16 threads) | where | done |
| --- | --- | --- |
| every ranked query hashed all units' names into its dictionaries on one thread | 25-30 ms of LLVM's 83-98 ms | kept per corpus state (`index_state::unit_columns`) |
| every query sorted its units by weight on one thread | 10 ms on LLVM | each kind's order kept, filtered by size per query |
| every parallel step started and joined its threads | 500 µs a step; an index build has eight | parked workers: 90 µs |
| `add_files` grew the corpus a unit at a time | 17-25 ms on LLVM | reserved once: 5 ms |
| `pygim similar` spent 580 of its 774 ms in Python | path handling, pyarrow scalars, a row at a time | pygim's paths, Arrow's kernels, a column at a time: 0.49-0.53 s |

### 2026-10-29: kept memory, counted classes, the plan in the core

- **Freed blocks kept.** A short query faulted its arrays in again each time:
  the C library hands freed blocks of that size back to the kernel. A block of
  a megabyte or more is now kept for the next allocation of its size class
  (`utils/kept_blocks.h`, at most 256 MiB): Lib/test's ranked query 31-32 →
  23-26 ms, LLVM's 46-48 → 37-39.
- **Classes with a limit counted too.** A limit's floor was thought to rise at
  once and cut most merges; on most corpora it does not: the stdlib's top 2,000
  classes 432 → 23 ms, its top 200 at 0.8 107 → 8.5 ms; Lib/test's top 200,
  where exact copies raise the floor at once, 19 → 28 ms.
- **A scored pair is 24 bytes**, not 32, and ranked batches are written from
  4,096-pair chunks instead of a copy of each batch: ranking Lib/test's 62M
  class pairs 3.95-4.21 → 2.96-3.06 s, its peak 5.9 → 5.1 GB.
- **Function streams at a threshold filtered.** From 0.5 a function stream's
  rows come from the prefix filter in sweep order, its blocks cut by the work
  the filter walks: the stdlib's functions at 0.5 88 → 31 ms, at 0.8 39 → 11;
  Lib/test's at 0.5 777 → 148 ms, at 0.8 329 → 33.
- **The plan in the core.** `sweeps.h` holds the sweeps and the decisions
  between them; each kind's rows are counted by a type, never by a flag a row
  checks.
- **Races found by the sanitizers.** `len()`, `stats()` and `repr()` walked
  the units while `add_files()` grew them (a segfault), and a placed class
  stream counted its tiles' rows after letting go of the lock; every way to the
  corpus is now one of `index_state`'s commands. The Python frontend resolved a
  function's defaults and decorators in the function's own scope: 31 of
  CPython's 74,592 stdlib and Lib/test units changed when fixed, no ranked pair
  at 0.8.
