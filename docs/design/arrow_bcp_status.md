# Arrow BCP Status

## Status

Implemented and active behind the native repository/strategy path.

## Runtime behavior

- `repo.persist_dataframe(..., prefer_arrow=True)` prefers Arrow C stream ingestion.
- Falls back to IPC serialization when stream ingestion is unavailable.
- Falls back to `bulk_upsert` if Arrow path cannot be used.

## Operational requirement

Arrow/BCP acceleration still depends on environment support (Arrow C++ + ODBC/BCP availability).
When unavailable, behavior degrades to supported fallback paths.
