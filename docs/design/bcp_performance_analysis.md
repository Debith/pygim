# BCP Performance Bottleneck Analysis (mssql_strategy_bcp.cpp)

Date: 2026-02-23
Branch: `core/repository`
Scope: `bulk_insert_arrow_bcp` function in `mssql_strategy_bcp.cpp`
Test Run: 1M rows, batch_size=50,000, arrow_c_stream_exporter mode, throughput=23.07 MB/s

## Executive Summary

Based on code analysis and latest stress test results, the dominant bottlenecks in the BCP implementation are excessive ODBC API calls in the row loop (54.4% of BCP time) and expensive batch flushes (41.9%). Current throughput (23.07 MB/s) remains below the historical target (63.63 MB/s), with millions of per-cell ODBC calls and server-side commit overhead as key contributors. Memory allocations for strings/temporal data add secondary pressure.

## Detailed Findings

### 1. Excessive ODBC API Calls in Row Loop (High Impact - 54.4% of BCP Time)
- **Evidence**: Row loop: 2.419s (54.4% of 4.443s BCP time) for 1M rows. ~11M `bcp_colptr` calls + `bcp_collen` for nulls/variable data, plus 1M `bcp_sendrow` calls.
- **Root Cause**: BCP is row-oriented; even bound fixed-width columns require per-row `bcp_colptr` to advance pointers. Variable-length strings require per-cell `bcp_collen`/`bcp_colptr`. ODBC call overhead accumulates with high density.
- **Impact**: Core bottleneck; historical regressions may amplify call latency.
- **Remediation**:
  - Investigate array binding or bulk modes in ODBC/BCP.
  - Profile call latency; minimize calls via pre-computed buffers.
  - Test smaller batches to isolate call density vs. total volume.

### 2. Expensive Batch Flushes (High Impact - 41.9% of BCP Time)
- **Evidence**: Batch flush: 1.860s (41.9%) across ~20 batches (1M / 50k).
- **Root Cause**: `bcp_batch` commits batches via network I/O, logging, and server processing. Per-batch overhead high without optimizations like TABLOCK.
- **Impact**: Combines with row loop for 96.3% of write time.
- **Remediation**:
  - Increase batch_size (e.g., 100k-500k).
  - Enable TABLOCK hint to reduce locking.
  - Profile server-side (logs, indexes, tempdb).

### 3. Memory Allocations and Data Conversions (Medium-High Impact)
- **Evidence**: Bind_columns: 0.138s; allocates vectors of strings/structs for temporal/string columns.
- **Root Cause**: `typed->GetString(i)` and chrono conversions create per-value copies. UTF8 used but server may expect UTF16.
- **Impact**: Allocation overhead in bind phase; amplifies cell processing.
- **Remediation**:
  - Use zero-copy Arrow views.
  - Pre-allocate reusable buffers.
  - Bind native temporal types; confirm encoding (switch to UTF16 if needed).

### 4. Suboptimal Binding for Fixed-Width Columns (Medium Impact)
- **Evidence**: Bound once but `bcp_colptr` called per row per column anyway.
- **Root Cause**: BCP requires per-row pointer updates for row-oriented insertion.
- **Impact**: Increases call count unnecessarily.
- **Remediation**:
  - Explore column-major BCP modes.
  - Test skipping `bcp_colptr` for non-null fixed-width (if binding suffices).

### 5. C-Stream Path and Fallbacks (Low-Medium Impact, Resolved)
- **Evidence**: Using `arrow_c_stream_exporter`; no IPC fallback in test.
- **Root Cause**: Previously failed, adding overhead; now stable.
- **Impact**: Minimal currently.
- **Remediation**: Add warnings for c-stream failures; strengthen bridge.

### 6. Other Issues
- Batch size defaults too conservative (raise to 10k-100k).
- No parallelism; single-threaded.
- Environment variability (server config affects throughput).

## Prioritized Remediation Plan

### P0 (Immediate)
- Raise default batch_size for Arrow BCP (e.g., 50k-100k).
- Add TABLOCK hint support and warnings.

### P1 (High Impact)
- Reduce ODBC calls: Profile and optimize binding strategy.
- Optimize allocations: Zero-copy strings, native temporals.

### P2 (Structural)
- Rewrite BCP for bulk array inserts if feasible.
- Add comprehensive benchmarks (rows, batch_size, modes).

## Acceptance Gates
- Reproduce runs at 200k/1M rows, batch_size 10k/50k/100k.
- Target: Approach 63.63 MB/s historical throughput.
- Track: rows/s, MB/s, prep/write phases.