# Arrow BCP Implementation Summary

## Current Implementation

`Repository.persist_dataframe(...)` is now the single public entry point for DataFrame persistence.
The native strategy handles conversion and write orchestration internally.

### Native write path priority
1. Arrow C Data Interface (`__arrow_c_stream__`) → `bulk_insert_arrow_bcp`
2. IPC serialization fallback (`write_ipc`) → `bulk_insert_arrow_bcp`
3. Non-arrow fallback: `bulk_upsert`

## Key Outcomes

- Python helper orchestration for Arrow payload preparation has been removed from runtime hot paths.
- BCP ingestion processes Arrow data batch-by-batch to avoid full-table materialization.
- Multi-batch Arrow input is handled by streaming `RecordBatch` data.

## Files of Interest

- `src/_pygim_fast/repository/mssql_strategy/mssql_strategy.cpp`
- `src/_pygim_fast/repository/mssql_strategy/detail/mssql_strategy_bcp.cpp`
- `src/_pygim_fast/repository.cpp`
- `__playground__/stresss_test.py`

## Notes

This document intentionally tracks only the current architecture and avoids historical helper-module flow to prevent drift.
