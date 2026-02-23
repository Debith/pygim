# Arrow BCP Final Status

## Final runtime architecture

DataFrame persistence orchestration is native-first.

- Public entry: `Repository.persist_dataframe(...)`
- Native strategy prefers Arrow C stream (`__arrow_c_stream__`)
- Native fallback uses IPC serialization (`write_ipc`)
- Final fallback is `bulk_upsert`

## Practical impact

- No standalone Python Arrow helper module in runtime ingestion path.
- Lower Python-side orchestration overhead.
- Batch-wise BCP processing avoids full-table materialization.

## Verification baseline

- Editable build succeeds.
- Core unit suites (`test_registry.py`, `test_factory.py`) remain green.
