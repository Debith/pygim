# Vectorisation: when pygim writes SIMD, and how

An optimisation that follows this strategy is made without asking (Debith, 2026-10-26: "I don't
see why you should not do optimization either, if we have strategy"). Each tier says when it
applies; every tier keeps the scalar code as the definition and checks the faster path against
it.

## A scenario: where do the lines of a 36 MB tree start?

`code.parse` must know where every line of CPython's `Lib` begins (972,865 lines): a SyntaxError
names its line, and `Node.lineno` is read from those starts. One search per line loads the 64
bytes around each line end again and again: 12.6-13.5 ms. `utils/byte_scan.h` reads each 64
bytes once and takes every line end from one mask:

```text
 64 bytes      x = 1 \n def f(a): \n     return a \r\n ...          (one step)
 SSE2          4 loads of 16 bytes, compare each with '\n' and '\r', movemask:
               16 bits each, folded into one 64-bit mask       ...0000100000000001000000100
 AVX2          2 loads of 32 bytes, 2 compares, 2 movemasks: the same 64-bit mask
 NEON          4 compares narrowed to 4 bits a byte (shrn): the same, as nibbles
 every stop    countr_zero(mask), then mask &= mask - 1, until it is 0
```

The line index then costs 3.9-4.4 ms. Which path runs is chosen once, at run time:

```text
first(s, from) ── has_avx2()? ── yes ── first_avx2: 64 bytes a step ─┬─ a stop: the answer
                       │                                             └─ < 64 bytes left ─┐
                       └─ no ─────────────────────────────────────────────────────────────┤
                                                      first_wide (SSE2 / NEON): 64, then 16 ─┤
                                                      scalar: the definition, the tail ──────┘
```

Every path is checked against the scalar definition at every offset 0-140 around its steps
(`tests/static/run_byte_scan_paths.py`), each called directly, since the dispatch runs only one
of them on a machine.

## The tiers

| Tier | When | How | Measured |
| --- | --- | --- | --- |
| 1. Scalar the compiler vectorises | always first: a loop over bytes or numbers with no early exit | write the plain loop; the production build compiles with `-O3 -ftree-vectorize`; confirm with `-fopt-info-vec-optimized` | `chars::lower` "loop vectorized using 16 byte vectors"; a hand-written SSE2 version is no faster (table below) |
| 2. Hand-written 128-bit SIMD | the compiler cannot vectorise it: a loop that stops at the first match (a scan), or one that needs a 64-byte mask | SSE2 on x86-64, NEON on arm64, no run-time dispatch (the wheels are baseline); four 16-byte compares folded into one 64-bit mask (#130); `utils/byte_scan.h` | the UTF-8 check and the line index over CPython's `Lib`: 33 ms to 10 ms; then every line end taken from one 64-byte mask instead of a search per line (`for_each_line_end`): the line index 12.6-13.5 ms to 5.0-5.6 ms |
| 3. Wider vectors (AVX2) | a vectorised path that takes a real share of a run on buffers that fit in cache | behind a run-time dispatch (`utils/cpu.h`'s `has_avx2()` choosing the function, `[[gnu::target("avx2")]]` on GCC and clang, plain intrinsics on MSVC), the scalar or 128-bit path the fallback and the definition | `utils/byte_scan.h`: two 32-byte compares a 64-byte step, the line index 5.0-5.6 → 3.9-4.4 ms and ASCII runs 1.7-2.2 → 1.1-1.7 ms on `Lib`, a one-thread `code.parse` 425-428 → 408-412 ms with the bulk scan. `utils/sorted_sets.h`: the intersection of two sorted sets, four 64-bit values a step against every rotation of the other four — similarity's merges 1.5-1.8x, its class stream 28% and ranked queries 7-13% faster (table below). Not taken: `chars::lower` (1.65x at 1 KB, but no caller lowers more than a file extension); similarity's stream column writes (memory bound, "Caches") |
| 4. No SIMD | the cost is elsewhere | profile one thread stage by stage first (#284) | the C++ tokenizer: 5.6x from bucketed keyword lookups, 1% from SIMD; the Python frontend: strings per node, not bytes |

## The rules every tier keeps

- The scalar loop is the definition: constant evaluation, the tail under one vector, and a build
  with the tier switched off (`-DPYGIM_BYTE_SCAN_SCALAR`) run it.
- A faster path is checked against the scalar one at every offset around the vector widths
  (16, 32, 64), each path called directly — the dispatch runs one path a machine, so a test
  through it checks only that one (`tests/static/run_byte_scan_paths.py`: SSE2, AVX2 and the
  dispatch, a build with the tier switched off, and one under ASan and UBSan) — and on real
  corpora (digests).
- Measured with the extension's own compiler and flags (conda g++, `-O3 -flto`), A against B
  interleaved, three runs each; a `-O2` harness with the system compiler can mislead (5% that the
  production build did not have).
- Sanitizer builds must compile: `tests/static/run_proofs_under_sanitizers.py`.

## Lowercasing, measured (2026-10-26)

`chars::lower` on random printable ASCII, nanoseconds per string, best of five, conda g++ 14.3:

| Build | 4 bytes | 16 | 64 | 1 KB | 1 MB |
| --- | --- | --- | --- | --- | --- |
| `chars::lower`, `-O3 -march=nocona` (auto-vectorised, 16 bytes) | 5.4 | 11.5 | 12.3 | 46.6 | 64,000-71,000 |
| hand-written SSE2, same flags | 6.1 | 11.6 | 11.9 | 44.4 | 60,000-70,000 |
| `chars::lower`, `-march=haswell` (auto-vectorised, 32 bytes) | | | | 28.1 | 50,000-57,000 |

Tier 1 holds: the compiler already does what a hand-written SSE2 path would. Tier 3 would pay
only for kilobyte-sized strings, which no caller has.

## Sorted-set intersection, measured (2026-10-27)

similarity's fingerprints were multisets of subtree hashes, so a block compare would count a
repeat more than once. A repeat now stands as its own value (the k-th repeat of hash h is
mix(h, k)), which makes Σ min(count) the size of a plain set intersection, the same values and
the same memory. Then `intersection_size` compares four values of each side at once. On the
candidate pairs of a ranked query, one thread:

| Pairs | Old merge (multisets) | Merge of sets | AVX2 |
| --- | --- | --- | --- |
| Lib/test at 0.5: 12.7M pairs, 30 values a pair | 900-916 ms | 726-741 ms | 493-515 ms |
| Lib/test at 0.8: 0.55M pairs, 16 values | 13.4-13.9 ms | 10.6-11.8 ms | 7.5-8.0 ms |
| LLVM at 0.5: 5.4M pairs, 15 values | 218-221 ms | 170-175 ms | 138-145 ms |

Whole calls, 16 threads, three runs each interleaved: Lib/test's 62M class pairs streamed 16.1-16.2
s → 11.3-12.0 s, the stdlib's 1.86-1.89 → 1.32-1.36 s; ranked at 0.5 Lib/test 336-345 → 307-317
ms, the C core 38.6-39.5 → 33.2-34.6 ms; top-200 of Lib/test 85-87 → 78-80 ms. Every unit,
ranked pair and streamed row identical. The scalar merge stays the definition: constant
evaluation, sets under four values, machines without AVX2 and `PYGIM_SORTED_SETS_SCALAR` run it,
`intersection_laws` (utils_proofs.cpp) proves it equal to the gallop, and
`test_shared_counts_agree_between_merging_and_counting` checks the AVX2 path against the token
index's counts.

## Caches

A loop that writes more than it computes is limited by where its bytes go, and no vector width
changes that. The rules, each measured:

- **Know the caches.** `utils/cpu.h` reads them once from the system (Linux sysconf, then
  /sys; macOS sysctl; Windows GetLogicalProcessorInformationEx), with typical sizes where a
  system says nothing; Python sees them as `pygim.utils.cpu()`. On the x86-64 desktops measured
  here the L1 and L2 are a core's own (its two hardware threads share them) and the L3 every
  core's; elsewhere an L2 may be a cluster's (Apple, Intel's E-cores) and an L3 one chiplet's
  (AMD with several CCDs). The sizes read are what one core sees.
- **A worker's unit of work fits three quarters of its L2** (`tile_pairs`), so what it wrote
  is still there when it hands it over.
- **Hand work over through the L3**: no more in flight between workers and a consumer than
  fits an eighth of it (`tiles_ahead`), so the consumer reads from cache, not memory.
- **Write once, at the place it belongs.** An intermediate copy (a vector of records later
  turned into columns) doubles the bytes moved; when places are known in advance, write the
  final buffers directly.
- **Wake one thread, not all, for one piece of work.** A consumer and its producers wait on
  separate condition variables; a finished unit wakes the consumer, a freed slot one producer.
  Work for many is the other way round: `parked_workers` wakes every parked thread for a job
  that wants most of them, and `ordered_pool` its held-back workers once the window has drained
  by half — one wake for many, since each notify is a system call on the waker's path.
- **Non-temporal stores only for output that leaves the caches** before it is read again;
  they bypass a cache the next reader would have hit.
- **Huge pages for large buffers one thread writes once** (`utils/huge_pages.h`,
  `prefer_huge_pages`): a ranked sweep's collectors — one fault per 2 MB instead of 512. Not
  for buffers an allocator recycles at once (a stream's batch columns): there the advice and
  the kernel's compaction cost more than the faults they save. Not for buffers many threads
  fill at once (a token index's arrays): the faults on one 2 MB page wait for each other, and
  4 KB pages faulted in by the threads that write them cost less (`utils/unfilled.h`, whose
  vectors' resize writes nothing).

What showed it: similarity's all-pairs stream, 127M function pairs of CPython's stdlib, 16
threads on a Ryzen 7 5800X (L1d 32 KB, L2 512 KB per core, L3 32 MB), in a C++ harness with a
reader thread consuming every row, 2026-10-27:

| Stream design | Wall |
| --- | --- |
| collector of 32-byte matches, then columns; 512K-pair blocks; one condition variable | 1,015 ms |
| columns written from the shared counts (no collector), 512K-pair blocks | 360 ms |
| the same, 256K / 128K / 64K / 16K-pair blocks | 330 / 338 / 422 / 888 ms |
| the same, two condition variables (notify one), 16K-pair blocks (384 KB), 32 ahead | 191 ms |
| the same, 18 / 8 ahead (7 / 3 MB in flight) | 142 / 114 ms |
| the same, AVX2 for the column writes, 8 ahead | 107-114 ms |
| the same, AVX2 with non-temporal stores, 8 ahead | 171-184 ms |

Huge pages, measured the same day (16 threads, three runs each interleaved): first touch of 50 MB
20-21 ms on 4 KB pages, 4.0-4.3 ms on 2 MB ones. Advised under the token index and the ranked
collectors: top-200 of Lib/test 86-95 → 67-69 ms, of LLVM 156-167 → 131-147 ms; ranked at 0.5 of
the stdlib 59-60 → 49-53 ms, of LLVM 341-378 → 320-351 ms. Advised under the stream's batch columns
too, the stdlib's stream went 227-231 → 250-272 ms (+12%), so those are left on 4 KB pages.
Written by 16 threads at once, 2026-10-28, the same 50 MB took 3.4 ms on 4 KB pages and 7-8 ms
on huge pages; LLVM's token index, built on every core (similarity/core.h), took 47 ms with its
arrays on 4 KB pages and 59 ms advised. The index's arrays are left on 4 KB pages since.

Small blocks lost until the hand-over was fixed: 7,800 hand-overs each waking sixteen workers
cost more than the cache saved. AVX2 changed nothing measurable: the loop moves 24 bytes a
pair and computes almost nothing. Through pyarrow the placed stream (stream.h) took the
stdlib's stream from 1,203 to 221 ms and Lib/test's billion pairs from 9.0 to 1.65 s, each row
identical (docs/design/similarity.md).
