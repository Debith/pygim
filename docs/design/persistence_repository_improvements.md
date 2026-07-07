# pygim persistence — repository-oriented improvements

Status: P0 + 2.6 implemented (branch `core/persistence-repository`) · Audience: `pygim` maintainers.

> **Implementation status (2026-07-07).** Sections 2.1, 2.2, 2.3 and 2.6 are
> implemented and live-verified against SQL Server 2022. Shipped names differ
> slightly from the sketches below: `access_token=` (str/bytes/callable — the
> raw token; pygim performs the `SQL_COPT_SS_ACCESS_TOKEN` packing, unlike
> pyodbc's `attrs_before`), `DataStore.session()` (no explicit `begin()` —
> ODBC manual-commit transactions start implicitly; the `connection=`
> parameter variant was subsumed by sessions), and `mode="upsert" /
> "insert_missing"` with `keys=[...]`. A generic `attrs_before`-style
> pre-connect map remains out of scope. Sections 2.4/2.5/2.7–2.12 are open.

This document lists concrete changes the `pygim.persistence` `DataStore` (the
C++/ODBC extension) would need to serve a **repository / data-access layer** more
completely and efficiently. It is written from the point of view of code that
consumes the `DataStore` behind a repository abstraction (bulk save/load of tabular
data), not from the point of view of the persistence engine internals.

Each item states a generic data-access requirement, what `pygim` does today, and the
requested change.

---

## 1. How a repository layer consumes pygim today

A typical repository wraps the `DataStore` like this:

- **Save.** Serialise a domain object into a Polars DataFrame whose column names and
  dtypes already match the target table, then call `DataStore.save(df, table_name)`
  for a native BCP bulk insert.
- **Load.** Call `DataStore.load(table_or_sql)` and wrap the returned Polars frame
  back into the domain / tabular type.
- **Acquire.** Build the store once from a connection string via
  `acquire_datastore(dsn, format="polars", …)` and reuse it.

Against that consumption model the current `DataStore` is usable only for **local,
credentialed, append-only** writes. The gaps below are what stop it from being
general and production-grade.

---

## 2. Requested improvements (prioritised)

### P0 — Blockers for anything beyond local, credentialed, append-only

#### 2.1 Managed-identity / access-token authentication
- **Need.** Cloud databases (e.g. Azure SQL) commonly authenticate with a short-lived
  **access token** (Managed Identity / Entra ID) instead of a username/password. With
  pyodbc this is done by passing an `attrs_before` map that sets the
  `SQL_COPT_SS_ACCESS_TOKEN` (1256) pre-connection attribute (and by stripping any
  `Trusted_Connection`).
- **Current pygim.** `acquire_datastore(conn_str, …)` only accepts a connection string;
  the only connect attribute it sets is `SQL_COPT_SS_BCP`. There is no way to pass an
  access token or an `attrs_before`-style map, so token auth cannot work through it —
  this is the single reason the store is limited to local, credentialed connections.
- **Requested change.** Add a first-class token path, e.g.:
  ```python
  acquire_datastore(conn_str, *, access_token: bytes | None = None, ...)
  # or a callable invoked per physical connect (tokens expire; pooled
  # connections outlive them):
  acquire_datastore(conn_str, *, pre_connect: Callable[[], Mapping[int, bytes]] | None = None)
  ```
  Internally set the token via `SQLSetConnectAttr(SQL_COPT_SS_ACCESS_TOKEN, …)` before
  `SQLDriverConnect`, mirroring the pyodbc `attrs_before` contract. A callable is
  preferable to a static token because tokens expire while pooled connections persist.

#### 2.2 Caller-owned transaction / connection handle
- **Need.** A single logical write often spans **multiple tables that must commit or
  roll back together**, inside one transaction the caller controls.
- **Current pygim.** `DataStore.save` checks out its **own** pooled connection and BCP
  commits on batch boundaries (`bcp_batch`/`bcp_done`), independent of any caller
  transaction. Cross-table atomicity is therefore lost, and a partial failure can leave
  one table written and another not.
- **Requested change.** Let the caller own the transaction boundary. Either:
  1. accept an externally-created connection/handle: `store.save(df, table, *, connection=<handle>)`, or
  2. expose explicit `begin()` / `commit()` / `rollback()` on a session object that
     multiple `save`/`load` calls share, so N writes commit once.
  Minimum viable: a documented "single-connection, deferred-commit" mode so a batch of
  saves is one commit.

#### 2.3 Upsert / MERGE (not just INSERT)
- **Need.** Many targets are **upserts**, not appends — keyed writes that update
  existing rows and insert new ones. A common workaround today is a batched primary-key
  `SELECT` + set-difference + separate bulk update/insert. Some insert-only targets must
  also **skip already-present keys** (dedup).
- **Current pygim.** `save` is BCP **insert-only**; there is no merge/upsert and no
  server-side dedup, so keyed writes cannot use it at all.
- **Requested change.** Add an upsert mode built on the fast BCP path, e.g.
  `store.save(df, table, *, mode="upsert", key=[...])` implemented as *BCP into a
  temp/staging table → `MERGE` on the key → drop staging*. Expose the merge key and,
  optionally, an `insert_if_not_exists` (anti-join) variant. This is the highest-value
  correctness feature after auth: it lets the slowest keyed writes move to BCP.

### P1 — Correctness, safety, and observability at the repository boundary

#### 2.4 Schema validation with per-column diagnostics
- **Need.** A repository hands `save` a frame whose columns/dtypes are meant to match
  the table. When they *don't* (a missing/extra column, or a NULL in a `NOT NULL`
  column), the caller wants to fail fast with a message **naming the offending
  column(s)/row(s)** before the write.
- **Current pygim.** A schema/constraint mismatch surfaces as an opaque `GimError`
  (an ODBC error, e.g. SQL Server 515) raised from deep inside the BCP pipeline, with no
  column/row context.
- **Requested change.** Before BCP, validate the DataFrame schema against the target
  table's `INFORMATION_SCHEMA` (names, nullability, types) and raise a structured error
  listing the mismatched columns. Optionally expose `store.describe(table) -> schema` so
  callers can pre-flight-check themselves.

#### 2.5 Typed error hierarchy (integrity vs transient)
- **Need.** Data-access layers distinguish **IntegrityError** (duplicate key →
  skip/dedup) from **OperationalError** (transient connection loss → retry) to drive
  retry/skip policy.
- **Current pygim.** Everything collapses to `GimError(RuntimeError)`.
- **Requested change.** Map ODBC `SQLSTATE` classes to a small typed hierarchy —
  `DataStoreIntegrityError`, `DataStoreTransientError`, `DataStoreDataError` — all under
  `GimError`, carrying the `SQLSTATE` and native error code.

#### 2.6 Empty-frame is a no-op
- **Need.** Callers routinely guard `if df.is_empty(): return`. A DataStore that errors
  (or opens a BCP session) on zero rows forces every caller to keep that guard.
- **Current pygim.** Behaviour on an empty frame is unspecified.
- **Requested change.** Define `save(empty_df, table)` as a no-op returning
  zero-row metrics; never open a BCP session for zero rows.

#### 2.7 Predicate-safe, parameter-bound loads
- **Need.** Real loads are **filtered**, not full-table scans — equality predicates,
  date ranges, `IN (...)` id lists. Pushing those predicates down is the point of a fast
  loader.
- **Current pygim.** `Query` stores `where` as a **raw string** and `load(source)` treats
  any string containing a space as raw SQL. There is no parameter binding, so callers
  must string-concatenate values — an injection and correctness hazard that also defeats
  plan caching.
- **Requested change.** Add bound parameters to `Query` / `load`, e.g.
  `Query().from_table("t").where("status = ?", ["Approved"]).where_in("id", ids)`,
  rendered with ODBC parameter markers.

#### 2.8 Stable metrics contract + type stubs
- **Need.** Callers logging/telemetry want a stable, documented shape for what `save`
  returns, and static typing wants the extension typed.
- **Current pygim.** `save` returns an undocumented metrics `dict`; there is no `.pyi`,
  so any code wrapping the `DataStore` is untyped at the boundary.
- **Requested change.** Publish a documented metrics schema (rows, batches, per-phase
  seconds, bytes) — ideally a typed object — and ship a `pygim/persistence.pyi` (or
  inline stubs) covering `DataStore.save/load`, `acquire_datastore`, and `Format`.

### P2 — Ergonomics and delivery

#### 2.9 Accept a SQLAlchemy URL (or ship the translator)
- Callers that keep connection details in the SQLAlchemy URL form (`mssql+pyodbc://…`)
  must translate to a raw ODBC DSN before calling `acquire_datastore` (e.g. via the
  pyodbc dialect's `create_connect_args`). Accepting either a raw DSN **or** a URL — or
  shipping a documented translator — removes that per-caller step.

#### 2.10 Schema-qualified table names + identifier quoting
- Table names are often unqualified (`t`), resolved against the connection's default
  schema. Document and test how `save`/`load` resolve `table` vs `schema.table` vs
  `[db].[schema].[table]`, and quote identifiers consistently (the dialect already has
  `quote_identifier`).

#### 2.11 `replace`/truncate + delete-by-predicate
- A common "replace" pattern is delete-all (or delete-by-predicate) then insert, in one
  transaction. A `store.truncate(table)` / `store.delete(table, where=…)` on the same
  caller-owned connection (see 2.2) would let "replace" run atomically through the
  `DataStore`.

#### 2.12 Packaging for CI (installable wheel)
- **Need.** Because the extension isn't distributed as a normal wheel, downstream
  projects can't add it as a dependency or exercise it in CI — it must be "assume on
  PATH", which leaves the write/load paths untested end-to-end.
- **Requested change.** Publish a prebuilt `manylinux` wheel (pinned Arrow/ODBC) so
  consumers can install it (e.g. as an optional extra) and run integration tests against
  a containerised SQL Server.

---

## 3. Priority summary

| # | Improvement | Priority | Unblocks |
|---|---|---|---|
| 2.1 | MI / access-token auth | P0 | Any cloud / non-local environment |
| 2.2 | Caller-owned transaction | P0 | Multi-table atomicity |
| 2.3 | Upsert / MERGE | P0 | Keyed / upsert writes |
| 2.4 | Schema validation w/ diagnostics | P1 | Safe adoption; fail-fast parity |
| 2.5 | Typed error hierarchy | P1 | Retry/skip policies |
| 2.6 | Empty-frame no-op | P1 | Drop per-caller guards |
| 2.7 | Parameter-bound loads | P1 | Filtered loads on the DataStore |
| 2.8 | Metrics contract + stubs | P1 | Telemetry + static typing |
| 2.9 | Accept SQLAlchemy URL | P2 | Fewer per-caller steps |
| 2.10 | Schema-qualified names | P2 | Correct multi-schema targeting |
| 2.11 | replace/truncate/delete | P2 | Atomic "replace" semantics |
| 2.12 | Installable CI wheel | P2 | End-to-end test coverage |

---

## 4. Reference — verified pygim behaviour this is based on

Grounded in the pygim source (`src/_pygim_fast/persistence/`) and the Python surface
`src/pygim/persistence.py`:

- Public API: `acquire_datastore(conn_str, format="polars", pool_size=4,
  batch_size=100000, table_hint="TABLOCK", bcp_workers=1, block_size=4096,
  packet_size=16384)` → `DataStore` with `save(data, table_name, bcp_workers=-1)`,
  `load(source|Query, load_workers=1, partition_column="")`, `add_pre_transform`,
  `add_post_transform`, `clear_transforms` (`adapter/bindings.cpp`).
- `save` is BCP bulk **insert** only, committing via `bcp_batch`/`bcp_done`
  (`strategy/mssql/bcp/bcp_pipeline.h`); no MERGE/upsert/delete/truncate.
- The only connect attribute set is `SQL_COPT_SS_BCP`; no
  `SQL_COPT_SS_ACCESS_TOKEN` / `attrs_before` (`strategy/mssql/backend.h`).
- `Query` stores `where` as a raw string; no bound parameters (`core/query.h`).
- Errors surface as `GimError` (a `RuntimeError` subclass) (`adapter/bindings.cpp`).
- Connection string is a raw ODBC DSN, not a SQLAlchemy URL.
