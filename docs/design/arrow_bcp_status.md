# Arrow BCP Status

## Status

Implemented and active behind the native repository/strategy path.

## Runtime behavior

- `repo.persist_dataframe(..., prefer_arrow=True)` prefers Arrow C stream ingestion.
- Falls back to IPC serialization when stream ingestion is unavailable.
- Falls back to `bulk_upsert` if Arrow path cannot be used.

## Build requirement

Arrow C++ (libarrow, libparquet) and ODBC (unixODBC) are mandatory build dependencies.
The build fails at compile time if they are missing — there is no graceful degradation.
