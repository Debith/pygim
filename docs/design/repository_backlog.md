# Repository — Design & Implementation Backlog

## Goal

Build a Python repository abstraction (`repository.save(data)`) that pushes bulk database write throughput to the hardware limit — saturating memory bandwidth, exploiting SIMD, and overlapping CPU with network I/O — while exposing a trivially simple, single-call API to the user.

The user does `pip install pygim`, calls `save(data)`, and gets the fastest path the machine is capable of. All complexity — cache-tiled transposition, AVX2 dispatch, multi-threaded pipeline, connection pooling — is internal and invisible. One wheel ships both scalar and SIMD paths; the right one activates at runtime.

This is an academic exercise in finding the performance ceiling of the Python → C++ → ODBC/BCP → SQL Server path, combined with a pragmatic goal of delivering that performance through a clean library interface.

---

## Phase 0 — Architecture Consolidation

Settle the abstract design before writing new code.

| #   | Task                                            | Status     | Notes                                                                                                                                                     |
| --- | ----------------------------------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0.1 | StorageStrategy acquired from connection string | done       | `Repository("memory://")` or `Repository("mssql://server/db")`. URI parsed in `connection_uri.h`. One strategy per repo. Raw ODBC strings also accepted.  |
| 0.2 | ExtractionPolicy returns abstract View          | done       | `DataView = std::variant<ArrowView, TypedBatchView>`. ExtractionPolicy is the single py::object inspection point: Arrow C stream (zero-copy via `__arrow_c_stream__`, or `_export_to_c`/IPC bytes for non-Polars objects), Polars DataFrame, and Python iterables all dispatch to the correct view. Arrow C++ >= 15 required (enforced via `static_assert`); Polars 1.x StringView (`"vu"`) accepted natively. Separate `bcp_batch_size` added to `persist_dataframe` to decouple BCP commit frequency from MERGE statement batching. |
| 0.3 | Transpose is internal to StorageStrategy        | done       | `bcp_transpose_strategy.h` owns the column→row transform behind a `TransposeStrategy` abstract interface. Two implementations ship: `RowMajorTranspose` (default, per-row outer loop) and `ColumnMajorTranspose` (128-row mini-batch, column-major fill for sequential Arrow reads). `BcpContext::transpose` is the runtime hook — set to `nullptr` for default, or point at any `TransposeStrategy*` for override. No public API exposes the choice; Phase 2 will wire the SIMD dispatch here. |
| 0.4 | Unified `save(data, hint?)` API                 | done       | Transpose strategy is now selected at acquire time via `acquire_repository(conn, transpose="column_major")` / `acquire_repository(conn, transpose="row_major")`. `MssqlStrategy` is templated on `Transpose` (`RowMajorTranspose` or `ColumnMajorTranspose`); the `make_mssql_strategy()` factory maps the hint string to the concrete instantiation. `m_transpose` is a value member — fixed for the connection's lifetime, devirtualizable in the hot loop. Arrow vs bulk-upsert detection remains automatic via `ExtractionPolicy`. The three-name surface (`persist_dataframe` / `bulk_insert` / `bulk_upsert`) is kept for now; surface rename to `save()` deferred to a polish pass. |
| 0.5 | Update abstract diagrams                        | done       | Verified: `repository_architecture_abstract.puml` and `repository_sequence_abstract.puml` contain no IPC / `compat_level` references. Diagrams already reflect the Arrow C stream direct path. No edits required. |
| 0.6 | Public API: `acquire_repository` + `StatusPrinter` | done    | `acquire_repository(conn_str, printer=None)` is a pybind11 free function in `_repository.cpp`. Prints `connecting  <masked_uri>` to stdout before connecting (separate from logging). `StatusPrinter(connection=True/False)` controls per-category output; additional flags can be added as new categories emerge. `repository.py` removed — all symbols exported directly from the C++ extension. |

---

## Phase 1 — Tiled Transpose (single-threaded, scalar)

Goal: eliminate the biggest CPU bottleneck with zero additional dependencies.

| #   | Task                                            | Status     | Notes                                                                                            |
| --- | ----------------------------------------------- | ---------- | ------------------------------------------------------------------------------------------------ |
| 1.1 | Benchmark current naive col→row transpose       | done       | **Baseline: 22.71 MB/s** (1,000,000 rows × 11 cols, 4.61 s, 10 BCP commits). Strategy: `RowMajorTranspose` — per-row outer loop, per-column `memcpy` into contiguous staging buffer, `bcp_sendrow` per row. Bottleneck is `bcp_sendrow` call overhead (1M ODBC calls) plus read-jumping across 11 Arrow column buffers each row. `perf stat` cache-miss rate not yet collected. |
| 1.2 | Implement cache-tiled transpose                 | done       | `ColumnMajorTranspose` (128-row mini-batch) implemented in `bcp_transpose_strategy.h`. Column-major fill reads each Arrow column buffer sequentially for 128 rows (L1-friendly), writing into a `128 × row_width` staging block. **Double-copy eliminated**: the second `memcpy` back into the single-row staging buffer is replaced with per-row `bcp_colptr` redirects directly into the mini-batch buffer — one pointer update per column per row, zero data moved. Wired via `acquire_repository(conn, transpose="column_major")` through `MssqlStrategy<ColumnMajorTranspose>`. Not yet benchmarked vs `RowMajorTranspose` (see 1.5). |
| 1.3 | Separate fixed-width and variable-width columns | done       | `classify_columns()` in `bcp_bind.h` splits bindings into `fixed` (INT32, INT64, UINT8, DOUBLE, DATE32, TIMESTAMP) and `string` (STRING, LARGE_STRING, STRING_VIEW) at bind time. Fixed: `memcpy` path via contiguous staging. Strings: per-row `handle_string_column()` with zero-copy offset extraction and reusable null-terminated buffer. DATE32 and TIMESTAMP are pre-converted at bind time (`days_to_sql_date`, `micros_to_sql_timestamp`) into typed SQL structs; their staging slots hold those structs. |
| 1.4 | Validate correctness                            | done       | Round-trip correctness test in `tests/adapters/test_transpose_correctness.py`. Skipped automatically when `STRESS_CONN` is unset. When live: creates `dbo._pygim_transpose_test`, inserts 2,000 rows with types INT32, INT64, DOUBLE, DATE32, TIMESTAMP, NVARCHAR via `row_major`, SELECTs back; then truncates, inserts same data via `column_major`, SELECTs back; asserts byte-identical results. Null-path and STRING_VIEW coverage deferred to a follow-up pass. |
| 1.5 | Benchmark tiled vs naive                        | done       | `benchmarks/bcp_throughput.py` — parameterised by `--strategy row_major\|column_major`, `--rows`, `--batch-size`, `--compare` (both side-by-side). Reports MB/s, rows/s, and `BcpMetrics` per-stage times. Run with `--compare` to get the speedup ratio between strategies. Requires `STRESS_CONN` + `pip install pyodbc polars`. |

---

## Phase 2 — SIMD Within Tiles (AVX2 / scalar fallback)

Goal: exploit data-level parallelism inside each cache tile.

**Dispatch mechanism**: `if constexpr` trampoline. The pipeline is a template parameterized on `Simd` level. One runtime `if` at the entry point (`cpu_supports_avx2()`) selects the template instantiation. Inside each instantiation, `if constexpr` compiles away the unused branch — zero per-call overhead. The compiler sees fully inlined, direct calls throughout the hot path. A `static_assert` in the `else` branch ensures adding a new `Simd` enum value (e.g., `Neon`, `Avx512`) produces a compile error at every unhandled site.

```cpp
enum class Simd { Scalar, Avx2 /*, Neon, Avx512 — future */ };

template <Simd S>
void run_pipeline(const ArrowArray& data, OdbcConnectionPool& pool) {
    for (auto& batch : partition_rows(data)) {
        if constexpr (S == Simd::Avx2)
            transpose_avx2(batch);
        else if constexpr (S == Simd::Scalar)
            transpose_scalar(batch);
        else
            static_assert(false, "Unhandled SIMD level");
        send_to_db(pool, batch);
    }
}

void save(const ArrowArray& data, OdbcConnectionPool& pool) {
    if (cpu_supports_avx2())        // ONE runtime branch
        run_pipeline<Simd::Avx2>(data, pool);
    else
        run_pipeline<Simd::Scalar>(data, pool);
}
```

| #   | Task                                                   | Status | Notes                                                                                                                                                                           |
| --- | ------------------------------------------------------ | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2.1 | `Simd` enum + `cpu_supports_avx2()` portable detection | done   | Implemented in `bcp_simd.h`. Linux/macOS (GCC/Clang): `__builtin_cpu_supports("avx2")`. Windows/MSVC: `__cpuidex()` + `_xgetbv` guard for OS YMM state. Non-x86 returns false. |
| 2.2 | Template pipeline with `if constexpr` dispatch         | done   | `ColumnMajorTranspose::run()` checks `PYGIM_FORCE_SIMD=avx2` env var (no hardware auto-detect), plans AVX2 blocks, and dispatches to `run_impl<Simd::Avx2|Scalar>()` with `if constexpr` and compile-time `static_assert` guard. Scalar is the default; AVX2 only activates with explicit opt-in + ≥2 eligible blocks. |
| 2.3 | AVX2 8×8 int32 transpose kernel                        | done (opt-in) | Implemented 8×8 i32 and 4×4 i64 AVX2 kernels (`bcp_simd_kernels.h`). Block planning precomputes eligible contiguous groups once per run. AVX2 path is **strictly opt-in** via `PYGIM_FORCE_SIMD=avx2` with ≥2 block minimum (profile-aware). **Key finding**: transpose copy is <5% of row_loop time; `bcp_sendrow` ODBC calls dominate at 85%+. AVX2 shows no measurable throughput gain over scalar. Dedicated AVX2 TU / per-file compile flags deferred — not worth the complexity given the bottleneck location. |
| 2.4 | Handle remaining types                                 | done (gated)  | 4×4 i64/double AVX2 blocks implemented behind `PYGIM_ENABLE_AVX2_8B=1` env gate. Default-off pending further validation. Strings remain scalar.                                 |
| 2.5 | Validate with ASAN / MSAN                              | deferred | Low priority: AVX2 path is opt-in only and proven non-beneficial for throughput. Will revisit if Phase 4 connection parallelism changes the bottleneck profile.                   |
| 2.6 | Benchmark SIMD vs scalar tiled                         | done   | Benchmarked across 4 dataset profiles (simple/mixed/complex/exhaustive) at 1M rows. Results: row_major scalar wins across the board (5-10% faster than column_major). `sendrow` is 85% of row_loop; copy is <5%. SIMD on the copy phase yields microseconds, not meaningful throughput improvement. Per-row `bcp_colptr` redirect was replaced with single `memcpy` (20× cheaper). Micro-metrics promoted from hot-only to stage-level for always-on visibility. |
| 2.7 | Verify single-wheel distribution                       | open   | One manylinux2014_x86_64 wheel contains both instantiations. `pip install pygim` works on AVX2 and non-AVX2 machines. AVX2 TU excluded from ARM wheels.                         |

---

## Phase 3 — Multi-threaded Transpose

Goal: saturate memory bandwidth by parallelizing across row ranges.

**Status: deprioritized.** Phase 2 benchmarking proved that transpose/copy accounts for <5% of row_loop time. `bcp_sendrow` ODBC calls dominate at 85%+. Parallelizing the transpose across threads would save <50 ms on a 1M-row run while adding significant complexity (thread management, false-sharing avoidance, GIL coordination). **Phase 4 (connection pool + pipeline)** is the correct next target — parallelizing the `bcp_sendrow` bottleneck across multiple ODBC connections is where the real throughput gain lives.

| #   | Task                                   | Status | Notes                                                                                                                          |
| --- | -------------------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------ |
| 3.1 | Partition row ranges across N threads  | open   | Non-overlapping ranges → zero synchronization on data.                                                                         |
| 3.2 | `alignas(64)` per-thread write buffers | open   | Prevent false sharing (two threads writing adjacent cache lines).                                                              |
| 3.3 | GIL release around entire pipeline     | open   | `py::gil_scoped_release` before spawning threads. Arrow C Data Interface gives raw pointers — no Python objects touched after. |
| 3.4 | Thread count auto-tuning               | open   | Default: `min(hw_concurrency, data_size / MIN_TILE)`. Respect L3 cache capacity — too many threads thrash shared cache.        |
| 3.5 | Validate with TSAN                     | open   | Thread Sanitizer to catch races.                                                                                               |
| 3.6 | Benchmark N=1,2,4,8 threads            | open   | Expect near-linear scaling until memory bandwidth saturation (~4 threads on typical desktop).                                  |

---

## Phase 4 — Pipeline + Connection Pool

Goal: overlap CPU transpose with network I/O via parallel BCP connections.

**Architecture chosen: partitioned-parallel BCP** (not producer-consumer queue). Each worker thread gets an independent ODBC connection + BCP session and processes a row-balanced subset of Arrow RecordBatches. Simpler than a bounded queue and equally effective: transpose is <5% of time so pipelining transpose/sendrow shows no measurable benefit.

**Implementation**: `bcp_connection_pool.h` manages M pre-connected ODBC connections with BCP enabled. `bulk_insert_arrow_bcp_parallel()` in `bcp_strategy.cpp` reads all RecordBatches, partitions by row count (greedy least-loaded), launches `std::thread` workers (each with own `BcpContext`, `BcpSessionGuard`, `BatchBindingState`), joins, and merges metrics. Python API: `persist_dataframe(..., bcp_workers=N)` where `bcp_workers=0` (default) uses single-connection; `bcp_workers >= 2` activates parallel path. Falls back to single-connection when batch count < requested workers.

**Benchmark results (Docker SQL Server, 16-core host, tmpfs + delayed durability)**: After fixing a critical batch-slicing bug (Polars exports 1 RecordBatch — workers were always clamped to 1), parallel BCP shows **dramatic throughput gains**:

| Workers | Stress test (11 cols) | Benchmark complex (11 cols) |
|---------|-----------------------|-----------------------------|
| 1       | 33.7 MB/s             | 32.2 MB/s                   |
| 2       | 54.1 MB/s             | 45.9 MB/s                   |
| 4       | 65.7 MB/s             | 48.0 MB/s                   |
| 8       | 67.8 MB/s             | 76.3 MB/s                   |
| 16      | 77.8 MB/s             | —                           |

The fix: `RecordBatch::Slice()` (zero-copy) splits large single-batch Arrow exports into N sub-batches before partitioning. Additionally, `batch_flush_seconds` metric merge was corrected from `+=` (sum) to `std::max` (wall-clock).

| #   | Task                                               | Status | Notes                                                                                                                                                                          |
| --- | -------------------------------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 4.1 | Lock-free bounded queue between stages             | done (simplified) | Replaced with partitioned-parallel approach. No queue needed: each worker independently processes its Arrow batch partition. Producer-consumer adds complexity with no measurable benefit when transpose is <5% of row_loop. |
| 4.2 | ODBC connection pool                               | done   | `BcpConnectionPool` in `bcp_connection_pool.h`. Allocates M SQLHENV+SQLHDBC pairs, each with `SQL_COPT_SS_BCP` enabled. RAII cleanup, exception-safe constructor rollback. Used per parallel persist call (not persistent — no need to manage idle connection lifecycle). |
| 4.3 | Pipeline: readers produce while writers send       | done (simplified) | Workers directly read from their Arrow batch partition and sendrow. No producer-consumer split. Transpose runs inline per-worker. Architecture supports future refinement if server-side bottleneck is resolved. |
| 4.4 | Tune N (reader threads) and M (writer connections) | done   | Auto-resolution: `min(requested, hw_concurrency, num_batches)`. Single-connection fallback when actual_workers ≤ 1. Default `bcp_workers=0` = single-connection. Benchmark CLI: `--workers N`. |
| 4.5 | End-to-end benchmark                               | done   | Benchmarked with `benchmarks/bcp_throughput.py --workers N` across simple/complex profiles at 500K-1M rows. No throughput improvement in Docker environment (server-bottlenecked). See notes above. |
| 4.6 | Graceful error handling                            | done   | Each worker catches exceptions via `std::exception_ptr`. Main thread joins all workers, then rethrows first error. `BcpSessionGuard` RAII ensures `bcp_done` on any failure path. Connection pool destructor disconnects + frees all handles. |

---

## Phase 5 — Caching & Advanced Optimizations

Goal: reduce redundant work across repeated operations.

| #   | Task                                       | Status | Notes                                                                                                                                       |
| --- | ------------------------------------------ | ------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| 5.1 | Schema cache                               | open   | Cache column metadata (types, offsets, BCP bindings) across `save()` calls to same table. Avoid re-probing SQL Server schema on every call. |
| 5.2 | Buffer pool                                | open   | Reuse pre-allocated transpose buffers (`alignas(64)`) across calls instead of malloc/free each time. Thread-local or pooled.                |
| 5.3 | String interning for repeated values       | open   | If many rows share identical strings, replace with integer keys before transpose. Keys are fixed-width → SIMD-eligible.                     |
| 5.4 | `madvise(MADV_HUGEPAGE)` for large buffers | open   | 2 MB pages reduce TLB pressure for multi-MB transpose buffers.                                                                              |
| 5.5 | Prefetch hints                             | open   | `_mm_prefetch` on next tile's source while processing current tile. Profile to confirm benefit (prefetch can hurt if wrong).                |

---

## Measurement Infrastructure (cross-cutting)

Required throughout all phases. Not optional.

| #   | Task                               | Status     | Notes                                                                                               |
| --- | ---------------------------------- | ---------- | --------------------------------------------------------------------------------------------------- |
| M.1 | Per-stage timing instrumentation   | done | `QuickTimer` wraps `bind_columns`, `row_loop`, `bcp_batch` stages. Micro-metrics (fixed_copy, colptr_redirect, string_pack, sendrow) promoted from hot-only to stage-level — always collected when any timing is enabled (<1% overhead). All metrics surfaced in Python return dict via `persist_dataframe(...)['bcp_metrics']`. Benchmark script (`benchmarks/bcp_throughput.py`) displays the full breakdown inline. |
| M.2 | Hardware counter profiling harness | open       | `perf stat -e cache-misses,cache-references,instructions,cycles,mem-bandwidth` around the BCP row loop. Need the Phase 1.1 cache-miss rate to quantify how much `ColumnMajorTranspose` actually helps. Run as: `perf stat -p <PID> python benchmark.py` or via `perf record` + `perf report`. |
| M.3 | Repeatable benchmark script        | done       | `benchmarks/bcp_throughput.py` — parameterised by `--rows`, `--strategy`, `--dataset` (simple/mixed/complex/exhaustive/all), `--compare-strategies`. Reports MB/s, rows/s, per-stage timing breakdown, SIMD level. Supports `--dataset all` for side-by-side profile comparison. Requires live DB via `--conn`. |
| M.4 | Regression gate                    | open       | CI step: run `benchmark.py --rows 100000 --fast` against an in-process or local DB, compare to stored baseline JSON. Alert (fail CI) on >5% regression in MB/s. Store baseline in `benchmarks/baseline.json`, updated manually on intentional improvements. |

---

## Cross-Platform

| Platform                    | SIMD Available                 | Compile Flags       | Detection                             |
| --------------------------- | ------------------------------ | ------------------- | ------------------------------------- |
| Linux x86_64 (GCC/Clang)    | SSE2 (baseline), AVX2, AVX-512 | `-mavx2` per TU     | `__builtin_cpu_supports("avx2")`      |
| macOS x86_64 (Apple Clang)  | SSE2, AVX2                     | `-mavx2` per TU     | `__builtin_cpu_supports("avx2")`      |
| macOS ARM64 (Apple Silicon) | NEON (baseline)                | — (no AVX)          | Scalar fallback; NEON a future option |
| Windows x86_64 (MSVC)       | SSE2, AVX2                     | `/arch:AVX2` per TU | `__cpuidex()` from `<intrin.h>`       |

- **AVX2 TU isolation**: Only the transpose kernel is compiled with AVX2 flags. Everything else stays at baseline. Prevents accidental SIGILL from flag contamination.
- **ARM**: Ships scalar fallback. Network latency dominates when talking to SQL Server from ARM; NEON transpose is low priority.
- **Wheel matrix**: `cibuildwheel` produces manylinux_x86_64, macosx_x86_64, macosx_arm64, win_amd64. All contain both Scalar and AVX2 instantiations (except ARM, which has Scalar only).

---

## Design Principles

1. **External simplicity**: `repository.save(data)` — one call. All complexity is internal.
2. **Measure before optimizing**: No phase proceeds without baseline numbers from the previous phase.
3. **Incremental correctness**: Each phase must produce byte-identical results to the previous one (except for performance).
4. **Single wheel**: One `pip install pygim` works on all machines. SIMD dispatch is runtime, not build-time.
5. **`if constexpr` trampoline**: One runtime `if` at the pipeline entry point selects a fully compile-time-optimized template instantiation. No function pointers, no virtual calls, no per-iteration branches inside the hot path. Adding a new SIMD level is a compile error at every unhandled site (`static_assert`).
6. **GIL-free pipeline**: Python boundary crossing is microseconds (Arrow C Data Interface export). Everything else is pure C++ with GIL released.
7. **No false sharing**: Per-thread buffers are cache-line aligned. Non-overlapping write regions.
8. **Cross-platform by default**: Portable CPU detection (`__builtin_cpu_supports` / `__cpuidex`). AVX2 TU isolated to prevent flag contamination. ARM gets scalar fallback.
