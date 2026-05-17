# Repository Module — Architecture

Current as of April 2026 (`core/repository` branch). Single source of truth for the
repository module's design. Replaces all prior architecture documents
(archived under `__archive__/repository/design/`).

**Diagrams** (PlantUML, same directory):

- `persistence_abstract_architecture.puml` — Layer architecture overview
- `persistence_class_diagram.puml` — Detailed class diagram
- `persistence_save_sequence.puml` — Save path sequence (Python → BCP)
- `persistence_load_sequence.puml` — Load path sequence (Python → ODBC → Arrow)

---

## 1. Directory Layout

```text
src/_pygim_fast/persistence/
├── core/                           ← Backend-agnostic concepts + generic logic
│   ├── arrow_builder.h             ← Columnar builder: ODBC block buffers → Arrow Table
│   ├── backend_policy.h            ← BackendPolicy concept (C++20) — includes LoadCache type
│   ├── connection_pool.h           ← Thread-safe ConnectionPool + RAII ConnectionHandle
│   ├── dialect.h                   ← DialectPolicy concept + build_sql()
│   ├── load_result.h               ← LoadMetrics + LoadResult (table + timing)
│   ├── null_load_cache.h           ← NullLoadCache (zero-cost for non-MSSQL backends)
│   ├── query.h                     ← Fluent Query builder (intent-only)
│   └── persistence.h                ← Repository<Backend> facade
├── strategy/mssql/                 ← SQL Server concrete backend
│   ├── backend.h                   ← OdbcConnection + MssqlBackend trait struct
│   ├── dialect.h                   ← MssqlDialect ([bracket]-quoting, TOP N)
│   ├── fetch_buffer.h              ← FetchBuffer + FetchBufferSet (block cursor storage)
│   ├── load_cache.h                ← MssqlLoadCache (persistent LoadConnectionPool cache)
│   ├── load_connection_pool.h      ← LoadConnectionPool (parallel worker connections)
│   ├── load_dispatch.h             ← O(1) dispatch table (arrow type → block append fn)
│   ├── load_impl.h                 ← MssqlLoadImpl (block cursor + parallel dispatch)
│   ├── odbc_compat.h               ← SQL_SS_TIME2 platform compatibility
│   ├── odbc_error.h                ← ODBC diagnostic collection + raise_if_error()
│   ├── parallel_load.h             ← execute_parallel (range-partitioned multi-worker load)
│   ├── pk_detect.h                 ← Auto-detect integer PK for partition column
│   ├── schema_describe.h           ← describe_columns(): SchemaInfo from ODBC metadata
│   ├── sql_helpers.h               ← Table identifier validation + qualification
│   ├── sql_type_map.h              ← ODBC SQL type → Arrow type mapping (TypeMapping)
│   ├── stmt_handle.h               ← StmtHandle RAII wrapper (SQLHSTMT)
│   ├── save_impl.h                 ← MssqlSaveImpl (delegates to BCP pipeline)
│   └── bcp/                        ← BCP bulk insert pipeline
│       ├── bcp_api.h               ← Thread-safe BCP function pointer loading (dlopen)
│       ├── bcp_bind.h              ← Column binding orchestration
│       ├── bcp_bind_dispatch.h     ← Compile-time bind dispatch table
│       ├── bcp_helpers.h           ← Row-level BCP operations (string/flush/null)
│       ├── bcp_pipeline.h          ← Single + parallel BCP orchestrator + BcpMetrics
│       ├── bcp_pool.h              ← BcpConnectionPool (pre-connected parallel pool)
│       ├── bcp_profiler.h          ← Optional per-section profiling (PYGIM_BCP_PROFILING)
│       ├── bcp_rebind_dispatch.h   ← Fast rebind for multi-batch streaming
│       ├── bcp_types.h             ← ColumnBinding, BcpContext, ClassifiedColumns
│       └── fetch_benchmark.h       ← Low-level SQLFetch throughput microbench
├── adapter/                        ← Python bindings (pybind11 edge)
│   ├── adapter.h                   ← RepositoryAdapter<Backend> (owns Repository directly)
│   ├── arrow_export.h              ← shared_ptr<arrow::Table> → Python DataFrame
│   ├── arrow_import.h              ← py::object → shared_ptr<arrow::Table> (PyCapsule)
│   ├── bindings.cpp                ← Production: DataStore + Format + acquire_datastore()
│   └── test_bindings.cpp           ← Test-only: Query, MssqlDialect, DataStore (module_local)

src/pygim/
└── persistence.py                   ← Public Python API: re-exports acquire_datastore, DataStore, Format
```

### Build Configuration

| TOML file                  | Module name         | Sources                                  | Purpose       |
|----------------------------|---------------------|------------------------------------------|---------------|
| `ext.persistence.toml`      | `_persistence`      | `persistence/adapter/bindings.cpp`        | Production    |
| `ext.persistence_test.toml` | `_persistence_test`  | `persistence/adapter/test_bindings.cpp`   | Test-only     |

---

## 2. Namespace Mapping

| Directory              | C++ Namespace                   | Responsibility                                        |
|------------------------|---------------------------------|-------------------------------------------------------|
| `core/`                | `pygim::core`                   | Concepts, generic templates, Query, ArrowBuilder      |
| `strategy/mssql/`      | `pygim::strategy::mssql`        | MSSQL-specific types, dialect, load/save pipeline     |
| `strategy/mssql/bcp/`  | `pygim::strategy::mssql::bcp`   | BCP bulk insert pipeline (bind, row loop, pool)       |
| `strategy/mssql/`      | `pygim::strategy::mssql::odbc`  | ODBC diagnostics (shared helper namespace)            |
| `strategy/mssql/`      | `pygim::strategy::mssql::sql`   | Table identifier validation + qualification           |
| `adapter/`             | `pygim::adapter`                | Format enum, RepositoryAdapter, Arrow import/export   |

---

## 3. Include Dependency Graph

```
bindings.cpp
  ├── core/query.h
  ├── strategy/mssql/save_impl.h
  │     ├── strategy/mssql/backend.h
  │     │     ├── core/backend_policy.h
  │     │     │     └── core/dialect.h → core/query.h
  │     │     ├── strategy/mssql/dialect.h → core/query.h
  │     │     └── strategy/mssql/odbc_error.h
  │     └── strategy/mssql/bcp/bcp_pipeline.h
  │           ├── bcp/bcp_helpers.h → bcp/bcp_types.h → bcp/bcp_api.h, <arrow/table.h>
  │           ├── bcp/bcp_bind.h → bcp/bcp_bind_dispatch.h
  │           ├── bcp/bcp_rebind_dispatch.h
  │           ├── bcp/bcp_pool.h → backend.h
  │           └── strategy/mssql/sql_helpers.h
  ├── strategy/mssql/load_impl.h
  │     ├── core/arrow_builder.h
  │     ├── strategy/mssql/schema_describe.h → sql_type_map.h
  │     ├── strategy/mssql/fetch_buffer.h → sql_type_map.h
  │     ├── strategy/mssql/load_dispatch.h → fetch_buffer.h, arrow_builder.h
  │     ├── strategy/mssql/stmt_handle.h → odbc_error.h
  │     ├── strategy/mssql/parallel_load.h
  │     │     ├── strategy/mssql/load_cache.h → load_connection_pool.h → backend.h
  │     │     ├── strategy/mssql/pk_detect.h
  │     │     └── strategy/mssql/schema_describe.h (shared)
  │     └── strategy/mssql/backend.h (already included)
  └── adapter/adapter.h
        ├── adapter/arrow_import.h        ← py::object → RecordBatchReader
        ├── adapter/arrow_export.h        ← Table → py::object
        ├── core/connection_pool.h → core/backend_policy.h
        ├── core/query.h (already included)
        └── core/persistence.h → backend_policy.h, connection_pool.h, dialect.h, query.h
```

---

## 4. Key Design Decisions

### 4.1 Concepts as Core Contracts

`BackendPolicy` and `DialectPolicy` are defined in `core/` with no knowledge
of concrete backends. New backends satisfy the concepts — no core code changes.

```cpp
template <typename B>
concept BackendPolicy = requires(std::string_view s, int packet_size,
                                 typename B::Connection& conn) {
    typename B::Connection;
    typename B::SaveImpl;
    typename B::LoadImpl;
    typename B::Dialect;
    typename B::LoadCache;

    { B::connect(s, packet_size) } -> std::same_as<typename B::Connection>;
    { B::reset(conn) }            -> std::same_as<void>;
    { conn.close() }              -> std::same_as<void>;
    { B::name() }                 -> std::convertible_to<const char*>;
} && DialectPolicy<typename B::Dialect>;
```

### 4.2 Dialect-Based SQL Rendering

`Query` stores intent (columns, table, where, limit). Each backend's `Dialect`
renders intent into backend-specific SQL. `build_sql(Query, Dialect)` dispatches
at compile time.

- `MssqlDialect` → `SELECT TOP N [col] FROM [schema].[table] WHERE ...`
- (future) `PostgresDialect` → `SELECT "col" FROM "table" WHERE ... LIMIT N`

### 4.3 Strategy Isolation

MSSQL-specific types live in `strategy/mssql/` with their own namespace.
Core headers never include strategy headers. Only `bindings.cpp` (and
`test_bindings.cpp`) pull in both, where `static_assert(BackendPolicy<MssqlBackend>)`
verifies concept satisfaction at compile time.

### 4.4 Format as Runtime Enum (Not Template Parameter)

`RepositoryAdapter<Backend>` in `adapter/adapter.h` owns `core::Repository<Backend>`
directly — ONE hop, matching the Registry/Factory adapter pattern. Format is a
runtime `enum class Format { Polars, Pandas }` member, yielding ONE template
instantiation per backend (not 2×).

### 4.5 Template Instantiation at the Edge

`bindings.cpp` is the single production translation unit. All template
instantiations happen here. `test_bindings.cpp` uses `py::module_local()`
to avoid type conflicts with the production module.

### 4.6 RepositoryAdapter Owns Core Directly

```
Python  →  RepositoryAdapter<MssqlBackend>  →  core::Repository<MssqlBackend>
               (adapter layer)                    (core layer)
```

- Pre/post transforms: `std::vector<py::function>` hooks run at the Python boundary
- Arrow import: `import_table()` converts py::object → `shared_ptr<arrow::Table>` via PyCapsule
- Arrow export: `export_table()` converts `shared_ptr<arrow::Table>` → Polars/Pandas via C Data Interface
- GIL release: adapter releases GIL before calling core `save()`/`load()` — core is GIL-free
- Core operates on Arrow exclusively — no `py::object` in `core/`

### 4.7 Portable Temporal Structs

`ArrowBuilder` in `core/` does NOT include ODBC headers. Instead, it defines
portable structs (`detail::DateStruct`, `detail::TimestampStruct`, `detail::Time2Struct`)
that are binary-compatible with their ODBC counterparts. The strategy layer
(`load_dispatch.h`) includes a `static_assert` verifying layout compatibility.
This keeps the core layer free of platform-specific ODBC dependencies.

### 4.8 RAII Arrow Export

`export_table()` wraps the `ArrowArrayStream` in a `std::unique_ptr` with a
custom deleter that calls `release()` (if active) then `delete`. This eliminates
the previous manual `new`/`delete` pattern and ensures cleanup on exception paths
without explicit try/catch.

### 4.9 GimError Exception Bridge

`bindings.cpp` registers `py::exception<std::runtime_error>(m, "GimError", PyExc_RuntimeError)`.
This maps all C++ `std::runtime_error` exceptions to `GimError` (a `RuntimeError` subclass)
at the Python boundary. Existing `except RuntimeError` catches remain compatible.
This aligns with the project convention of using `GimError` hierarchy for library-level
failures (defined in `pygim.core.explib`).

### 4.10 Connection Pool

`ConnectionPool<Backend>` is thread-safe with:
- Bounded creation (`max_size` constructor param)
- RAII `ConnectionHandle` (auto-return on destruction)
- `std::expected<ConnectionHandle, PoolError>` for checkout — timeout and pool-closed are control flow, not exceptions
- Priority: reuse idle → create new → wait with timeout

### 4.11 Dual Pool Pattern

Two distinct pool types serve different purposes:

| Pool                      | Purpose                              | Lifetime                          |
|---------------------------|--------------------------------------|-----------------------------------|
| `ConnectionPool<Backend>` | Shared save + metadata operations    | Repository lifetime               |
| `LoadConnectionPool`      | Dedicated parallel load workers      | Per-load or cached via LoadCache  |

Load workers need dedicated connections held for the entire query duration.
Save/metadata connections are short-lived checkouts. Don't merge them.

### 4.12 LoadCache Pattern

`MssqlLoadCache` persists a `LoadConnectionPool` across multiple `load()` calls.
Invalidates when `conn_str` or `pool_size` changes. `NullLoadCache` is zero-cost
for non-MSSQL backends (empty struct). Eliminates 0.06–0.15s per-load connection
establishment overhead for repeated load operations.

### 4.13 O(1) Dispatch Tables

Both save and load paths use function-pointer dispatch tables indexed by
`arrow::Type::type` (compile-time populated `std::array`):

- **Save**: `bcp_bind_dispatch` — per-type bind functions; `bcp_rebind_dispatch` — fast pointer-only rebind
- **Load**: `load_dispatch` — per-type block-append functions with nullable flag

Zero branching in hot loops. Type resolution happens once during schema setup.

### 4.14 Block Cursor Architecture

ODBC block cursors fetch multiple rows per `SQLFetch()` call:

1. `FetchBufferSet::allocate(col_info, block_size)` — pre-allocate per-column buffers
2. `SQLSetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE, block_size)` — configure block fetch
3. `SQLFetch()` fills buffers + writes `rows_fetched`
4. `indicators_to_valid_bytes()` — branchless null conversion (compiles to `cmp` + `setne`)
5. `dispatch[c].fn(builder, col, buf, nrows, valid)` — O(1) type dispatch

Configurable `block_size` (default 4096). Tunable via `acquire_datastore(block_size=...)`.

### 4.15 PK Auto-Detection

When `partition_column` is empty and `load_workers > 1`:
1. `detect_partition_column()` calls `SQLPrimaryKeys()` to enumerate PK columns
2. `SQLColumns()` checks if the PK column is an integer type (`SQL_INTEGER`, `SQL_BIGINT`, `SQL_SMALLINT`, `SQL_TINYINT`)
3. Returns the first integer PK column, or empty string if none found
4. Falls back to single-threaded load when no suitable partition key exists
5. Supports `catalog.schema.table` parsing via `parse_table_name()`

---

## 5. Implementation Status

All components are fully implemented and tested.

| Component               | Status          | Description                                                     |
|-------------------------|-----------------|-----------------------------------------------------------------|
| `OdbcConnection`        | **Implemented** | Real ODBC connection (SQLDriverConnect) with BCP + packet_size  |
| `MssqlSaveImpl`         | **Implemented** | Single + parallel BCP via bcp_pipeline.h                        |
| `BCP pipeline`          | **Implemented** | Full Arrow→BCP bind/row-loop/flush (16+ column types)           |
| `BcpConnectionPool`     | **Implemented** | Parallel eager pool — O(1×latency) via std::thread              |
| `BCP rebind dispatch`   | **Implemented** | Fast pointer-only rebind for multi-batch streaming              |
| `BCP profiler`          | **Implemented** | Optional per-section profiling (PYGIM_BCP_PROFILING)            |
| `arrow_import.h`        | **Implemented** | PyCapsule + Polars + PyArrow fallback                           |
| `arrow_export.h`        | **Implemented** | Arrow C Data Interface → PyArrow → Polars/Pandas               |
| `MssqlLoadImpl`         | **Implemented** | Block cursor + parallel range-partitioned load                  |
| `ArrowBuilder`          | **Implemented** | Full columnar builder (all ODBC types → Arrow)                  |
| `load_dispatch`         | **Implemented** | O(1) block-append dispatch table                                |
| `FetchBufferSet`        | **Implemented** | Per-column ODBC buffer allocation + bind                        |
| `schema_describe`       | **Implemented** | SQLDescribeCol → SchemaInfo (schema + nullable flags)           |
| `parallel_load`         | **Implemented** | N-worker range-partitioned load + ConcatenateTables             |
| `LoadConnectionPool`    | **Implemented** | Dedicated parallel worker connections                           |
| `MssqlLoadCache`        | **Implemented** | Persistent pool cache (invalidate on change)                    |
| `pk_detect`             | **Implemented** | SQLPrimaryKeys + SQLColumns integer PK detection                |
| `ConnectionPool`        | **Implemented** | Thread-safe pool with std::expected checkout                    |
| `Query / MssqlDialect`  | **Implemented** | Intent builder + T-SQL rendering                                |

---

## 6. Save Pipeline

### 6.1 Single-Connection BCP Save

**Pipeline:**
1. Adapter converts py::object → `shared_ptr<arrow::Table>` via `import_table()` (with GIL)
2. Adapter releases GIL, calls `core::Repository::save(table, table_name, ...)`
3. `MssqlSaveImpl` dispatches to `bcp::bulk_insert(dbc, table, table_name, batch_size, hint)`
4. `init_session()`: `bcp_init(table, DB_IN)` + optional `bcp_control(BCPHINTS, "TABLOCK")`
5. Per-RecordBatch:
   - First batch: `bind_columns()` — full `bcp_bind` per column via `bcp_bind_dispatch`
   - Subsequent batches: `rebind_columns()` — pointer-only update via `bcp_rebind_dispatch` (same schema)
   - Classify columns: fixed/string/temporal + check `any_has_nulls`
   - Fast path (no nulls): `row_loop_fast()` — memcpy staging + `bcp_sendrow`
   - General path (has nulls): `row_loop_general()` — per-cell null check → `bcp_collen(SQL_NULL_DATA)`
   - `flush_batch()` at configurable intervals (default 100K rows)
6. `finalize_bcp()`: `bcp_batch()` + `bcp_done()` with retry on transient failure
7. `BcpSessionGuard` provides RAII safety — calls `bcp_done()` on exception

### 6.2 Parallel BCP Save

```
RepositoryAdapter::save(df, table, bcp_workers=N)
  │
  ├── adapter: df → arrow::Table (with GIL, via import_table)
  ├── adapter: release GIL
  │
  └── core → MssqlSaveImpl → bcp::bulk_insert_parallel(conn_str, table, ...)
        │
        ├── Read all RecordBatches into memory
        ├── Slice large batches into N sub-batches (zero-copy via arrow::Slice())
        ├── Greedy least-loaded partition across workers
        ├── BcpConnectionPool(conn_str, N) — parallel establishment
        │
        └── spawn N std::jthread workers:
              worker_i:
                init_session(table, TABLOCK)
                for each batch: process_batch (bind/rebind + row loop)
                finalize_bcp()
        │
        ├── Join all, rethrow first error
        └── merge_parallel(worker_metrics) — max wall-clock, sum counts
```

### 6.3 BcpMetrics

Returned from both single and parallel paths:

| Field                  | Type     | Semantics                                      |
|------------------------|----------|-------------------------------------------------|
| `total_seconds`        | double   | Wall-clock (max across parallel workers)        |
| `connect_seconds`      | double   | Connection establishment time                   |
| `bind_seconds`         | double   | Column binding time                             |
| `row_loop_seconds`     | double   | Data copy + sendrow time                        |
| `batch_flush_seconds`  | double   | bcp_batch() + bcp_done() time                   |
| `processed_rows`       | int64_t  | Total rows processed (sum across workers)       |
| `sent_rows`            | int64_t  | Rows confirmed sent by BCP (sum)                |
| `record_batches`       | int64_t  | Arrow RecordBatches processed (sum)             |

Optional `BcpProfiler` sub-struct (enabled via `PYGIM_BCP_PROFILING` compile flag)
provides per-section breakdown: bind/rebind/classify/fixed_copy/string_copy/
sendrow/mid_flush/final_flush/init_session/reader_next seconds + call counts.

---

## 7. Load Pipeline

### 7.1 Single-Connection Block Cursor Load

**Pipeline:**
1. Adapter releases GIL, calls `Repository::load(source, load_workers, partition_column)`
2. Repository resolves source: raw SQL (contains space) or table name → `build_sql(Query, dialect)`
3. `MssqlLoadImpl::execute()`:
   - `StmtHandle(dbc)` — RAII statement allocation
   - `SQLPrepare(sql)` + `SQLExecute()`
   - `describe_columns(stmt)` → `SchemaInfo` (Arrow schema + `TypeMapping` per column + nullable flags)
   - `ArrowBuilder(schema)` — columnar builder with per-type array builders
   - `FetchBufferSet::allocate(col_info, block_size)` — pre-allocate per-column fetch buffers
   - `build_column_dispatch(schema, nullable_flags)` → `vector<ColumnDispatch>` (O(1) lookup)
   - `SQLSetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE, block_size)` — configure block fetch
   - `buffers.bind(stmt)` — `SQLBindCol` for all columns
4. Fetch loop (until `SQL_NO_DATA`):
   - `SQLFetch()` fills block buffers; driver writes `rows_fetched`
   - Per column: `indicators_to_valid_bytes()` (branchless null conversion)
   - Per column: `dispatch[c].fn(builder, col, buf, nrows, valid_ptr)` (O(1) type dispatch)
5. `builder.finish()` → `shared_ptr<arrow::Table>`
6. Return `LoadResult{table, metrics}`
7. Adapter acquires GIL, calls `export_table(table, use_polars)` → Polars/Pandas DataFrame

### 7.2 Parallel Range-Partitioned Load

Triggered when `load_workers > 1` and a suitable integer PK column is found
(either explicit `partition_column` or auto-detected via `pk_detect`):

```
Repository::load(table, load_workers=N)
  │
  ├── checkout primary connection
  ├── MssqlLoadImpl::execute() detects partition_column
  │     └── detect_partition_column(dbc, table) via SQLPrimaryKeys + SQLColumns
  │
  └── execute_parallel(conn, table, partition_col, N, block_size, load_cache, packet_size)
        │
        ├── query_min_max(conn, table, col) → RangeInfo{min, max}
        ├── Adjust N if range < N workers
        ├── generate_worker_queries() → N range-partitioned WHERE clauses
        ├── describe_columns(stmt) → SchemaInfo (shared across workers)
        │
        ├── load_cache.ensure_pool(conn_str, N-1, packet_size)
        │     └── Reuse existing pool if conn_str + size unchanged
        │         OR create new LoadConnectionPool(conn_str, N-1, packet_size)
        │
        └── spawn N std::jthread workers:
              Worker 0: primary connection (this thread's checkout)
              Workers 1..N-1: LoadConnectionPool connections
              Each worker:
                StmtHandle → SQLPrepare(range_sql) → SQLExecute
                FetchBufferSet::allocate(shared_schema, block_size)
                build_column_dispatch + buffers.bind
                fetch loop → ArrowBuilder → finish()
        │
        ├── ConcatenateTables(worker_tables) → single arrow::Table
        └── Aggregate metrics (max wall-clock, sum counts)
```

### 7.3 Stale Connection Retry

On connection errors (`SQLSTATE 08S01, 08001, HY000`) during parallel load:
1. Clear `MssqlLoadCache` (force fresh connections on retry)
2. Retry the entire parallel load once with new connections
3. No proactive health checks (avoids latency on the common path)

### 7.4 LoadResult / LoadMetrics

| Field                | Type     | Description                                   |
|----------------------|----------|-----------------------------------------------|
| `total_seconds`      | double   | End-to-end wall-clock                         |
| `prepare_seconds`    | double   | SQLPrepare + SQLExecute                       |
| `describe_seconds`   | double   | describe_columns() + dispatch table build     |
| `fetch_seconds`      | double   | Block cursor fetch loop                       |
| `build_seconds`      | double   | ArrowBuilder append + finish                  |
| `connect_seconds`    | double   | LoadConnectionPool establishment              |
| `concat_seconds`     | double   | ConcatenateTables (parallel only)             |
| `fetched_rows`       | int64_t  | Total rows fetched                            |
| `fetched_blocks`     | int64_t  | Number of SQLFetch blocks processed           |
| `columns`            | int64_t  | Column count                                  |
| `workers_used`       | int      | Actual parallelism level (may be < requested) |

---

## 8. Arrow Import / Export

### 8.1 Import (Save Path)

`import_table(py::object)` → `shared_ptr<arrow::Table>`. Called WITH GIL.

| Protocol                   | Detection                       | Path                                          |
|----------------------------|---------------------------------|-----------------------------------------------|
| PyCapsule (modern)         | `__arrow_c_stream__` attribute  | `ImportRecordBatchReader(c_stream)` → `ToTable()` |
| Polars DataFrame           | `to_arrow()` method             | Recurse `depth=1` → re-enter import           |
| PyArrow legacy             | `_export_to_c` method           | `_export_to_c(addr)` → import                 |

### 8.2 Export (Load Path)

`export_table(shared_ptr<arrow::Table>, use_polars)` → `py::object`. Called WITH GIL.

1. `arrow::Table` → `TableBatchReader` → `ExportRecordBatchReader`
2. `unique_ptr<ArrowArrayStream>` (RAII) → `PyArrow.RecordBatchReader._import_from_c(addr)`
3. `reader.read_all()` → PyArrow Table
4. `polars.from_arrow(table)` or `table.to_pandas()`

---

## 9. Multithreading Strategy

### 9.1 GIL Release Pattern

```
Python → adapter (with GIL)
  convert DataFrame ↔ Arrow via import/export helpers
  py::gil_scoped_release release;
  → core C++ (no GIL, no py::object)
    pool checkout, spawn workers, do ODBC work
  ← return Arrow pointers
  py::gil_scoped_acquire acquire;  // implicit on scope exit
  convert Arrow → DataFrame
← return to Python
```

**Rules:**
- No `py::object` crosses into `core/` — Arrow is the boundary
- Pre/post transform hooks execute WITH GIL (they are Python callables)
- Core C++ is GIL-free: thread spawning, ODBC calls, Arrow building

### 9.2 Thread Management

- `std::jthread` workers for parallel load (auto-join on scope exit)
- `std::thread` workers for parallel save
- Exception capture: each worker catches into `std::exception_ptr`; after join, rethrow first
- No detached threads — all workers joined before returning to adapter
- Pool handles connection lifecycle; workers only hold `ConnectionHandle` (RAII)

### 9.3 Pool Sizing

| Operation         | Pool Used              | Connections Needed | Notes                              |
|-------------------|------------------------|--------------------|------------------------------------|
| Single save       | ConnectionPool         | 1                  | Default `pool_size=4` sufficient   |
| Parallel save (N) | BcpConnectionPool      | N                  | Separate pool, not from main pool  |
| Single load       | ConnectionPool         | 1                  | Default sufficient                 |
| Parallel load (N) | ConnectionPool + LoadCP | 1 + (N-1)         | Primary from main pool; rest from LoadConnectionPool |

---

## 10. Performance

### 10.1 Benchmarks (Docker SQL Server, 8 workers, exhaustive dataset)

| Direction | Rows/s        | MB/s    | Dataset                    |
|-----------|---------------|---------|----------------------------|
| Write     | ~956K         | ~101    | 18 columns, 1M rows       |
| Load      | ~1.25M        | ~122    | 18 columns, 1M rows       |

### 10.2 Configurable Tuning Parameters

All parameters are configurable via `acquire_datastore()` and the benchmark CLI:

| Parameter         | Default   | Scope         | Effect                                      | CLI flag          |
|-------------------|-----------|---------------|----------------------------------------------|-------------------|
| `block_size`      | 4096      | Load          | Rows per SQLFetch call (block cursor)        | `--block-size`    |
| `packet_size`     | 16384     | Load + Pool   | TDS network packet size (bytes)              | `--packet-size`   |
| `batch_size`      | 100,000   | Save          | Rows per bcp_batch flush                     | `--batch-size`    |
| `bcp_workers`     | 1         | Save          | Parallel BCP threads                         | `--workers`       |
| `load_workers`    | 1         | Load          | Parallel range-partitioned workers           | `--workers`       |
| `pool_size`       | 4         | ConnectionPool| Max shared pooled connections                | —                 |

**Tuning findings (exhaustive dataset, 1M rows, 8 workers):**
- `block_size=4096` is the user-facing default; internal benchmark constant `kDefaultBlockSize` is 8192 — consider propagating to adapter defaults
- `block_size=16384` can improve load by ~28% but increases memory per worker
- `batch_size=100K` is optimal — both smaller and larger values degrade write
- `packet_size` is capped at 16384 with `Encrypt=yes` (TLS record limit)

---

## 11. Other Backends & Approaches

### 11.1 PostgreSQL (Future)

| Aspect       | MSSQL (BCP)                      | PostgreSQL (COPY)               |
|--------------|----------------------------------|---------------------------------|
| Bulk insert  | ODBC BCP API (`bcp_sendrow`)     | `COPY FROM STDIN` (binary/CSV)  |
| Bulk load    | Block cursor over ODBC           | `COPY TO STDOUT` (binary/CSV)   |
| Parallel     | N connections, each BCP / range  | N connections, each COPY / range|
| Driver       | ODBC (unixODBC + MS driver)      | libpq (native C API)           |
| Dialect      | `[bracket]` quoting, `TOP N`     | `"double-quote"`, `LIMIT N`    |

**Strategy files needed:**
```
strategy/postgres/
├── backend.h       ← PgConnection + PostgresBackend
├── dialect.h       ← PostgresDialect
├── save_impl.h     ← COPY FROM STDIN with Arrow data
└── load_impl.h     ← COPY TO STDOUT → ArrowBuilder
```

### 11.2 SQLite (Testing / In-Memory)

Useful for offline tests without a real DB server. ConnectionPool degenerates
to single-connection (SQLite single-writer). LoadCache uses `NullLoadCache`.

### 11.3 Read-Only Analytical Patterns

Export to Parquet or other formats is a Python-layer concern. Implement as
a post-transform hook: `repo.add_post_transform(write_parquet_fn)`.

---

## 12. Generalization & Reuse Analysis

### What in `core/` Is Truly Backend-Agnostic?

| Component         | Backend-agnostic? | Notes                                              |
|-------------------|-------------------|----------------------------------------------------|
| `BackendPolicy`   | Yes               | Pure concept — defines the contract (+ LoadCache)   |
| `DialectPolicy`   | Yes               | Pure concept — defines SQL rendering contract       |
| `Query`           | Yes               | Stores intent; no backend-specific SQL              |
| `build_sql()`     | Yes               | Delegates to dialect; compile-time dispatch         |
| `ConnectionPool`  | Yes               | Templated on Backend; works for any compliant type  |
| `ConnectionHandle`| Yes               | RAII wrapper; backend-agnostic                      |
| `ArrowBuilder`    | Mostly            | Column types generic; ODBC fetch semantics may leak |
| `Repository`      | Yes               | Delegates save/load to `Backend::SaveImpl/LoadImpl` |
| `LoadResult`      | Yes               | Arrow Table + timing metrics                        |
| `NullLoadCache`   | Yes               | Zero-cost for backends without persistent caching   |

### What Would a New Backend Need to Implement?

**Minimum viable backend (4 files + LoadCache):**

```cpp
struct NewDbBackend {
    using Connection = NewDbConnection;    // must have close()
    using SaveImpl   = NewDbSaveImpl;      // static execute(conn, ...)
    using LoadImpl   = NewDbLoadImpl;      // static execute(conn, ...)
    using Dialect    = NewDbDialect;       // satisfies DialectPolicy
    using LoadCache  = NullLoadCache;      // or custom persistent cache

    static Connection connect(std::string_view conn_str, int packet_size = 0);
    static void reset(Connection& conn);
    static constexpr const char* name() { return "newdb"; }
};
```

Then in bindings:
```cpp
static_assert(BackendPolicy<NewDbBackend>);
using NewDbRepo = adapter::RepositoryAdapter<NewDbBackend>;
```

---

## 13. Risks & Trade-offs

| Risk                                      | Likelihood | Impact   | Mitigation                                                      |
|-------------------------------------------|------------|----------|-----------------------------------------------------------------|
| ODBC driver differences (Linux vs Windows)| Medium     | High     | CI on both platforms; abstract platform-specific alloc if needed |
| `bcp_sendrow` remains the bottleneck      | High       | Medium   | Parallel connections are the only path to higher throughput      |
| Arrow C++ library dependency bloat        | Medium     | Medium   | Arrow C Data Interface (raw structs) used at adapter boundary   |
| String column null-termination copies     | High       | Low      | <5% impact; staging buffer is simple and correct                |
| GIL contention in transform hooks         | Low        | Low      | Transforms run before/after GIL release; don't interleave       |
| Range-partitioned load skew               | Medium     | Medium   | Auto-adjust workers based on range; fallback to single-thread   |
| LoadCache invalidation edge cases         | Low        | Medium   | Invalidate on conn_str or pool_size change; stale retry once    |
| `packet_size` > 16384 with TLS            | N/A        | N/A      | Hard limit by ODBC Driver 18; document as known constraint      |

---

## 14. Adding a New Backend (Checklist)

1. Create `strategy/<name>/` with `backend.h`, `dialect.h`, `save_impl.h`, `load_impl.h`
2. Backend struct satisfies `BackendPolicy` concept (including `LoadCache` type)
3. Dialect struct satisfies `DialectPolicy` concept
4. Set `using LoadCache = NullLoadCache` unless persistent caching is needed
5. Add `static_assert(BackendPolicy<NewBackend>)` in bindings
6. Add `ext.<name>.toml` build configuration
7. Add `test_bindings.cpp` with `py::module_local()` for test isolation
8. No changes to `core/` required (if concepts are sufficient)
9. Add unit tests parametrized by backend (extend existing test matrix)
