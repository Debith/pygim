# Repository Module — Architecture & Roadmap

Current as of the `core/repository` branch. Replaces the archived
`repository_file_structure.md` (which referenced the deleted `format/` directory
and `FormatAdapter`/`FlexibleRepository` classes). This is the single source
of truth for the repository module's design.

---

## 1. Directory Layout

```
src/_pygim_fast/repository/
├── core/                       ← Backend-agnostic concepts + generic logic
│   ├── arrow_builder.h         ← ArrowBuilder columnar builder (ODBC results → Arrow)
│   ├── backend_policy.h        ← BackendPolicy concept (C++20)
│   ├── connection_pool.h       ← Thread-safe ConnectionPool + RAII ConnectionHandle
│   ├── dialect.h               ← DialectPolicy concept + build_sql() free function
│   ├── query.h                 ← Fluent Query builder (intent-only + accessors)
│   └── repository.h            ← Generic Repository<Backend> facade
├── strategy/
│   └── mssql/                  ← SQL Server concrete backend
│       ├── backend.h           ← OdbcConnection + MssqlBackend struct
│       ├── dialect.h           ← MssqlDialect (TOP N, [bracket]-quoting)
│       ├── save_impl.h         ← MssqlSaveImpl (BCP bulk insert) — PLACEHOLDER
│       └── load_impl.h         ← MssqlLoadImpl (block cursor → ArrowBuilder) — PLACEHOLDER
└── adapter/                    ← Python bindings (pybind11 edge)
    ├── adapter.h               ← RepositoryAdapter<Backend> (owns Repository directly)
    ├── bindings.cpp            ← Production: Repository + Format enum + acquire_repo()
    └── test_bindings.cpp       ← Test-only: Query, MssqlDialect, Repository (module_local)

src/pygim/
└── repository.py               ← Public Python API: re-exports acquire_repo, Repository, Format
```

### Build Configuration

| TOML file                  | Module name         | Sources                                  | Purpose       |
|----------------------------|---------------------|------------------------------------------|---------------|
| `ext.repository.toml`      | `_repository`       | `repository/adapter/bindings.cpp`        | Production    |
| `ext.repository_test.toml` | `_repository_test`  | `repository/adapter/test_bindings.cpp`   | Test-only     |

---

## 2. Namespace Mapping

| Directory           | C++ Namespace              | Responsibility                          |
|---------------------|----------------------------|-----------------------------------------|
| `core/`             | `pygim::core`              | Concepts, generic templates, Query      |
| `strategy/mssql/`   | `pygim::strategy::mssql`   | MSSQL-specific types + SQL dialect      |
| `adapter/`          | `pygim::adapter`           | Format enum, RepositoryAdapter, bindings|

---

## 3. Include Dependency Graph

```
bindings.cpp
  ├── core/query.h
  ├── strategy/mssql/save_impl.h
  │     └── strategy/mssql/backend.h
  │           ├── core/backend_policy.h
  │           │     └── core/dialect.h
  │           │           └── core/query.h
  │           ├── strategy/mssql/dialect.h
  │           │     └── core/query.h
  │           └── utils/logging.h
  ├── strategy/mssql/load_impl.h
  │     ├── core/arrow_builder.h
  │     └── strategy/mssql/backend.h  (already included)
  └── adapter/adapter.h
        ├── core/connection_pool.h
        │     └── core/backend_policy.h  (already included)
        ├── core/query.h  (already included)
        └── core/repository.h
              ├── core/backend_policy.h  (already included)
              ├── core/connection_pool.h (already included)
              ├── core/dialect.h         (already included)
              └── core/query.h           (already included)
```

---

## 4. Key Design Decisions

### 4.1 Concepts as Core Contracts

`BackendPolicy` and `DialectPolicy` are defined in `core/` with no knowledge
of concrete backends. New backends satisfy the concepts — no core code changes.

```cpp
template <typename B>
concept BackendPolicy = requires(std::string_view s, typename B::Connection& conn) {
    typename B::Connection;
    typename B::SaveImpl;
    typename B::LoadImpl;
    typename B::Dialect;
    { B::connect(s) }  -> std::same_as<typename B::Connection>;
    { B::reset(conn) } -> std::same_as<void>;
    { conn.close() }   -> std::same_as<void>;
    { B::name() }      -> std::convertible_to<const char*>;
} && DialectPolicy<typename B::Dialect>;
```

### 4.2 Dialect-Based SQL Rendering

`Query` stores intent (columns, table, where, limit). Each backend's `Dialect`
renders intent into backend-specific SQL. `build_sql(Query, Dialect)` dispatches
at compile time.

- `MssqlDialect` → `SELECT TOP N [col] FROM [table] WHERE ...`
- (future) `PostgresDialect` → `SELECT "col" FROM "table" WHERE ... LIMIT N`

### 4.3 Strategy Isolation

MSSQL-specific types live in `strategy/mssql/` with their own namespace.
Core headers never include strategy headers. Only `bindings.cpp` (and
`test_bindings.cpp`) pull in both, where `static_assert(BackendPolicy<MssqlBackend>)`
verifies concept satisfaction at compile time.

### 4.4 Format as Runtime Enum (Not Template Parameter)

`format/` directory was deleted. `FormatAdapter<Backend, Fmt>` and
`FlexibleRepository<Backend, Fmt>` are gone.

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
- Format conversion: Will bridge Arrow ↔ Polars/Pandas at the adapter edge
- Core operates on Arrow exclusively — no `py::object` in `core/`

### 4.7 Connection Pool

`ConnectionPool<Backend>` is thread-safe with:
- Bounded creation (`max_size` constructor param)
- RAII `ConnectionHandle` (auto-return on destruction)
- `std::expected<ConnectionHandle, PoolError>` for checkout — timeout and pool-closed are control flow, not exceptions
- Priority: reuse idle → create new → wait with timeout

---

## 5. Current Placeholder State

All strategy implementations log only — no real ODBC calls.

| Component               | Status      | What It Logs / Does                                    |
|-------------------------|-------------|--------------------------------------------------------|
| `OdbcConnection`        | Placeholder | Stores connection string; logs open/close              |
| `MssqlSaveImpl`         | Placeholder | Logs BCP pipeline steps (init, bind, sendrow, done)    |
| `MssqlLoadImpl`         | Placeholder | Logs block cursor steps (fetch, bind, advance)         |
| `ArrowBuilder`          | Placeholder | Logs append operations; has ColumnType enum            |
| `ConnectionPool`        | Functional  | Real thread-safe pool of placeholder connections       |
| `Query` / `MssqlDialect`| Functional  | Intent builder + SQL rendering work correctly          |

---

## 6. BCP Implementation Roadmap

Based on proven archive performance (77.8 MB/s parallel BCP with 16 workers):

### Phase 1: Real ODBC Connection

**Goal:** Replace `OdbcConnection` placeholder with real ODBC handle management.

**Files:** `strategy/mssql/backend.h`

```cpp
struct OdbcConnection {
    SQLHENV  m_env;
    SQLHDBC  m_dbc;
    SQLHSTMT m_stmt;
    std::string m_conn_str;

    void open(std::string_view conn_str);   // SQLAllocHandle + SQLDriverConnect
    void close();                           // SQLDisconnect + SQLFreeHandle
};
```

**Key decisions:**
- `SQLHENV` allocated once per pool (shared), not per connection
- Env handle ownership moves to `ConnectionPool` — extend constructor to create/hold `SQLHENV`
- `SQLHSTMT` allocated lazily per operation, freed after
- `MssqlBackend::reset()` → `SQLFreeStmt(SQL_CLOSE) + SQLFreeStmt(SQL_UNBIND)`

**Risk:** ODBC driver differences between platforms (unixODBC vs Windows native). Mitigate by abstracting `SQLAllocHandle` calls behind `#ifdef _WIN32` only if needed.

### Phase 2: Single-Connection BCP Save

**Goal:** Real BCP bulk insert on a single connection.

**Files:** `strategy/mssql/save_impl.h`, new `strategy/mssql/bcp_helpers.h`

**Pipeline:**
1. Receive Arrow RecordBatch (via Arrow C Data Interface — zero-copy from Python)
2. `bcp_init(table, DB_IN, conn)` with `TABLOCK` hint
3. `SQLBindCol` per column from Arrow schema:
   - Fixed-width (int64, float64, bool): bind into staging buffer, toggle null via `bcp_collen`
   - String columns: stage null-terminated copies (Arrow stores without null terminators)
4. Row loop: `bcp_sendrow()` per row (85%+ of wall time per archive benchmarks)
5. `bcp_batch()` at configurable intervals (default: every 10K rows)
6. `bcp_done()` → final commit

**Arrow C Data Interface integration:**
```cpp
static void execute(OdbcConnection& conn,
                    const ArrowArray* array,
                    const ArrowSchema* schema,
                    std::string_view table_name,
                    int bcp_workers);
```

Adapter calls Python `_export_to_c()` to get raw `ArrowArray*`/`ArrowSchema*`
pointers, then calls into C++ with GIL released. Core stays pybind11-free.

### Phase 3: Parallel BCP Save

**Goal:** N workers, each with own connection, processing partitioned Arrow batches.

**Architecture:**
```
RepositoryAdapter::save(df, table, bcp_workers=N)
  │
  ├── adapter: df → ArrowArray/ArrowSchema (with GIL)
  ├── adapter: release GIL
  │
  └── core::Repository::save(arrow_data, table, N)
        │
        ├── Arrow::Slice() into N sub-batches (zero-copy, O(1))
        │
        └── spawn N std::thread workers:
              worker_i:
                conn = pool.checkout()
                bcp_init(table, TABLOCK)
                SQLBindCol per column
                bcp_sendrow loop over sub-batch[i]
                bcp_done()
                conn auto-returns to pool
```

**Key properties:**
- Zero-copy partitioning via `Arrow::Slice()` (pointer arithmetic only)
- Each worker holds own connection — no shared-connection contention
- Pool size must be ≥ `bcp_workers`; `save()` validates upfront
- `TABLOCK` hint required for parallel BCP (SQL Server constraint)
- Exception aggregation: collect from all threads, throw first after join

**Archive baseline:** 77.8 MB/s @ 16 workers (1M × 11 cols, heap table).

### Phase 4: Single-Connection Block Cursor Load

**Goal:** Real ODBC block cursor fetch → ArrowBuilder → RecordBatch.

**Files:** `strategy/mssql/load_impl.h`, `core/arrow_builder.h`

**Pipeline:**
1. `SQLPrepare(sql)` + `SQLExecute()`
2. `SQLDescribeCol()` → build `vector<ColumnInfo>` → create `ArrowBuilder`
3. Set `SQL_ATTR_ROW_ARRAY_SIZE = 1024` (configurable block size)
4. `SQLBindCol` per column into staging buffers
5. Fetch loop:
   - `SQLFetch()` fills block
   - Build validity bitmask from `SQL_NULL_DATA` indicators
   - `append_*_batch()` per column into ArrowBuilder
   - Repeat until `SQL_NO_DATA`
6. `ArrowBuilder::finish()` → `ArrowArray` + `ArrowSchema` (C Data Interface)
7. Adapter imports back into Polars/Pandas (with GIL)

**ArrowBuilder upgrade:** Use raw allocation + Arrow C Data Interface structs
(not Arrow C++ library) to minimize dependencies.

### Phase 5: Parallel Range-Partitioned Load

**Goal:** N workers each executing a range-partitioned query, merge results.

**Architecture:**
```
Repository::load(query, load_workers=N)
  │
  ├── Probe query: SELECT MIN(pk), MAX(pk), COUNT(*) FROM table
  ├── Compute N range boundaries
  │
  └── spawn N std::thread workers:
        worker_i:
          conn = pool.checkout()
          sql_i = "SELECT ... WHERE pk >= lo_i AND pk < hi_i"
          block cursor fetch → ArrowBuilder → RecordBatch_i
          conn auto-returns to pool
  │
  └── ConcatenateTables(batch_0 .. batch_N-1) → single RecordBatch
```

**Limitations:**
- Requires an ordered/indexed column (typically PK) for range partitioning
- Skewed data distributions create uneven worker loads
- Fallback: single-connection path when no suitable partition key exists

---

## 7. Multithreading Strategy

### GIL Release Pattern

```
Python → adapter (with GIL)
  convert DataFrame → Arrow C Data Interface pointers
  py::gil_scoped_release release;
  → core C++ (no GIL, no py::object)
    pool checkout, spawn workers, do ODBC work
  ← return Arrow C Data Interface pointers
  py::gil_scoped_acquire acquire;  // implicit on scope exit
  convert Arrow → DataFrame
← return to Python
```

**Rules:**
- No `py::object` crosses into `core/` — Arrow C Data Interface is the boundary
- Pre/post transform hooks execute WITH GIL (they are Python callables)
- Core C++ is GIL-free: thread spawning, ODBC calls, Arrow building

### Thread Management

- `std::thread` workers (not `std::async` — need deterministic join)
- Exception capture: each worker catches into `std::exception_ptr`; after join, rethrow first
- No detached threads — all workers joined before returning to adapter
- Pool handles connection lifecycle; workers only hold `ConnectionHandle` (RAII)

### Pool Sizing

| Operation         | Connections needed | Recommendation                        |
|-------------------|--------------------|---------------------------------------|
| Single save       | 1                  | Default `pool_size=4` sufficient      |
| Parallel save (N) | N                  | `pool_size ≥ N`; validate in `save()` |
| Single load       | 1                  | Default sufficient                    |
| Parallel load (N) | N + 1              | +1 for probe query                    |

---

## 8. Other Backends & Approaches

### 8.1 PostgreSQL (Future)

| Aspect       | MSSQL (BCP)                      | PostgreSQL (COPY)               |
|--------------|----------------------------------|---------------------------------|
| Bulk insert  | ODBC BCP API (`bcp_sendrow`)     | `COPY FROM STDIN` (binary/CSV)  |
| Bulk load    | Block cursor over ODBC           | `COPY TO STDOUT` (binary/CSV)   |
| Parallel insert | N connections, each BCP       | N connections, each COPY        |
| Parallel load| Range-partitioned SELECT         | Range-partitioned SELECT        |
| Driver       | ODBC (unixODBC + MS driver)      | libpq (native C API)           |
| Dialect      | `[bracket]` quoting, `TOP N`     | `"double-quote"`, `LIMIT N`    |

**Strategy files needed:**
```
strategy/postgres/
├── backend.h       ← PgConnection + PostgresBackend
├── dialect.h       ← PostgresDialect ("double-quote", LIMIT N)
├── save_impl.h     ← COPY FROM STDIN with Arrow data
└── load_impl.h     ← COPY TO STDOUT → ArrowBuilder, or block cursor
```

**Key difference:** PostgreSQL's `COPY` protocol is stream-based, not row-based
like BCP. SaveImpl would format Arrow columns into COPY binary format and push
via `PQputCopyData()`. Fundamentally different from BCP's bind+sendrow pattern,
but fits the same `SaveImpl::execute()` static interface.

### 8.2 SQLite (Testing / In-Memory)

Useful for offline tests without a real DB server.

```
strategy/sqlite/
├── backend.h       ← SqliteConnection (wraps sqlite3*)
├── dialect.h       ← SqliteDialect (no bracket quoting, no TOP N)
├── save_impl.h     ← Prepared INSERT batches
└── load_impl.h     ← sqlite3_step() → ArrowBuilder
```

**Trade-off:** SQLite lacks BCP/COPY, so save performance is low. But for tests
that exercise the Repository API contract without network I/O, it's ideal.
ConnectionPool degenerates to single-connection pool (SQLite single-writer).

### 8.3 Generic ODBC Fallback

A `GenericOdbcBackend` using standard `SQLBulkOperations` or parameterized
INSERT for save, and block cursor for load. No vendor-specific BCP or COPY.

**Assessment:** Low priority. BCP is the performance path; ODBC fallback adds
maintenance cost for a use case better served by existing Python libraries
(SQLAlchemy, connectorx). Defer unless a concrete user need arises.

### 8.4 Read-Only Analytical Patterns

- Export to Parquet: `load()` → Arrow RecordBatch → Parquet via PyArrow
- This is a Python-layer concern, not a backend concern
- Implement as a post-transform hook: `repo.add_post_transform(write_parquet_fn)`

---

## 9. Generalization & Reuse Analysis

### What in `core/` Is Truly Backend-Agnostic?

| Component         | Backend-agnostic? | Notes                                              |
|-------------------|-------------------|----------------------------------------------------|
| `BackendPolicy`   | Yes               | Pure concept — defines the contract                 |
| `DialectPolicy`   | Yes               | Pure concept — defines SQL rendering contract       |
| `Query`           | Yes               | Stores intent; no backend-specific SQL              |
| `build_sql()`     | Yes               | Delegates to dialect; compile-time dispatch         |
| `ConnectionPool`  | Yes               | Templated on Backend; works for any compliant type  |
| `ConnectionHandle`| Yes               | RAII wrapper; backend-agnostic                      |
| `ArrowBuilder`    | Mostly            | Column types generic; ODBC fetch semantics may leak |
| `Repository`      | Yes               | Delegates save/load to `Backend::SaveImpl/LoadImpl` |

### What Would a New Backend Need to Implement?

**Minimum viable backend (4 files):**

```cpp
struct NewDbBackend {
    using Connection = NewDbConnection;  // must have close()
    using SaveImpl   = NewDbSaveImpl;    // static execute(conn, ...)
    using LoadImpl   = NewDbLoadImpl;    // static execute(conn, ...)
    using Dialect    = NewDbDialect;     // satisfies DialectPolicy

    static Connection connect(std::string_view conn_str);
    static void reset(Connection& conn);
    static constexpr const char* name() { return "newdb"; }
};
```

Then in bindings:
```cpp
static_assert(BackendPolicy<NewDbBackend>);
using NewDbRepo = adapter::RepositoryAdapter<NewDbBackend>;
```

### Are the Concepts Sufficient?

**Current gaps for future backends:**

| Gap                          | Impact                                          | Remediation                            |
|------------------------------|-------------------------------------------------|----------------------------------------|
| `SaveImpl::execute` signature| Needs Arrow data pointers when moving beyond placeholder | Add `ArrowArray*`/`ArrowSchema*` params. Update `BackendPolicy` or leave `SaveImpl` unconstrained. |
| `LoadImpl::execute` return   | Currently void. Should return Arrow C Data Interface pointers. | Change return type to struct with `ArrowArray` + `ArrowSchema`. |
| No async/streaming support   | `load()` is synchronous; can't stream partial results. | Out of scope. Could add `load_batches()` iterator later. |
| No schema introspection      | Can't discover table schemas without a query. | Add optional `describe(table)` to `BackendPolicy` in a future phase. |

**Recommendation:** Don't expand the concepts now. Let Phase 2 (real BCP save)
drive the signature changes. Over-constraining `SaveImpl`/`LoadImpl` at the
concept level forces all backends into a single function signature, which may
not fit (PostgreSQL COPY uses stream semantics, not row-batch semantics).

### ConnectionPool Reuse

`ConnectionPool<Backend>` works for any backend satisfying `BackendPolicy`.
No changes needed for PostgreSQL or SQLite — they just provide different
`Connection` types with `close()`.

**SQLite caveat:** Single-writer constraint. Construct with `pool_size=1`.
The pool handles this naturally.

---

## 10. Risks & Trade-offs

| Risk                                      | Likelihood | Impact   | Mitigation                                                      |
|-------------------------------------------|------------|----------|-----------------------------------------------------------------|
| ODBC driver differences (Linux vs Windows)| Medium     | High     | CI on both platforms; abstract platform-specific alloc if needed |
| `bcp_sendrow` remains the bottleneck      | High       | Medium   | Parallel connections are the only path to higher throughput (proven) |
| Arrow C++ library dependency bloat        | Medium     | Medium   | Use Arrow C Data Interface (raw structs) — zero dependency      |
| String column null-termination copies     | High       | Low      | Archive proved <5% impact; staging buffer is simple and correct  |
| GIL contention in transform hooks         | Low        | Low      | Transforms run before/after GIL release; don't interleave       |
| Pool exhaustion during parallel save      | Low        | Medium   | Validate `pool_size ≥ bcp_workers` upfront; clear error message |
| Range-partitioned load skew               | Medium     | Medium   | Fall back to single-connection; expose `partition_key` param    |
| ArrowBuilder memory for large results     | Medium     | Medium   | Chunked building with configurable chunk size                   |

---

## 11. Implementation Priority

| Phase | Description                     | Dependency  | Effort | Impact                         |
|-------|---------------------------------|-------------|--------|--------------------------------|
| 1     | Real ODBC connection            | None        | Medium | Enables all subsequent phases  |
| 2     | Single-connection BCP save      | Phase 1     | High   | Core write path                |
| 3     | Parallel BCP save               | Phase 2     | Medium | 2× throughput (proven)         |
| 4     | Single-connection block load    | Phase 1     | High   | Core read path                 |
| 5     | Parallel range load             | Phase 4     | Medium | Read scalability               |
| 6     | PostgreSQL backend              | Phase 1–4   | High   | Multi-backend validation       |
| 7     | SQLite test backend             | Phase 1     | Low    | Offline CI testing             |

Phases 1–4 are the critical path. Phase 5 is incremental. Phases 6–7 validate
generalization but are not blocking for MSSQL production use.

---

## 12. Adding a New Backend (Checklist)

1. Create `strategy/<name>/` with `backend.h`, `dialect.h`, `save_impl.h`, `load_impl.h`
2. Backend struct satisfies `BackendPolicy` concept
3. Dialect struct satisfies `DialectPolicy` concept
4. Add `static_assert(BackendPolicy<NewBackend>)` in bindings
5. Add `ext.<name>.toml` build configuration
6. Add `test_bindings.cpp` with `py::module_local()` for test isolation
7. No changes to `core/` required (if concepts are sufficient)
8. Add unit tests parametrized by backend (extend existing test matrix)
