# BCP Hot-Path Bottleneck Analysis

Date: 2026-02-23  
File: `src/_pygim_fast/repository/mssql_strategy/detail/mssql_strategy_bcp.cpp`  
Benchmark: 1M rows × 11 columns, `--arrow --batch-size 50000`, mode `arrow_c_stream_bcp`

## Benchmark Baseline

```
Total write:      4.443s  →  23.07 MB/s, 220,154 rows/s
row_loop:         2.419s  (54.4%)
batch_flush:      1.860s  (41.9%)
bind_columns:     0.138s  ( 3.1%)
setup+done:       0.001s  ( 0.0%)
```

Known-good historical reference: **63.63 MB/s** — current is **2.76× slower**.

---

## Finding 1 — TABLOCK hint is accepted but silently discarded

**Severity: HIGH (server-side throughput)**

```cpp
void MssqlStrategyNative::bulk_insert_arrow_bcp(const std::string &table,
                                                const py::object &arrow_ipc_payload,
                                                int batch_size,
                                                const std::string &table_hint) {
    ...
    (void)table_hint;   // ← discarded
```

BCP supports `bcp_control(BCPHINTS, "TABLOCK")` which enables **minimal logging** on the server. Without it, SQL Server uses fully-logged row inserts even into a heap or empty clustered table. This alone can account for a large share of the `batch_flush` cost (1.860s / 41.9%).

**Remediation**: Call `bcp_control(m_dbc, BCPHINTS, (void*)"TABLOCK")` (or the user-supplied hint) after `bcp_init` and before the first `bcp_sendrow`. Will need to add `bcp_control` to the `dlsym` loader. This is likely the single highest-leverage fix available.

---

## Finding 2 — Per-row, per-column BCP API churn dominates the row loop

**Severity: CRITICAL (54.4% of total)**

The inner loop at lines 427–502 executes for every row × every column:

```
For 1M rows × 11 columns:
  - String columns (3):  bcp_collen + bcp_colptr per row  → 6M calls
  - Fixed columns (8):   bcp_colptr per row               → 8M calls
  - bcp_sendrow per row:                                   → 1M calls
  ────────────────────────────────────────────────────────────────────
  Total:                                                    ~15M ODBC calls
```

Each of these 15 million calls goes through a **dlsym-resolved function pointer** (indirect call, no inlining possible). The BCP API fundamentally requires per-row `bcp_sendrow`, but the per-column overhead can be reduced:

### 2a — Fixed-width non-null columns issue redundant `bcp_colptr` calls

For columns like INT64, INT32, DOUBLE, UINT8 backed by contiguous Arrow arrays with **zero nulls**, the code still calls `bcp_colptr` per row to update the pointer:

```cpp
const uint8_t *ptr = static_cast<const uint8_t *>(binding.data_ptr)
                   + (static_cast<size_t>(row_idx) * binding.value_stride);
ret = bcp_colptr(m_dbc, (LPCBYTE)ptr, binding.ordinal);
```

Since the data is contiguous and the stride is constant, the pointer is just `base + row * stride`. This pattern repeats 8M times for the current schema.

**Remediation**: For non-null, fixed-width columns: compute the next pointer outside the BCP call by maintaining a running `const uint8_t*` cursor per column, advanced by `value_stride` each row. More importantly, explore whether a single `bcp_bind` with proper offset handling can avoid `bcp_colptr` entirely for fixed-width data — the BCP API binds to a *program variable address*; if a secondary buffer were used with the row data copied in fixed layout, one `bcp_bind` per column suffices and only `bcp_sendrow` is needed per row. This trades `N×cols` function calls for `N×cols` memcpy-like copies which are far cheaper.

### 2b — `is_null_at` called unconditionally even with all NOT NULL columns

```cpp
if (is_null_at(binding, row_idx)) {
```

The stress table is entirely NOT NULL. Arrow arrays from the Polars generator have `null_count() == 0`, so `has_nulls` is false. But the branch is still evaluated 11M times. The compiler cannot eliminate it because `bindings` is runtime data.

**Remediation**: Split the row loop into two template paths: one for "no column has nulls" (common in bulk-load scenarios) and one for the general case. When no column has nulls, all null checks are dead code.

### 2c — Column dispatch is branch-heavy inside the hot loop

```cpp
for (auto &binding : bindings) {
    if (binding.utf8_cache) {        // branch 1
        ...
    } else if (binding.value_stride > 0) {  // branch 2
        ...
    }
}
```

Every column × every row hits these branches. With 11 columns in mixed order (8 fixed, 3 string), the branch predictor must track per-iteration state across the column vector.

**Remediation**: Separate columns into two flat arrays: `fixed_bindings[]` and `string_bindings[]`. Iterate each with a tight, branch-free inner loop. This transforms unpredictable per-column branches into predictable loop-exit branches.

---

## Finding 3 — String columns materialize 1M `std::string` objects each

**Severity: HIGH (contributes to bind_columns 0.138s + row_loop cache pressure)**

```cpp
case arrow::Type::STRING: {
    auto text_values = std::make_shared<std::vector<std::string>>(typed->length());
    for (int64_t i = 0; i < typed->length(); ++i) {
        (*text_values)[static_cast<size_t>(i)] = typed->GetString(i);
    }
```

For 3 string columns × 1M rows = **3M `std::string` heap allocations** during `bind_columns`. Each `GetString(i)` returns a temporary `std::string`, which is then copy-assigned into the vector. The data already exists as contiguous UTF-8 in the Arrow buffer — the copies are entirely redundant.

**Remediation**: Instead of caching into `std::vector<std::string>`, store the Arrow offsets and data pointer in the `ColumnBinding` and resolve `(ptr, len)` pairs directly in the row loop. Arrow's `StringArray::GetView(i)` returns a `std::string_view` without allocation. The row loop can call `bcp_collen(len)` + `bcp_colptr(ptr)` directly from the view. This eliminates 3M allocations and ~100 MB of transient heap traffic.

---

## Finding 4 — Temporal columns use chrono calendar arithmetic per element

**Severity: MEDIUM (2M conversions, ~3–5% of bind_columns)**

```cpp
auto days_to_sql_date = [](int32_t days_since_epoch) {
    sys_days day_point = epoch + days{days_since_epoch};
    year_month_day ymd(day_point);  // ← involves modular division
    ...
};
```

`year_month_day` construction from `sys_days` involves integer division to extract year/month/day. For 1M date + 1M timestamp values, this is 2M inline divisions.

**Remediation**: For `DATE32`, the input is days-since-epoch. Convert using Euclidean/civil-from-days algorithm (Howard Hinnant's formulation — a few integer multiplies, no division). For timestamps, similarly decompose microseconds into day + time-of-day using shifts and masks. Both approaches are branchless and ~3–5× faster than chrono calendar types.

---

## Finding 5 — `ColumnBinding` struct has unused fields and uses `shared_ptr` unnecessarily

**Severity: LOW-MEDIUM (cache line pollution, ref-counting overhead)**

```cpp
struct ColumnBinding {
    int ordinal;
    arrow::Type::type arrow_type;
    std::shared_ptr<arrow::Array> array;          // ref-count bump
    ...
    std::shared_ptr<std::vector<std::u16string>> utf16_cache;  // always nullptr in current paths
    std::shared_ptr<std::vector<std::string>> utf8_cache;      // 3 string cols active
    ...
    std::shared_ptr<std::vector<SQL_DATE_STRUCT>> date_buffer;
    std::shared_ptr<std::vector<SQL_TIMESTAMP_STRUCT>> timestamp_buffer;
};
```

- `utf16_cache` is never populated by any code path — dead weight.
- `offsets32`, `offsets64`, `str_data`, `max_length` are also unused in current paths.
- Each `shared_ptr` adds 16 bytes (pointer + control block pointer) and a ref-count bump. The struct is 160+ bytes.
- With 11 columns, `bindings` vector = ~1.8 KB — fits in L1, but bloated fields reduce effective density.

**Remediation**: Remove unused fields. Replace `shared_ptr<vector<T>>` with `unique_ptr<vector<T>>` (no sharing needed) or raw owning pointer. Better yet, if Finding 3 is implemented (zero-copy string views), the `utf8_cache` field disappears entirely.

---

## Finding 6 — Error checking after every BCP call in hot path

**Severity: LOW-MEDIUM (15M+ branch evaluations)**

```cpp
ret = bcp_colptr(m_dbc, (LPCBYTE)ptr, binding.ordinal);
if (ret != SUCCEED) {
    raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_colptr");
}
```

This pattern repeats for `bcp_collen`, `bcp_colptr`, and `bcp_sendrow` in the inner loop. While each check is a single comparison, across 15M calls this amounts to significant branch prediction pressure and prevents instruction-level parallelism.

**Remediation**: BCP calls that consistently succeed can be checked in batches — accumulate return codes and check at batch boundaries (e.g., every 1000 rows or after each `bcp_batch`). Alternatively, check only `bcp_sendrow` (which is the actual commit point) and remove per-`collen`/`colptr` checks. The BCP API does not fail on individual pointer updates in practice.

---

## Finding 7 — Column binding is repeated for every RecordBatch

**Severity: CONTEXT-DEPENDENT (significant for multi-batch c-stream)**

```cpp
auto process_record_batch = [&](const std::shared_ptr<arrow::RecordBatch> &record_batch) {
    ...
    std::vector<ColumnBinding> bindings;
    ...
    for (int col_idx = 0; col_idx < num_cols; ++col_idx) {
        ...
        bcp_bind(m_dbc, ...);
    }
    ...
};
```

In the c-stream path, `process_record_batch` is called once per RecordBatch from the stream reader. Each invocation re-binds all columns. For a 1M-row single-batch payload, this is called once (negligible). But if the stream emits many small batches (e.g., 65536-row chunks from Polars default), this becomes 15+ rebind cycles, each materializing new string/temporal buffers.

**Remediation**: For multi-batch streams with identical schemas, bind once and re-point data buffers in subsequent batches — only `bcp_colptr` updates are needed if the column layout is unchanged.

---

## Finding 8 — `batch_flush` cost (41.9%) may be inflated by server-side minimal logging gap

**Severity: HIGH (explains ~1.86s of total 4.44s)**

`batch_flush` is pure server-side work — `bcp_batch(m_dbc)` commits buffered rows to the table. At 50000-row batches with 1M rows, there are 20 flush calls. The 1.86s total means ~93ms per flush, which is high for 50K rows if minimal logging were active.

Contributing factors:
- **Missing TABLOCK hint** (Finding 1): Server uses fully-logged inserts.
- **Table has a clustered PK on `id`**: Inserts must maintain B-tree order. If IDs are not monotonically increasing or if there's page contention, row movement and page splits add overhead.
- **No explicit recovery model control**: If the database is in FULL recovery, every row is logged.

**Remediation**: In priority order:
1. Add TABLOCK hint via `bcp_control` (Finding 1).
2. Consider `bcp_control(BCPBATCH, batch_size)` to let BCP manage batching natively.
3. Document the recovery model + index impact in the perf guide.

---

## Finding 9 — `col_int` is Int64 in Arrow but INT (32-bit) in SQL Server

**Severity: LOW (correctness risk + implicit conversion)**

The Polars generator produces `id` and `col_int` as `pl.Int64`, but the SQL table defines them as `INT` (32-bit). The BCP binding uses `SQLBIGINT` for Int64 columns. SQL Server accepts this via implicit narrowing, but:
- It forces the server to do per-row type coercion.
- It sends 8 bytes per value instead of 4.

**Remediation**: Cast these columns to `pl.Int32` in the Polars dataframe before export, or handle the Arrow→SQL type narrowing in the bind phase.

---

## Summary: Prioritized Action Items

| Priority | Finding | Est. Impact | Effort |
|----------|---------|-------------|--------|
| **P0** | F1: Enable TABLOCK via `bcp_control` | 20–40% throughput improvement | Low |
| **P0** | F3: Zero-copy string binding from Arrow views | 10–20% (removes 3M allocs) | Medium |
| **P1** | F2a: Eliminate redundant `bcp_colptr` for fixed columns | 5–15% | Medium |
| **P1** | F2c: Split columns by type for branch-free iteration | 5–10% | Medium |
| **P1** | F8: Server-side flush optimization (TABLOCK + batch tuning) | 20–40% (server-dependent) | Low |
| **P2** | F2b: Template-split null vs non-null row loop | 3–5% | Low |
| **P2** | F4: Fast integer-arithmetic temporal conversion | 2–4% | Low |
| **P2** | F6: Batch error checking instead of per-call | 1–3% | Low |
| **P3** | F5: Slim down ColumnBinding struct | <1% | Trivial |
| **P3** | F9: Type-narrow Int64→Int32 at bind time | <1% | Trivial |

### Estimated ceiling if all P0+P1 are addressed

Conservatively: **40–60 MB/s** (1.7–2.6× current), which approaches the 63.63 MB/s reference. The TABLOCK fix alone may recover a significant fraction of the gap, since minimal logging fundamentally changes the server write path.
