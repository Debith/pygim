# Quick Start: Native Arrow BCP Bulk Insert

## TL;DR

```python
from pygim import repository
from gen_data import generate_polars_dataset

conn_str = "Driver={ODBC Driver 18 for SQL Server};Server=localhost;..."
repo = repository.acquire_repository(conn_str, transformers=False)

df = generate_polars_dataset(n=1_000_000)
stats = repo.persist_dataframe(
	"my_table",
	df,
	key_column="id",
	prefer_arrow=True,
	table_hint="TABLOCK",
	batch_size=100000,
)

print(stats["mode"])  # arrow_c_stream_bcp | arrow_ipc_bcp | bulk_upsert
```

## Performance Comparison

```bash
# Run the stress harness with native Arrow path enabled (default)
python __playground__/stresss_test.py --rows 1000000 --arrow

# Force non-arrow fallback path
python __playground__/stresss_test.py --rows 1000000 --no-arrow
```

## Requirements

- Arrow C++ library installed (`libarrow-dev` on Ubuntu)
- Polars Python package (`pip install polars`)
- SQL Server with table already created

## Notes

- The native path first attempts Arrow C Data Interface (`__arrow_c_stream__`).
- If stream import is unavailable, native code falls back to IPC serialization.
- Use `--arrow` in the stress harness to set `PYGIM_ENABLE_ARROW_BCP=1` explicitly.
- If Arrow persist fails, repository falls back to `bulk_upsert`.
