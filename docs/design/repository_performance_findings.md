# Repository Performance Findings (MSSQL Arrow/BCP)

Date: 2026-02-23
Branch: `core/repository`
Scope: `Repository.persist_dataframe(...)` MSSQL write path (`arrow_*_bcp` and `bulk_upsert`)

## Executive Summary

- Throughput is materially below the known-good historical level.
- Current Arrow runs are functional, but the runtime usually lands on `arrow_ipc_bcp` (not zero-copy c-stream) in this environment.
- The dominant cost is the native write call, not data generation.
- `batch_size` is a high-leverage control; current defaults are conservative for BCP and leave performance on the table.

## Known-Good Historical Reference

User-reported prior run (same workload shape, 1,000,000 rows × 11 columns):

- `DB write throughput: 63.63 MB/s, 607,200 rows/s, 6,679,204 cells/s (1,000,000 rows × 11 columns = 11,000,000 cells in 1.65s)`

This is the regression target reference for this branch analysis.

## Measured Baseline (Current Branch)

All runs below were executed with `conda run -n py312` from repo root.

### 1M rows

- `--arrow --batch-size 1000`: ~`6.38 MB/s` (`60,917 rows/s`) in one run.
- `--arrow --batch-size 100000`: ~`15.47 MB/s` (`147,641 rows/s`) in one run.
- User-observed `--arrow` run: ~`5.61 MB/s` (`53,499 rows/s`).

### 200k rows (A/B sanity)

- `--arrow --batch-size 1000`: ~`7.58 MB/s`.
- `--arrow --batch-size 10000`: ~`12.08 MB/s`.
- `--arrow --batch-size 100000`: ~`14.45 MB/s`.
- `--no-arrow` (bulk_upsert): ~`5.91 MB/s`.

Conclusion from A/B: Arrow path is generally faster than fallback, but still far below the known-good historical reference (63.63 MB/s).

## What Is Going Wrong (Critical and Blunt)

## 1) Zero-copy c-stream path is effectively unavailable in this environment

- Runtime reports c-stream failures (unsupported view format, `'vu'`), then falls back to IPC.
- This means the intended fastest path is usually skipped.
- Relevant code path: `src/_pygim_fast/repository/mssql_strategy/mssql_strategy.cpp`.

Impact: high. Losing c-stream means extra serialization work and additional failure/retry overhead.

## 2) BCP hot loop is call-heavy by design

- Inner loop performs per-cell `bcp_collen` + `bcp_colptr` then per-row `bcp_sendrow`.
- At 1M rows × 11 columns, this is tens of millions of ODBC calls even before network/log flush effects.
- Relevant code path: `src/_pygim_fast/repository/mssql_strategy/detail/mssql_strategy_bcp.cpp`.

Impact: very high. This is the dominant write-path overhead after payload prep.

## 3) String/date/timestamp handling is allocation-heavy

- Date/timestamp values are formatted into strings (`snprintf` + `std::string`) instead of binding native temporal representations.
- String-like Arrow columns are materialized into owned `std::string` buffers before writes.

Impact: high for mixed schemas with multiple textual/temporal columns.

## 4) Current defaults are not tuned for BCP throughput

- `batch_size=1000` is stable but conservative; measured throughput improves strongly at larger values.
- This mismatch can make performance appear “regressed” even when correctness is fixed.

Impact: high and immediately actionable.

## 5) Correctness fixes can expose hidden prior bugs that looked “fast”

- A prior bug path around fixed-width row pointer handling caused wrong-row behavior (duplicate key symptoms), which can distort old performance impressions.
- Correctness is now improved, but with clear overhead trade-offs.

Impact: contextual but important for comparing old vs new numbers.

## Why “1k used to be much faster” can still be true

Possible contributors (non-exclusive):

- Different runtime/driver/library combo previously (Polars/PyArrow/Arrow C++/ODBC driver versions).
- Older path may have accidentally skipped work (buggy pointer binding, fewer effective conversions).
- Table/server state changed (index fragmentation, log pressure, autogrowth, tempdb contention).
- Current path incurs c-stream failure + IPC fallback overhead before writing.

This is why same `--batch-size 1000` can legitimately deliver very different throughput across revisions.

## Prioritized Remediation Plan

## P0 (Immediate)

1. Separate Arrow BCP default batch from generic fallback defaults and raise it materially (e.g., 10k–100k).
2. Add an explicit startup warning when c-stream is unavailable, so operators know they are on IPC fallback.
3. Add a small benchmark matrix script (`rows`, `batch_size`, `arrow on/off`) and store output snapshots under `docs/design/`.

## P1 (High impact)

1. Rework BCP binding strategy to reduce per-cell API churn where feasible (especially fixed-width columns).
2. Replace string-formatted temporal serialization with native temporal binding where ODBC/BCP supports it robustly.

## P2 (Structural)

1. Implement a robust c-stream bridge for current Polars/PyArrow APIs and compatibility modes.
2. Keep fallback behavior, but make c-stream path truly first-class and continuously benchmarked.

## Suggested Perf Acceptance Gates

- Record and track at least:
  - `rows/s`, `MB/s`, `prep_seconds`, `write_seconds`, mode (`arrow_c_stream_bcp|arrow_ipc_bcp|bulk_upsert`).
- Gate changes with reproducible runs at:
  - `rows=200000` and `rows=1000000`
  - `batch_size in {1000, 10000, 100000}`
  - `--arrow` and `--no-arrow`

## Scope Notes

- Findings are specific to current branch and test environment.
- Numbers are expected to vary with SQL Server settings, storage, and driver/runtime versions.
- The gap to the known-good 63.63 MB/s run is real in current measurements and should be treated as an open performance regression until proven otherwise via repeatable benchmarks.
