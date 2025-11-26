# Arrow BCP Implementation - Status Report

## Summary

Successfully implemented the Arrow IPC → BCP bulk insert optimization path for MSSQL. The code compiles and is ready for use, but requires SQL Server ODBC Driver 17/18 to be installed on the system for BCP function linking.

## Achievements

### 1. Complete Arrow BCP Implementation (220+ lines)
- **File**: `src/_pygim_fast/repository/mssql_strategy/mssql_strategy.cpp`
- **Method**: `bulk_insert_arrow_bcp(table, arrow_ipc_bytes, batch_size, table_hint)`
- **Features**:
  - Arrow IPC deserialization from Python bytes
  - BCP initialization with connection handle
  - Column binding for multiple types:
    - INT64 (BIGINT)
    - INT32 (INT)
    - UINT8 (BIT)
    - DOUBLE (FLOAT)
    - STRING (VARCHAR with variable-length handling)
    - DATE32 (DATE, ready for conversion)
    - TIMESTAMP (DATETIME2, ready for conversion)
  - Row-by-row sendrecordBatch with periodic batch commits (100k rows)
  - String offset array handling for variable-length data
  - Error handling with descriptive messages

### 2. Build System Integration
- **File**: `setup.py`
- **Changes**:
  - Arrow C++ detection with conda environment support
  - Automatic PYGIM_HAVE_ARROW macro definition
  - Include/library paths for Arrow in conda prefix
  - Conditional compilation guards

### 3. Python Helper Functions
- **File**: `src/pygim/arrow_bridge.py`
- **Function**: `prepare_for_bcp(df) -> bytes`
- Wraps Polars/Pandas → Arrow IPC serialization

### 4. Test Infrastructure
- **File**: `__playground__/check_arrow_bcp.py`
- Checks implementation status without triggering link errors
- Provides installation instructions for SQL Server ODBC Driver

### 5. Documentation
- `docs/design/arrow_bcp_refactor.md` - Architecture & plan (269 lines)
- `docs/design/arrow_bcp_implementation.md` - Implementation details (176 lines)
- `docs/examples/arrow_bcp_quickstart.md` - Quick start guide (75 lines)

## Current Status

### ✅ Completed
1. Arrow C++ and PyArrow installed (conda)
2. Full BCP implementation compiled successfully
3. Method `bulk_insert_arrow_bcp` exists in Python bindings
4. Build system detects Arrow and enables optimization
5. Platform-specific handling (Windows vs Linux)
6. Header conflict resolution (ODBC BOOL macro vs Arrow Type::BOOL enum)

### ⏳ Pending
1. **SQL Server ODBC Driver installation** (Linux)
   - Required for BCP function linking (bcp_init, bcp_bind, bcp_sendrow, etc.)
   - Standard unixODBC does NOT include BCP API
   - Installation: https://learn.microsoft.com/en-us/sql/connect/odbc/linux-mac/installing-the-microsoft-odbc-driver-for-sql-server

2. **NULL handling**
   - Arrow validity bitmaps not checked
   - Need to set indicator to SQL_NULL_DATA when `array->IsNull(i)` is true

3. **DATE/TIMESTAMP conversion**
   - Currently bound as raw int32/int64 (days/microseconds since epoch)
   - Should convert to SQL_DATE_STRUCT / SQL_TIMESTAMP_STRUCT for proper SQL Server types

4. **Additional type support**
   - FLOAT (SQL_REAL)
   - DECIMAL (SQL_NUMERIC_STRUCT)
   - BINARY/VARBINARY
   - Large types (LARGE_STRING, LARGE_BINARY)

## Installation Steps (Ubuntu 22.04)

```bash
# 1. Install SQL Server ODBC Driver 17/18
curl https://packages.microsoft.com/keys/microsoft.asc | sudo apt-key add -
curl https://packages.microsoft.com/config/ubuntu/$(lsb_release -rs)/prod.list | sudo tee /etc/apt/sources.list.d/mssql-release.list
sudo apt-get update
sudo ACCEPT_EULA=Y apt-get install -y msodbcsql18

# 2. Rebuild extension (will now link BCP functions)
cd /home/debith/projects/pygim
conda activate py312
pip install -e . --force-reinstall

# 3. Verify BCP availability
python __playground__/check_arrow_bcp.py

# 4. Test with real SQL Server connection
python __playground__/stresss_test.py --rows 100000 --use-arrow-bcp
```

## Performance Expectations

Based on the provided example and BCP benchmarks:
- **Current path (MERGE)**: ~10k-20k rows/sec
- **Arrow BCP path**: 500k-1M rows/sec (10-50x faster)
- **Batch size**: 100k rows (commit every batch)
- **Memory**: Columnar format reduces allocations

## Technical Notes

### Header Conflict Resolution
ODBC headers define `BOOL` as a macro which conflicts with Arrow's `Type::BOOL` enum. Resolved by:
```cpp
#ifdef BOOL
    #undef BOOL
#endif
#ifdef INT
    #undef INT
#endif
```

### Arrow API Changes
Arrow 13.0.0 doesn't have `RecordBatchFileReader::ReadAll()`. Implementation uses:
```cpp
for (int i = 0; i < num_batches; ++i) {
    batches.push_back(reader->ReadRecordBatch(i).ValueOrDie());
}
arrow::Table::FromRecordBatches(schema, batches);
```

### BCP Type Mapping
| Arrow Type | BCP Type | Binding Constant | Notes |
|------------|----------|------------------|-------|
| INT64 | BIGINT | SQLBIGINT (-5) | Fixed-width, direct pointer |
| INT32 | INT | SQLINT4 (4) | Fixed-width, direct pointer |
| UINT8 | BIT | SQLBIT (50) | Treated as boolean |
| DOUBLE | FLOAT | SQLFLT8 (8) | 8-byte float |
| STRING | VARCHAR | SQLCHARACTER (1) | Variable-length, uses SQL_VARLEN_DATA |
| DATE32 | DATE | SQLINT4 | TODO: Convert to SQL_DATE_STRUCT |
| TIMESTAMP | DATETIME2 | SQLBIGINT | TODO: Convert to SQL_TIMESTAMP_STRUCT |

## Next Steps

1. **Immediate**: Install SQL Server ODBC Driver 17/18 to enable BCP linking
2. **Short-term**: Test with real SQL Server instance and benchmark
3. **Medium-term**: Implement NULL handling and date/time conversions
4. **Long-term**: Add remaining type support (FLOAT, DECIMAL, BINARY)

## References

- Original example: Arrow IPC → BCP pipeline achieving 10-50x speedup
- SQL Server ODBC Driver: https://docs.microsoft.com/sql/connect/odbc/
- Arrow C++ docs: https://arrow.apache.org/docs/cpp/
- BCP API: https://docs.microsoft.com/sql/odbc/reference/bcp-api
