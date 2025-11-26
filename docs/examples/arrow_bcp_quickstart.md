# Quick Start: Arrow BCP Bulk Insert

## TL;DR

```python
from pygim.arrow_bridge import prepare_for_bcp
from pygim import mssql_strategy
from gen_data import generate_polars_dataset

# Generate data
df = generate_polars_dataset(n=1_000_000)

# Convert to Arrow IPC
arrow_bytes = prepare_for_bcp(df)

# Connect and insert via BCP
conn_str = "Driver={ODBC Driver 18 for SQL Server};Server=localhost;..."
strategy = mssql_strategy.MssqlStrategyNative(conn_str)
strategy.bulk_insert_arrow_bcp("my_table", arrow_bytes, batch_size=100000)
```

## Performance Comparison

```bash
# Baseline: MERGE path
time python stresss_test.py --rows 1000000 --use-merge

# Optimized: Arrow BCP path
time python stresss_test.py --rows 1000000 --use-arrow-bcp
```

Expected speedup: **10-50x faster** for large datasets

## Requirements

- Arrow C++ library installed (`libarrow-dev` on Ubuntu)
- PyArrow Python package (`pip install pyarrow`)
- Polars Python package (`pip install polars`)
- SQL Server with table already created

## Supported Types (Current)

| Python/Polars | Arrow | SQL Server | Status |
|---------------|-------|------------|--------|
| Int64 | INT64 | BIGINT | ✅ |
| Int32 | INT32 | INT | ✅ |
| Boolean | UINT8 | BIT | ✅ |
| Float64 | DOUBLE | FLOAT | ✅ |
| Utf8 | STRING | VARCHAR/NVARCHAR | ✅ |
| Date | DATE32 | DATE | ⚠️ Partial |
| Datetime | TIMESTAMP | DATETIME2 | ⚠️ Partial |

## Troubleshooting

**"Arrow C++ library not detected"**
- Install: `sudo apt-get install libarrow-dev libparquet-dev`
- Rebuild: `pip install -e . --force-reinstall --no-cache-dir`

**"bcp_bind failed"**
- Check table exists: `SELECT * FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME='...'`
- Verify column order matches DataFrame columns

**"bcp_sendrow failed at row X"**
- Check for type mismatches (e.g., string too long for VARCHAR(N))
- Review SQL Server error log for details

## Next Steps

See `docs/design/arrow_bcp_implementation.md` for full details on:
- Type conversion TODOs
- Performance tuning
- NULL handling
- Multi-chunk support
