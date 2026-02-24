# BCP Performance Bottleneck Analysis (mssql_strategy_bcp.cpp)

Date: 2026-02-24
Branch: `core/repository`
Scope: `bulk_insert_arrow_bcp` function in `mssql_strategy_bcp.cpp`
Test Runs:
- 1M rows, batch_size=50,000: **CRASHED** (memory corruption: "free(): corrupted unsorted chunks")
- 10k rows, batch_size=10,000: Successful, throughput=6.04 MB/s

## Executive Summary

Recent "improvements" introduced a memory corruption bug that causes crashes in large-scale runs (1M rows), preventing full benchmarking. The smaller 10k row test completed successfully but shows shifted phase shares: batch_flush now dominates (64.3%) over row_loop (32.6%), suggesting the changes affected the bottleneck distribution. Current small-run throughput (6.04 MB/s) is lower than the previous 1M run (23.07 MB/s), indicating potential regressions even in successful cases.

## Crash Analysis (Critical Issue)

### Finding: Memory Corruption in Large Runs
- **Evidence**: 1M row test aborts with "free(): corrupted unsorted chunks" (glibc heap corruption detection).
- **Root Cause**: Likely introduced by recent code changes (e.g., buffer management, memcpy operations, or pointer arithmetic). The crash occurs during DB write phase, suggesting issues in BCP binding, data copying, or Arrow buffer handling.
- **Impact**: Blocks performance testing and deployment. Scale-dependent, as 10k rows succeed.
- **Remediation**:
  - Debug with Valgrind or AddressSanitizer: `valgrind --tool=memcheck python __playground__/stresss_test.py --rows 100000 ...`
  - Check for buffer overflows in fixed-width binding or string view usage.
  - Revert recent changes incrementally to isolate the culprit.
  - Add bounds checking and assertions in hot paths.

## Performance Findings (From 10k Row Run)

### 1. Batch Flush Dominates (64.3% of BCP Time)
- **Evidence**: batch_flush=0.050s (64.3%), row_loop=0.026s (32.6%).
- **Root Cause**: Server-side commit overhead; TABLOCK hint still discarded.
- **Impact**: High, especially at scale.
- **Remediation**: Implement TABLOCK via bcp_control (as in detailed analysis).

### 2. Prep Phase Significant (53.8% of Total)
- **Evidence**: prep=0.092s (53.8%), write_call=0.079s (46.2%).
- **Root Cause**: Data generation/conversion dominates small runs.
- **Impact**: Indicates client-side bottlenecks in small tests.

### 3. Row Loop Reduced (32.6%)
- **Evidence**: Lower than previous 54.4%, possibly due to optimizations.
- **Root Cause**: Changes may have reduced ODBC calls.

## Prioritized Remediation Plan

### P0 (Critical)
- Fix memory corruption bug (debug and revert faulty changes).
- Re-enable full-scale testing.

### P1 (Post-Fix)
- Implement TABLOCK hint for batch_flush reduction.
- Optimize ODBC calls and allocations as per detailed analysis.

### P2
- Benchmark improvements against historical 63.63 MB/s target.

## Acceptance Gates
- Fix crash; reproduce 1M row run successfully.
- Track: rows/s, MB/s, phases; aim for >40 MB/s post-fixes.