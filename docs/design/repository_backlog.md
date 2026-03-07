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
| 0.4 | Unified `save(data, hint?)` API                 | open       | Current entry point is `persist_dataframe(table, data, ...)`. Rename to `save(table, data)` with optional `hint` kwarg that selects `TransposeStrategy` (e.g., `hint="column_major"` → `ColumnMajorTranspose`, `hint="avx2"` → Phase 2 kernel). Arrow vs bulk-upsert detection stays automatic via `ExtractionPolicy`. Removes the three-name surface (`persist_dataframe` / `bulk_insert` / `bulk_upsert`) in favour of one call. |
| 0.5 | Update abstract diagrams                        | open       | `repository_architecture_abstract.puml` and `repository_sequence_abstract.puml` still reference the old IPC / `compat_level=oldest` path that was removed. Update to reflect: ExtractionPolicy → Arrow C stream direct → BCP strategy; compat path gone; TransposeStrategy hierarchy in BCP layer. |
| 0.6 | Public API: `acquire_repository` + `StatusPrinter` | done    | `acquire_repository(conn_str, printer=None)` is a pybind11 free function in `_repository.cpp`. Prints `connecting  <masked_uri>` to stdout before connecting (separate from logging). `StatusPrinter(connection=True/False)` controls per-category output; additional flags can be added as new categories emerge. `repository.py` removed — all symbols exported directly from the C++ extension. |

---

## Phase 1 — Tiled Transpose (single-threaded, scalar)

Goal: eliminate the biggest CPU bottleneck with zero additional dependencies.

| #   | Task                                            | Status     | Notes                                                                                            |
| --- | ----------------------------------------------- | ---------- | ------------------------------------------------------------------------------------------------ |
| 1.1 | Benchmark current naive col→row transpose       | done       | **Baseline: 22.71 MB/s** (1,000,000 rows × 11 cols, 4.61 s, 10 BCP commits). Strategy: `RowMajorTranspose` — per-row outer loop, per-column `memcpy` into contiguous staging buffer, `bcp_sendrow` per row. Bottleneck is `bcp_sendrow` call overhead (1M ODBC calls) plus read-jumping across 11 Arrow column buffers each row. `perf stat` cache-miss rate not yet collected. |
| 1.2 | Implement cache-tiled transpose                 | in-progress | `ColumnMajorTranspose` (128-row mini-batch) is implemented in `bcp_transpose_strategy.h`. Column-major fill reads each Arrow column buffer sequentially for 128 rows (L1-friendly), writing into a `128 × row_width` staging block. **Known issue**: a second `memcpy` then copies each row from the mini-batch block back into the single-row staging buffer that `bcp_colptr` points at — this double-copy partially negates the read-locality gain. Next step: eliminate the second copy by redirecting `bcp_colptr` into the mini-batch buffer per-row (or pre-computing per-column BCP pointer arrays) before `bcp_sendrow`. Not yet benchmarked vs `RowMajorTranspose`. |
| 1.3 | Separate fixed-width and variable-width columns | done       | `classify_columns()` in `bcp_bind.h` splits bindings into `fixed` (INT32, INT64, UINT8, DOUBLE, DATE32, TIMESTAMP) and `string` (STRING, LARGE_STRING, STRING_VIEW) at bind time. Fixed: `memcpy` path via contiguous staging. Strings: per-row `handle_string_column()` with zero-copy offset extraction and reusable null-terminated buffer. DATE32 and TIMESTAMP are pre-converted at bind time (`days_to_sql_date`, `micros_to_sql_timestamp`) into typed SQL structs; their staging slots hold those structs. |
| 1.4 | Validate correctness                            | open       | No explicit comparison test between `RowMajorTranspose` and `ColumnMajorTranspose` output yet. Need a round-trip test: insert 1 M rows via each strategy → `SELECT` → compare byte-by-byte. Cover all 8 Arrow types: INT32, INT64, UINT8, DOUBLE, DATE32, TIMESTAMP, STRING, STRING_VIEW. Include null rows (bitmap path). |
| 1.5 | Benchmark tiled vs naive                        | open       | Wire `ColumnMajorTranspose` into the BCP path (set `ctx.transpose`) and run the same 1 M-row benchmark. First fix the double-copy issue (1.2) before benchmarking — otherwise the measurement includes redundant work. Expected: improvement visible on fixed-width-heavy tables (fewer Arrow buffer jumps per row); smaller gain when string columns dominate (string path is unchanged). |

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
| 2.1 | `Simd` enum + `cpu_supports_avx2()` portable detection | open   | Linux: `__builtin_cpu_supports("avx2")`. macOS/Clang: same. Windows/MSVC: `__cpuidex()` from `<intrin.h>`.                                                                      |
| 2.2 | Template pipeline with `if constexpr` dispatch         | open   | Single trampoline `if` at entry point. Entire pipeline templated on `Simd` level. Compiler stamps out two fully-optimized instantiations; linker ICF merges identical sections. |
| 2.3 | AVX2 8×8 int32 transpose kernel                        | open   | `_mm256_unpacklo/hi_epi32` cascade. Process 8 rows × 8 columns in one register set. Compiled in a separate TU with `-mavx2` / `/arch:AVX2`.                                     |
| 2.4 | Handle remaining types                                 | open   | int64/double via 4×4 AVX2 blocks. Strings remain scalar (prefetch offset arrays).                                                                                               |
| 2.5 | Validate with ASAN / MSAN                              | open   | Catch alignment, bounds, and uninitialized-memory issues.                                                                                                                       |
| 2.6 | Benchmark SIMD vs scalar tiled                         | open   | Target: 2-4× on top of Phase 1 for fixed-width columns.                                                                                                                         |
| 2.7 | Verify single-wheel distribution                       | open   | One manylinux2014_x86_64 wheel contains both instantiations. `pip install pygim` works on AVX2 and non-AVX2 machines. AVX2 TU excluded from ARM wheels.                         |

---

## Phase 3 — Multi-threaded Transpose

Goal: saturate memory bandwidth by parallelizing across row ranges.

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

Goal: overlap CPU transpose with network I/O via producer-consumer architecture.

| #   | Task                                               | Status | Notes                                                                                                                                                                          |
| --- | -------------------------------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 4.1 | Lock-free bounded queue between stages             | open   | Transposed row batches → queue → writer threads. `moodycamel::ConcurrentQueue` or simple SPMC ring buffer. Bounded depth controls memory.                                      |
| 4.2 | ODBC connection pool                               | open   | M pre-opened connections. Each writer thread acquires one, does `bcp_sendrow` loop + `bcp_done`, releases. SQL Server `TABLOCK` hint enables parallel bulk insert server-side. |
| 4.3 | Pipeline: readers produce while writers send       | open   | Hides network latency behind CPU work (and vice versa).                                                                                                                        |
| 4.4 | Tune N (reader threads) and M (writer connections) | open   | Sweep benchmark: data shape × network latency × server load.                                                                                                                   |
| 4.5 | End-to-end benchmark                               | open   | Compare full pipeline vs Phase 3 (single-connection). Target: 2-4× from connection parallelism + pipeline overlap.                                                             |
| 4.6 | Graceful error handling                            | open   | Writer failure → drain queue, report partial progress. Connection failure → retry or re-acquire from pool.                                                                     |

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
| M.1 | Per-stage timing instrumentation   | in-progress | `QuickTimer` (`utils/quick_timer.h`) is embedded in `BcpContext` and already wraps `bind_columns`, `row_loop`. Subtimers: `bind_columns`, `row_loop`, `bcp_batch`. Results printed on destruction. Not yet exposed to Python or to the `BcpMetrics` result dict returned from `persist_dataframe`. Next: surface per-stage numbers in the Python return value so callers can see where time is spent without `perf stat`. |
| M.2 | Hardware counter profiling harness | open       | `perf stat -e cache-misses,cache-references,instructions,cycles,mem-bandwidth` around the BCP row loop. Need the Phase 1.1 cache-miss rate to quantify how much `ColumnMajorTranspose` actually helps. Run as: `perf stat -p <PID> python benchmark.py` or via `perf record` + `perf report`. |
| M.3 | Repeatable benchmark script        | open       | Write `benchmarks/bcp_throughput.py`. Parameterise: `--rows`, `--cols`, `--col-types` (int-heavy / string-heavy / mixed), `--strategy` (row_major / column_major / avx2), `--db` (local / remote). Output: MB/s, rows/s, cells/s, per-stage times from `BcpMetrics`. Compare against commit-tagged baseline. The playground `stress_test.py` is a starting point but hardcodes schema and connection. |
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
