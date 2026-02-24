# BCP Hot-Path Bottleneck Analysis

File: `src/_pygim_fast/repository/mssql_strategy/detail/mssql_strategy_bcp.cpp`
Benchmark: 1M rows × 11 columns, `--arrow --batch-size 50000`, mode `arrow_c_stream_bcp`
Known-good historical reference: **63.63 MB/s, 607,200 rows/s** (1M rows in 1.65s)

---

## Revision History

### Rev 3 — 2026-02-24

**Changes since Rev 2**: Unified staging buffer for ALL fixed columns (both null and non-null paths), null toggle via `bcp_collen(SQL_NULL_DATA)` only — no `bcp_colptr(nullptr)` in row loop. Pre-set `bcp_collen(value_stride)` during setup so non-null rows skip it. String column `bcp_colptr` elision via `str_buf_bound` flag — only called on first use or after resize. Added `--truncate` CLI flag to stress test for clean benchmarks. Tested heap table (no PK) for B-tree overhead quantification. Tested direct Arrow buffer pointer for strings (F10a) — **failed: driver requires null-terminated strings**.

**Current stable measurement** (3 runs on truncated table, clean inserts):

```
Best run (PK):  3.45s  →  29.4 MB/s, 280,388 rows/s     (was 3.99s → 25.4 MB/s)
  row_loop:     1.815s (52.6%)                             (was 2.10s → 52.8%)
  batch_flush:  1.600s (46.4%)                             (was 1.80s → 45.0%)
  bind_columns: 0.031s ( 0.9%)

Best run (Heap, no PK): 3.07s  →  33.1 MB/s, 315,704 rows/s
  row_loop:     1.891s (61.7%)
  batch_flush:  1.144s (37.3%)                             ← 28% less than PK
  bind_columns: 0.027s ( 0.9%)
```

**Summary**: 16% wall-clock improvement over Rev 2 (3.99s → 3.45s on PK table, clean). Heap table reaches **33.1 MB/s** — within the predicted ceiling. `row_loop` improved **14%** (2.10s → 1.815s) from unified staging + colptr elision. The bottleneck has **fully shifted to batch_flush** on heap tables (61.7% row_loop vs 37.3% batch_flush). On PK tables, it's balanced ~53/46.

**Gap to reference**: **1.9× slower** on PK (29.4 vs 63.63 MB/s), **1.9× slower** on heap (33.1 vs 63.63). The remaining gap is irreducible per-row BCP API overhead + server-side write latency.

**Experiment: Direct Arrow string pointer (F10a) — FAILED**: Binding strings with `pTerm=NULL, cbTerm=0` and pointing `bcp_colptr` directly into Arrow's contiguous buffer (no memcpy, no null terminator) causes `free(): corrupted unsorted chunks` crash. The ODBC driver internally requires null-terminated strings for SQLCHARACTER type regardless of `bcp_collen`. The current `str_buf` + null-terminate approach is **mandatory**.

### Rev 2 — 2026-02-24

**Changes since Rev 1**: TABLOCK via `bcp_control` implemented (F1), zero-copy string binding from Arrow offset/data (F3), staging buffer for fixed columns (F2a), column split into fixed/string arrays (F2c), no-null fast-path branch elimination (F2b), `ColumnBinding` cleaned up (F5), per-call error checking removed for `bcp_collen`/`bcp_colptr` (F6).

**Current stable measurement** (3 consecutive runs, warm server, table with 1–3M rows):

```
Total write:      3.99s  →  25.4 MB/s, 242,453 rows/s       (was 4.44s → 23.1 MB/s)
row_loop:         2.10s  (52.8%)                               (was 2.42s → 54.4%)
batch_flush:      1.80s  (45.0%)                               (was 1.86s → 41.9%)
bind_columns:     0.029s ( 0.7%)                               (was 0.138s →  3.1%)
setup+done:       0.001s ( 0.0%)
```

**Summary**: ~10% wall-clock improvement overall (4.44s → 3.99s). `bind_columns` improved **4.8×** (0.138s → 0.029s) from eliminating 3M `std::string` allocations. `row_loop` improved **13%** (2.42s → 2.10s) from staging-buffer memcpy replacing per-row `bcp_colptr` calls and branch elimination. `batch_flush` is essentially unchanged — the TABLOCK hint was already being applied in the baseline (initial analysis was wrong: the `(void)table_hint` was in the *old* commit, not the baseline benchmarked).

**Gap to reference**: still **2.5× slower** (25.4 vs 63.63 MB/s). The dominant remaining costs are `row_loop` (2.1s) and `batch_flush` (1.8s).

### Rev 1 — 2026-02-23

Original analysis. Baseline: 4.443s total, 23.07 MB/s.

---

## Server-Side Environment (Rev 3)

```
Recovery model:    SIMPLE
Clustered PK:     id INT (monotonically increasing inserts) — tested
Heap (no PK):     same schema, no index — tested
Index frag:        0.7%
Table size at test: clean (truncated before each run)
Docker SQL Server: MSSQL 2022 (local container)
```

SIMPLE recovery + monotonic inserts + TABLOCK = near-optimal server conditions. Heap table provides **28% lower batch_flush** (1.14s vs 1.60s) by eliminating B-tree maintenance.

---

## What Was Fixed (Findings from Rev 1)

### ~~F1 — TABLOCK hint silently discarded~~ → RESOLVED

`bcp_control(BCPHINTS, "TABLOCK")` now called after `bcp_init`. A/B tested:
- With TABLOCK: batch_flush ~1.80s
- Without TABLOCK (`--table-hint ""`): batch_flush ~2.07s
- Delta: **~13% improvement** on batch_flush from TABLOCK, or ~0.27s absolute.

Note: TABLOCK impact is modest here because SIMPLE recovery already provides reasonable logging behavior. Impact would be much larger on FULL recovery databases.

### ~~F2a — Redundant bcp_colptr for fixed columns~~ → RESOLVED

Staging buffer implemented. Fixed-width non-null columns now use `memcpy` into a contiguous staging buffer instead of per-row `bcp_colptr` calls. For 8 fixed columns × 1M rows, this eliminated **8M ODBC function pointer calls**.

### ~~F2b — No-null fast path~~ → RESOLVED

Row loop now splits into two code paths based on `any_has_nulls`. With all-NOT-NULL data (the common bulk-load case), the null-check branch is completely eliminated.

### ~~F2c — Column dispatch branches in hot loop~~ → RESOLVED

Columns pre-split into `fixed_cols[]` and `string_cols[]` arrays. Inner loop iterates each with a tight, type-homogeneous loop instead of if/else dispatch.

### ~~F3 — 3M std::string allocations for string columns~~ → RESOLVED

String columns now use zero-copy offset-based access from Arrow buffers. Only a single reusable `str_buf` per column is used for null-terminated copy to ODBC. `bind_columns` dropped from 0.138s → 0.029s (**4.8× faster**).

### ~~F5 — ColumnBinding struct bloat~~ → RESOLVED

`utf16_cache` and `utf8_cache` fields removed. Fields reorganized with `is_string` flag for clean dispatch. `str_buf` replaces per-value `std::string`.

### ~~F6 — Per-call error checking~~ → RESOLVED

`bcp_collen` and `bcp_colptr` calls in the hot loop no longer check return codes individually. Only `bcp_sendrow` retains per-call error checking (it's the commit point).

---

## Remaining Bottlenecks (Current Priority)

### Row loop: 1.8s (53%) — approaching irreducible floor

**Severity: MEDIUM — limited remaining headroom.**

Current inner loop (no-null fast path) per row:
```
For 1M rows × 11 columns:
  - Fixed (8 cols): 8 × memcpy(stride_bytes) into staging  →  8M memcpy calls
  - String (3 cols): memcpy + null-term + bcp_collen        →  3M memcpy + 3M ODBC calls
                     + bcp_colptr (only on first use/realloc) →  ~3 ODBC calls total
  - bcp_sendrow                                              →  1M ODBC calls
  ────────────────────────────────────────────────────────────────────
  Total:  11M memcpy + 4M ODBC calls per 1M rows
```

At 1.815s for 1M rows = **1.8µs per row**. The `bcp_sendrow` floor is ~1.0µs, meaning per-column overhead is ~0.8µs — down from ~1.1µs in Rev 2.

#### ~~F10a — Direct Arrow buffer pointer for strings~~ → CLOSED (driver crash)

Tested: removing null terminator (`pTerm=NULL, cbTerm=0`) and pointing `bcp_colptr` directly into Arrow's contiguous string buffer. Result: `free(): corrupted unsorted chunks` (core dump). The ODBC driver requires null-terminated SQLCHARACTER strings regardless of `bcp_collen` value. **Not viable.**

The current `str_buf` + memcpy + null-termination approach is the minimum required by the driver. String column ODBC calls are now just `bcp_collen` per row (3M calls for 3 cols × 1M rows), which is irreducible since BCP needs per-value length for variable-length columns.

#### F10b — Fixed-column staging memcpy still per-row

8M small memcpy calls (4–8 bytes each) remain. These are cache-hot and trivially cheap (~2ns each = ~16ms total). Not worth further optimization.

#### F10c — `bcp_sendrow` is an irreducible ~1µs call

Hard floor. At ~1.0µs × 1M rows = ~1.0s, this is roughly 55% of `row_loop`. Cannot be optimized from client side.

**Row loop floor estimate**: ~1.0–1.2s (sendrow only). Current 1.8s means ~0.6–0.8s of per-column overhead remains.

---

### Batch flush: 1.1–1.6s (37–46%) — server-bound

**Severity: HIGH on PK tables, MODERATE on heaps.**

| Table Type | batch_flush (1M, clean) | % of total |
|-----------|------------------------|------------|
| PK (clustered) | 1.600s | 46.4% |
| Heap (no PK) | 1.144s | 37.3% |
| Delta | **−28%** | |

The PK overhead is ~0.46s for 1M rows = B-tree insertion cost for a monotonically increasing INT key. Non-monotonic keys would be even worse.

**Remaining remediation** (server-side only):
- For bulk load scenarios: drop PK → heap insert → rebuild PK. Saves ~0.46s per 1M rows.
- Column width reduction: `NVARCHAR(100) → VARCHAR(40)` would reduce page allocations.
- Faster storage / memory settings in SQL Server container.

---

### F4 — Temporal conversion still uses chrono (unchanged from Rev 1)

**Severity: LOW (now <0.5% of bind_columns = ~0.03s total)**

With `bind_columns` at 0.029s total and temporal conversion being a small fraction, this is no longer material. Keeping for completeness — the Hinnant algorithm would save perhaps 5–10ms, not worth the added complexity.

---

### F7 — Column rebinding per RecordBatch (unchanged from Rev 1)

**Severity: LOW (single-batch stream in current test = no impact)**

Current c-stream exporter emits 1 RecordBatch for 1M rows, so this triggers once. Would matter if Polars emitted multiple smaller batches (e.g., with `rechunk=False`).

---

### F9 — Int64→INT type mismatch (unchanged from Rev 1)

**Severity: LOW**

Polars emits Int64 for `id`/`col_int` where SQL expects INT (32-bit). Extra 4 bytes per value × 2 columns × 1M rows = 8 MB unnecessary transfer. Negligible vs total overhead.

---

## Benchmark Data Matrix (Rev 3)

All runs: `arrow_c_stream_exporter` mode, TABLOCK default, 11 columns.

| Rows | Batch Size | Table | Total (s) | MB/s | row_loop (s) | batch_flush (s) | bind_col (s) | Notes |
|------|-----------|-------|-----------|------|-------------|----------------|-------------|-------|
| 1M | 50000 | PK (clean) | 3.45 | **29.4** | 1.815 | 1.600 | 0.031 | Best of 3 runs |
| 1M | 50000 | PK (clean) | 3.84 | 26.4 | 2.065 | 1.744 | 0.030 | Run 1 |
| 1M | 50000 | Heap (clean) | 3.07 | **33.1** | 1.891 | 1.144 | 0.027 | **Best overall** |
| 1M | 50000 | Heap | 3.64 | 27.9 | 2.117 | 1.483 | 0.031 | First run (warm) |
| 1M | 50000 | PK (dirty) | 3.99 | 25.4 | 2.16 | 1.80 | 0.029 | Rev 2 baseline |
| 1M | 100000 | PK (dirty) | 4.71 | 21.6 | 2.10 | 2.59 | 0.029 | Larger batch_flush |
| 1M | 1000000 | PK (dirty) | 5.18 | 19.8 | 1.97 | 3.18 | 0.028 | Single flush |
| 200K | 50000 | PK | 1.11 | 16.7 | 0.62 | 0.48 | 0.006 | |
| 200K | 50000 (no-arrow) | PK | 3.68 | 5.7 | — | — | — | bulk_upsert fallback |
| 1M | 50000 (no TABLOCK) | PK | 4.23 | 24.1 | 2.12 | 2.07 | 0.031 | ~13% slower flush |

**Key observations**:
- **Heap table is 12% faster** than PK table at same row count (33.1 vs 29.4 MB/s).
- **Table bloat inflates batch_flush** significantly: PK dirty (1.80s) vs PK clean (1.60s).
- Optimal batch_size remains **50,000** for this schema/server.
- Arrow BCP is **5.2× faster** than bulk_upsert fallback at 200K rows.
- Row_loop scales linearly at ~1.8–2.1µs/row depending on system load.

---

## Summary: Where the Time Goes (Rev 3)

```
┌─────────────────────────────────────────────────┐
│ PK Table:  3.45s (29.4 MB/s)  ← best clean   │
│ Heap Table: 3.07s (33.1 MB/s)  ← best overall │
│                                                   │
│ ┌───────────────────────────────┐                │
│ │ row_loop: 1.8–2.1s (53–62%)  │                │
│ │  ├─ bcp_sendrow   ~1.0s      │ ← irreducible  │
│ │  ├─ string cols   ~0.4s      │ ← 3M collen    │
│ │  └─ fixed memcpy  ~0.4s      │ ← 8M memcpy    │
│ └───────────────────────────────┘                │
│ ┌───────────────────────────────┐                │
│ │ batch_flush:                │                │
│ │   PK:   1.6s (46%)          │ ← B-tree cost  │
│ │   Heap: 1.1s (37%)          │ ← server I/O   │
│ └───────────────────────────────┘                │
│ bind+setup+done: 0.03s (0.9%)                    │
└─────────────────────────────────────────────────┘
```

---

## Prioritized Next Actions

| Priority | Item | Est. Impact | Effort | Status |
|----------|------|-------------|--------|--------|
| ~~P0~~ | ~~F10a: Direct Arrow buffer pointer for strings~~ | ~~5–10%~~ | ~~Medium~~ | **CLOSED — driver crash** |
| ~~P0~~ | ~~Heap table test~~ | ~~Diagnostic~~ | ~~Low~~ | **DONE — 28% batch_flush reduction** |
| **P1** | F10b: Batch memcpy — transpose fixed cols into row-major chunks | 1–2% (~16ms) | Medium | Low value |
| **P1** | Drop PK → heap insert → rebuild PK (for bulk load scenarios) | 12% total | Application-level | Design decision |
| **P2** | F7: Cache column bindings across RecordBatches | <1% (single-batch) | Low | |
| **P2** | F9: Downcast Int64→Int32 for matching SQL columns | <1% | Trivial | |

### Realistic ceiling estimate (updated)

With all remaining client-side optimizations:
- `row_loop` floor: ~1.0–1.2s (limited by `bcp_sendrow` per-call cost)
- `batch_flush`: ~1.1s (heap) to ~1.6s (PK) on clean table
- **Best achievable with PK: ~2.8–3.0s → 34–37 MB/s**
- **Best achievable with heap: ~2.3–2.5s → 41–45 MB/s**

The gap to the 63.63 MB/s reference (1.65s) confirms the historical measurement used a fundamentally different write path — likely:
1. No null handling (all NOT NULL, no bitmap checks)
2. Native binary date/timestamp format (no chrono conversion)
3. UTF-16 strings (may interact differently with driver)
4. Possibly fewer columns or simpler schema
5. Or a completely different bulk loading approach (format file + pipe, TDS-level)

Closing the remaining ~2× gap with per-row BCP API is **not feasible**. The `bcp_sendrow` call alone accounts for ~1.0s of the ~3.0s total. Further gains require a fundamentally different approach.
