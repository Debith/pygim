# Arrow IPC → BCP Bulk Insert Refactoring Plan

## Overview
Refactor `mssql_strategy` to use the Arrow IPC → BCP pipeline for maximum performance when inserting large datasets from Polars DataFrames into SQL Server.

## Architecture

### Current State
- `bulk_insert`: Uses parameterized INSERT VALUES statements (batched)
- `bulk_upsert`: Uses MERGE statements with parameterized VALUES
- Polars path: Converts to NumPy arrays, binds per-row via SQLBindParameter

### Target State
- Add `bulk_insert_arrow_bcp`: Accept Arrow IPC bytes/file, use BCP API
- Zero-copy path: Polars → Arrow IPC → C++ memory-mapped read → BCP bulk copy
- Direct column buffer binding (fixed-width) + offset-based varlen binding

## Implementation Phases

### Phase 1: Build Infrastructure ✓
- [x] Detect Arrow C++ library availability in setup.py
- [ ] Add Arrow C++ headers + link flags (`-larrow -lparquet`)
- [ ] Add feature macro `PYGIM_HAVE_ARROW` similar to `PYGIM_HAVE_ODBC`

### Phase 2: Arrow IPC Reader
```cpp
// New method signature
void bulk_insert_arrow_bcp(
    const std::string& table,
    const py::bytes& arrow_ipc_bytes,  // or accept file path
    int batch_size = 100000,
    const std::string& table_hint = "TABLOCK"
);
```

**Steps:**
1. Deserialize Arrow IPC from `py::bytes` (or memory-map from file)
2. Extract `arrow::Table` with schema + column chunks
3. Validate schema matches target table (future: auto-detect from INFORMATION_SCHEMA)

### Phase 3: BCP Setup
```cpp
// Enable BCP mode on connection
SQLSetConnectAttr(m_dbc, SQL_COPT_SS_BCP, (SQLPOINTER)SQL_BCP_ON, SQL_IS_INTEGER);

// Initialize BCP for target table
bcp_initW(m_dbc, L"dbo.PlayData", nullptr, nullptr, DB_IN);
```

### Phase 4: Column Binding Strategy

#### Fixed-Width Columns (Fast Path)
```cpp
// Example: INT64 (id, col_bigint)
auto id_arr = static_pointer_cast<arrow::Int64Array>(table->GetColumnByName("id")->chunk(0));
const int64_t* id_data = id_arr->raw_values();
bcp_bind(m_dbc, (LPCBYTE)id_data, 0, sizeof(int64_t), nullptr, 0, SQLBIGINT, /*col_ordinal*/1);
```

**Type Mappings:**
- `arrow::Int64` → `SQL_C_SBIGINT` / `SQLBIGINT`
- `arrow::Int32` → `SQL_C_SLONG` / `SQLINT4`
- `arrow::UInt8` (bit) → `SQL_C_UTINYINT` / `SQLBIT`
- `arrow::Double` → `SQL_C_DOUBLE` / `SQLFLT8`

#### Variable-Length Columns (String/UUID)
```cpp
auto str_arr = static_pointer_cast<arrow::StringArray>(table->GetColumnByName("col_nvarchar")->chunk(0));
const int32_t* offsets = str_arr->raw_value_offsets();
const uint8_t* data = str_arr->value_data()->data();

// Per-row in sendrow loop:
DBINT len = offsets[i+1] - offsets[i];
bcp_collen(m_dbc, len, col_ordinal);
bcp_colptr(m_dbc, (LPCBYTE)(data + offsets[i]), col_ordinal);
```

#### Date/Timestamp Conversion
```cpp
// Arrow Date32 (days since epoch) → SQL_DATE_STRUCT
auto date_arr = static_pointer_cast<arrow::Date32Array>(table->GetColumnByName("col_date")->chunk(0));
std::vector<SQL_DATE_STRUCT> date_buf(num_rows);
for (int64_t i = 0; i < num_rows; ++i) {
    int32_t days = date_arr->Value(i);
    // Convert days since 1970-01-01 to SQL_DATE_STRUCT
    date_buf[i] = days_to_sql_date(days);
}
bcp_bind(m_dbc, (LPCBYTE)date_buf.data(), 0, sizeof(SQL_DATE_STRUCT), nullptr, 0, SQL_TYPE_DATE, col_ord);
```

```cpp
// Arrow Timestamp(us) → SQL_TIMESTAMP_STRUCT
auto ts_arr = static_pointer_cast<arrow::TimestampArray>(table->GetColumnByName("col_datetime2")->chunk(0));
std::vector<SQL_TIMESTAMP_STRUCT> ts_buf(num_rows);
for (int64_t i = 0; i < num_rows; ++i) {
    int64_t us = ts_arr->Value(i);
    ts_buf[i] = micros_to_sql_timestamp(us);
}
bcp_bind(m_dbc, (LPCBYTE)ts_buf.data(), 0, sizeof(SQL_TIMESTAMP_STRUCT), nullptr, 0, SQL_TYPE_TIMESTAMP, col_ord);
```

### Phase 5: BCP Send Loop
```cpp
const DBINT batch = 100000;
for (DBINT i = 0, N = (DBINT)table->num_rows(); i < N; ++i) {
    // Update varlen pointers for this row
    for (auto& [col_ord, str_arr] : varlen_columns) {
        DBINT len = offsets[i+1] - offsets[i];
        bcp_collen(m_dbc, len, col_ord);
        bcp_colptr(m_dbc, (LPCBYTE)(data + offsets[i]), col_ord);
    }

    if (bcp_sendrow(m_dbc) != SUCCEED) {
        throw std::runtime_error("bcp_sendrow failed");
    }

    if (i && (i % batch == 0)) {
        bcp_batch(m_dbc);
    }
}
bcp_done(m_dbc);
```

### Phase 6: Python Integration
```python
# pygim/arrow_bridge.py
def write_arrow_for_bcp(df: pl.DataFrame, path: str | None = None) -> bytes:
    """Convert Polars DataFrame to Arrow IPC format for BCP bulk insert."""
    if path:
        df.write_ipc(path)
        return b""
    else:
        from io import BytesIO
        buf = BytesIO()
        df.write_ipc(buf)
        return buf.getvalue()

# Usage in stress test
from pygim.arrow_bridge import write_arrow_for_bcp
df = generate_polars_dataset(n=1_000_000)
arrow_bytes = write_arrow_for_bcp(df)
strategy.bulk_insert_arrow_bcp("stress_data", arrow_bytes)
```

## Type Conversion Reference

| Arrow Type | Polars Type | SQL Server Type | C Type | ODBC Type |
|------------|-------------|-----------------|--------|-----------|
| Int64 | Int64 | BIGINT | int64_t | SQLBIGINT |
| Int32 | Int32 | INT | int32_t | SQLINT4 |
| UInt8 | Boolean | BIT | uint8_t | SQLBIT |
| Double | Float64 | FLOAT(53) | double | SQLFLT8 |
| Utf8 | Utf8 | VARCHAR/NVARCHAR | char* + len | SQLVARCHAR |
| Date32 | Date | DATE | SQL_DATE_STRUCT | SQL_TYPE_DATE |
| Timestamp(us) | Datetime(us) | DATETIME2(6) | SQL_TIMESTAMP_STRUCT | SQL_TYPE_TIMESTAMP |
| Utf8 (UUID) | Utf8 | CHAR(36) | char[36] | SQLCHAR |

## Conversion Helpers

```cpp
SQL_DATE_STRUCT days_to_sql_date(int32_t days_since_epoch) {
    // Days since 1970-01-01
    // Simple calculation: year/month/day from Julian day
    // (Implementation omitted for brevity - use <chrono> or manual calc)
    SQL_DATE_STRUCT d;
    // ... populate d.year, d.month, d.day
    return d;
}

SQL_TIMESTAMP_STRUCT micros_to_sql_timestamp(int64_t us_since_epoch) {
    SQL_TIMESTAMP_STRUCT ts;
    // Split into date + time components
    // ... populate ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second, ts.fraction (nanos)
    return ts;
}
```

## Performance Expectations

| Method | Throughput (rows/sec) | Notes |
|--------|----------------------|-------|
| Current MERGE (upsert) | ~10-20k | Parameterized SQL, network round-trips per batch |
| Current INSERT (bulk) | ~50-100k | Batched VALUES, still parameterized |
| BCP (proposed) | ~500k-1M+ | Zero-copy columnar, native TDS bulk protocol |

## Testing Strategy

1. **Unit test**: Small Arrow table (10 rows) → BCP → verify SQL Server contents
2. **Integration test**: 100k rows stress test comparing:
   - Old path: `bulk_insert(df)`
   - New path: `bulk_insert_arrow_bcp(arrow_ipc_bytes)`
3. **Benchmark**: 1M rows → measure time + memory

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Arrow library not available | Feature-gate behind `PYGIM_HAVE_ARROW`, fallback to current path |
| BCP API differences (Windows vs Linux) | Use unixODBC + SQL Server ODBC 18 driver (same BCP funcs) |
| Date/timestamp conversion bugs | Add comprehensive unit tests with edge cases |
| Memory overhead for temp structs | Allocate per-batch, reuse buffers |

## Next Steps

1. ✅ Document plan (this file)
2. ⏳ Update setup.py to detect Arrow C++
3. ⏳ Implement Arrow IPC reader stub
4. ⏳ Implement BCP initialization + fixed-width binding
5. ⏳ Implement varlen binding
6. ⏳ Implement date/timestamp conversion
7. ⏳ Add pybind11 binding for new method
8. ⏳ Update stress test to use new path
9. ⏳ Benchmark and validate

---

**Target Completion**: Implement Phase 1-3 (basic BCP path) first, then iterate on types.
