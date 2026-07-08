Changelog
=========

.. NOTE FOR CONTRIBUTORS --------------------------------------------------
   - Keep newest entries at the top.
   - Group changes under: Added / Changed / Fixed / Removed / Performance / Docs.
   - Use past tense or imperative consistently (here: imperative mood, e.g. "Add", "Fix").
   - Reference PR numbers in parentheses when available, e.g. (PR #4).
   - Do not list trivial internal refactors unless they affect public API, performance, or developer workflow.
   - For unreleased work, accumulate under "Unreleased"; move to a versioned heading when bumping __version__.

Unreleased
----------
Added
~~~~~
- Persistence: Typed error hierarchy under ``GimError`` — ``DataStoreIntegrityError`` (constraint violations; skip/dedup), ``DataStoreTransientError`` (connection loss, deadlock victim, timeouts; retry), ``DataStoreDataError`` (conversion/truncation). Classified from the ODBC SQLSTATE class; exceptions carry ``sqlstate`` and ``native_error`` attributes.
- Persistence: Pre-save schema validation — every ``save()`` checks the frame against the target's ``sys.columns`` catalog and fails fast with one structured error naming all offending columns (unknown columns, NULLs headed into NOT NULL, required columns missing without defaults, cross-family type mismatches, nonexistent tables). ``DataStore.describe(table)`` exposes the same catalog (name/type/nullability/identity/computed/default per column).
- Persistence: Parameter-bound loads — ``Query.where("status = ?", ["Approved"])`` (repeated calls AND-combine), ``Query.where_in("id", ids)`` (empty list matches nothing), and ``load(sql, params=[...])`` on stores and sessions. Values bind through ODBC parameters (strings as UTF-16), never string concatenation. ``Query`` is now exported from the production module.
- Persistence: ``DataStore.truncate(table)`` and ``DataStore.delete(table, where=..., params=...)`` (returns affected rows), also on sessions — an atomic replace is truncate + save inside one session transaction (rollback verified live).
- Persistence: ``acquire_datastore`` accepts SQLAlchemy-style URLs (``mssql+pyodbc://user:pass@host:port/db?driver=...`` and the ``odbc_connect=`` form) alongside raw ODBC DSNs; translation is internal, no SQLAlchemy dependency.
- Persistence: Three-part table names (``database.schema.table``) accepted and quoted part-by-part across save/load/keyed writes/maintenance.
- Persistence: Type stubs — ``pygim/persistence.pyi`` documents the stable API including the ``SaveMetrics`` schema; ``py.typed`` (partial) marker shipped in wheels.
- Packaging: cibuildwheel now builds the persistence extension into Linux/macOS wheels (unixODBC headers installed in the build image; ``libarrow``/``libparquet`` deliberately NOT vendored — the extension links the user's pyarrow via rpath; ``libodbc`` is vendored, drivers still resolve via the system ``odbcinst.ini``).

Fixed
~~~~~
- Persistence/BCP: Fix silent data loss on constraint violations — ``bcp_batch``/``bcp_done`` return rows COMMITTED, and rejected rows (e.g. duplicate keys, error 2627) were silently dropped while ``save()`` reported success. The pipeline now tracks committed counts and raises ``DataStoreIntegrityError`` on any shortfall, on both single-connection and parallel paths.
- Persistence: Parallel loads (``load_workers > 1``) silently DROPPED the query's WHERE/columns/limit — the range-partitioned path builds its own SQL. Filtered or parameterized queries now fall back to single-worker and honor the predicate; only plain full-table scans partition.
- Persistence/BCP: Plain ``save()`` (append) bound the frame to table columns by position, so a frame with correct names in the wrong order — or omitting an IDENTITY column — silently wrote values into the wrong columns. Append now validates positional correspondence and fails fast; use ``mode="upsert"``/``"insert_missing"`` (MERGE by name) for partial or reordered frames.
- Persistence/BCP: A unique index declared ``WITH (IGNORE_DUP_KEY = ON)`` drops duplicate rows by design; the commit-shortfall check now recognizes that case (message 3604) and does not raise for it.
- Persistence/BCP: Dismiss the BCP session guard only after ``finalize_bcp`` — a failing final ``bcp_batch`` previously left the connection with a BCP operation in flight, poisoning it for every later statement (HY010).

Added
~~~~~
- Persistence: Entra ID / Managed Identity authentication via ``acquire_datastore(..., access_token=...)`` (keyword-only). Accepts the raw token as ``str``/``bytes`` or a zero-argument callable invoked per physical connect (short-lived tokens keep working as pooled connections are created); the ``SQL_COPT_SS_ACCESS_TOKEN`` packing is done internally and the packed buffer lives on the connection for its whole lifetime, per driver requirements. Conflicting ``UID``/``PWD``/``Trusted_Connection``/``Authentication`` keywords and pre-packed pyodbc-style tokens are rejected eagerly. Currently requires ``bcp_workers=1``/``load_workers=1``.
- Persistence: Caller-owned transactions via ``DataStore.session()`` → ``DataStoreSession``. One pooled connection with autocommit off; every ``save``/``load`` through the session shares one transaction finished by ``commit()``/``rollback()``, making multi-table writes atomic (verified on SQL Server 2022: BCP rows, including multi-batch saves, fully participate in the manual-commit transaction; session saves additionally suppress mid-save batch commits). Context-manager form commits on clean exit, rolls back on exception, then closes. Cleanup failures discard the connection instead of repooling it.
- Persistence: Keyed writes — ``DataStore.save(df, table, mode="upsert"|"insert_missing", keys=[...])`` (keyword-only). BCP-stages into a session-local temp table, then one atomic ``MERGE WITH (HOLDLOCK)`` (upsert) or anti-join ``INSERT`` (insert_missing); metrics gain ``affected_rows``. Duplicate merge-key values in the frame fail fast server-side; NULL or missing key columns fail before any connection; IDENTITY targets are handled via ``IDENTITY_INSERT`` automatically. Composes with sessions.
- Persistence: ``save()`` on an empty frame is a defined no-op — zero-row metrics, no BCP session, no connection checkout.
- Persistence: ``ConnectionPool`` gained a pluggable connect function, a discard path for suspect connections, and no longer leaks a pool slot when a connect attempt throws.

Changed
~~~~~~~
- Persistence: ``Query.where()`` now AND-combines with any existing predicate instead of replacing it (repeated calls conjoin); callers that relied on ``where()`` overwriting must build a fresh ``Query``.
- Wiring: Group internal registry, factory, and IoC native modules under ``src/_pygim_fast/wiring/`` while keeping public module names stable (``pygim.registry``, ``pygim.factory``, ``pygim.ioc``).
- Wiring: Factor shared pybind adapter validation helpers into ``src/_pygim_fast/wiring/common/`` for reuse across wiring modules.
- Build: Move native extension ``ext.*.toml`` manifests next to their corresponding module sources and resolve manifest ``sources`` relative to each TOML file.
- Build: Prefer stdlib ``tomllib`` for setup metadata parsing, with the ``tomli`` backport only for Python < 3.11 builders.
- IoC: Validate resolved instances against the registered interface/protocol after provider construction and decorator application.
- IoC: Add opt-in constructor autowiring for class providers using Python type hints; unresolved typed parameters fall back to default values when present.
- IoC: Move autowiring policy, lifecycle parsing, and the decorator/validation sequence from the pybind adapter into the pybind-free core. The adapter now only introspects constructors into neutral ``ParamSpec`` records and executes the plan produced by ``core::plan_autowiring`` (itself ``constexpr`` and verified with compile-time ``static_assert`` tests).
- IoC: Cache constructor introspection per registration (shared ``AutowireSlot``); autowired resolves no longer re-run ``inspect.signature``/``get_type_hints`` each time. Provider availability is still re-evaluated per resolve, so dependencies registered later are picked up.
- IoC: Registration and resolution errors now name the offending key, e.g. ``No provider for key [key: Repository, name='cached']``; nested resolves append their frames so the message reads as the resolution chain.
- IoC: ``register()`` validates at registration time that the interface is a class or protocol (previously a bad interface only failed at resolve, inside ``isinstance``).
- IoC: ``ServiceDescriptor`` is now an explicitly read-only snapshot; ``describe()`` returns a copy, so the previous writable fields silently discarded mutations.
- IoC: Document thread-safety expectations on ``Container`` (GIL-based consistency; concurrent first-resolves of a singleton whose provider releases the GIL can race).
- Build: Upgrade base C++ standard from C++20 to C++23 for all platforms (GCC, Clang, MSVC).
- Build: Set ``MACOSX_DEPLOYMENT_TARGET`` default to 13.3 in ``setup.py`` (required for ``std::format`` and ``std::to_chars`` with floating-point).
- CI: Update ``MACOSX_DEPLOYMENT_TARGET`` from 10.15 to 13.3 in ``python-packages.yml``.
- CI: Drop Python 3.8 (EOL); add Python 3.14 to test matrix and cibuildwheel build targets.
- Persistence: Rename module from ``repository`` to ``persistence``. Folder ``src/_pygim_fast/repository/`` → ``persistence/``, Python module ``pygim.repository`` → ``pygim.persistence``, C++ extension ``_repository`` → ``_persistence``. C++ class names (``Repository``, ``RepositoryAdapter``) and DDD protocol names unchanged.
- Persistence: Rename Python-facing class from ``Repository`` to ``DataStore``. Aligns naming with actual DAO/Table Gateway semantics; DDD ``Repository`` protocol remains in ``interfaces.py``.
- Persistence: Rename ``acquire_repo`` to ``acquire_datastore``.
- DDD interfaces: Convert all 17 ABC-based interfaces to ``@runtime_checkable`` Protocols. Remove ``I`` prefix (e.g., ``IEntity`` → ``Entity``). Drop unused ``DomainEventType`` enum.
- Each module: Rename ``each.h`` to ``adapter.h`` to match core/adapter convention (module is inherently pybind11-dependent).
- PathSet: Encapsulate ``m_paths`` (moved from ``public`` to ``private``); iterator access via ``begin()``/``end()``.

Fixed
~~~~~
- PathSet: Fix interpreter crash when filtering: ``ext()`` captured a dangling ``string_view`` and ``Query`` held a non-owning pointer to a source ``PathSet`` that Python could garbage-collect before evaluation. The filter now owns its extension string and the ``&``/``|`` bindings keep the source alive (``py::keep_alive``).
- PathSet: Fix ``__add__`` discarding the left operand; ``a + b`` now returns the union of both path sets.
- Each: Accessing an attribute missing from any element now raises ``AttributeError`` immediately, per the Proxy's documented contract; previously the exception *instances* were silently collected into the result list.
- Each: Fix the descriptor form (``each = each()``) rejecting every ordinary class; ``__set_name__`` checked whether the class *object* was iterable instead of whether it defines ``__iter__`` for its instances.
- Registry: Restore ``[[no_unique_address]]`` on the hooks policy member, dropped incidentally during the wiring move.
- IoC: Fix interpreter crash (use-after-free) when a provider or decorator registered new services during ``resolve()``; the registry vector could reallocate under a live descriptor reference. ``resolve()`` now works on a descriptor copy, and a generation guard prevents caching a singleton for a registration overridden mid-resolve.
- IoC: Fix interpreter crash (stack overflow) on circular autowired dependencies; resolution now tracks an in-progress stack and raises ``RuntimeError: Circular dependency detected`` with the key chain.
- IoC: Fix dangling container reference held by the decorator form of ``register()``; the returned decorator now keeps the container alive and remains reusable (decorators are no longer moved-from on first use).
- Fix macOS compilation failure caused by ``std::format`` with floating-point requiring ``std::to_chars`` (unavailable below macOS 13.3).
- Fix Windows (MSVC) compilation failure: replace GCC-only ``__builtin_unreachable()`` with C++23 ``std::unreachable()`` in ``datagen/core.h``.
- Fix ``_cli_app.py`` ``NameError``: ``PathSet`` was used but never imported. Rewrite ``clean_up()`` to use ``pathlib.Path.rglob()`` and ``shutil.rmtree()`` (old PathSet API removed).
- Fix ``testing.py`` dead ``pytest_args`` parameter: reversed ``or`` operands so caller-supplied args take precedence.
- Fix ``interfaces.py`` inverted import: internal module was importing from public ``pygim.core.explib``.
- Fix ``pygim/__init__.py`` overly broad ``except Exception:`` → ``except (ImportError, ModuleNotFoundError):``.
- Fix ``_error_msgs.py`` stray ``]`` in type-is-type f-string branch.
- Fix ``_typing.py`` re-exporting entire ``typing``/``typing_extensions`` ``__all__`` into package namespace.

Removed
~~~~~~~
- Remove 7 dead ``x_test_*`` functions from ``test_pathset.py`` (tested APIs that no longer exist: ``prefixed``, ``dirs``, ``files``, ``by_suffix``, ``drop``, ``dropped``, ``FS.delete_all``).
- Remove ``DomainEventType`` enum (empty, unused) from interfaces.

Added
~~~~~
- IoC: Add ``pygim.ioc.Container`` with transient/singleton lifecycles, named registrations, decorator application, and strict override semantics implemented with the same core/adapter/bindings pattern as registry and factory.
- Examples: Add runnable IoC container example under ``docs/examples/ioc/``.
- Examples: Add runnable IoC autowiring example under ``docs/examples/ioc/``.
- Examples: Add runnable IoC test-override example under ``docs/examples/ioc/`` showing fake substitution and singleton cache invalidation.
- Examples: Add runnable Factory examples under ``docs/examples/factory/`` (basic named creators, interface enforcement).
- Examples: Add runnable ``each`` broadcasting example under ``docs/examples/each/``.
- Examples: Add runnable ``PathSet`` example under ``docs/examples/pathset/``.
- Examples: Add ``docs/examples/README.md`` index; rewrite all examples in a narrative, self-verifying teaching style.
- Tests: Add a live MSSQL persistence round-trip test that auto-skips unless ``STRESS_CONN`` is reachable or the local Docker SQL Server on ``localhost:1433`` is available.
- Initial CHANGELOG with retroactive notes for registry enhancement work.
- Registry: Decorator-based registration via ``@registry.register(key, override=False)``.
- Registry: ``registered_keys()`` introspection method for current logical ids.
- Registry: ``find_id(obj)`` fast reverse lookup (qualname policy only).
- Registry: Optional ``capacity`` constructor arg for upfront map reservation.
- Registry: Explicit ``post(key, value)`` trigger to manually invoke post hooks.
- Examples: Two runnable registry examples under ``docs/examples/registry/`` (basic & hooks).
- Added dedicated implementation folders for wiring modules under ``src/_pygim_fast/wiring/{registry,factory,ioc}/``.
- Added PlantUML architecture reference ``docs/design/core_adapter_bindings_convention.puml`` for core/adapter/bindings layering.
- CI: Add release workflow that builds wheels via ``cibuildwheel`` and publishes tagged releases to PyPI.
- CI: Auto-tag main whenever the ``dev`` branch is merged, driven by PR labels ``release:major``/``release:minor``/default patch.
- Added native C++ ``QuickTimer`` utility (`src/_pygim_fast/utils/quick_timer.h`) with ordered subtimers, immediate stop reporting, and destructor summary output.
- Benchmark: Consolidated ``benchmarks/bcp_throughput.py`` into a multi-profile benchmark with simple (7 cols), mixed (9 cols), and complex (11 cols) dataset profiles. Supports ``--dataset all`` for side-by-side comparison and ``--compare-strategies`` for row_major vs column_major matrix runs.
- Benchmark: Regression gate — ``--save-baseline FILE`` and ``--check-regression FILE`` in ``benchmarks/bcp_throughput.py``. Saves/compares per-profile MB/s against a JSON baseline. ``--regression-threshold PCT`` (default 15%) controls sensitivity. Exits 1 on failure for CI integration.
- Persistence/MSSQL BCP: Parallel BCP with ``bcp_workers=N`` parameter on ``persist_dataframe()``. Creates N independent ODBC connections, partitions Arrow RecordBatches by row count, and runs worker threads in parallel. ``bcp_workers=0`` (default) uses single-connection. Falls back to single-connection when batch count < workers.
- Persistence/MSSQL BCP: ``BcpConnectionPool`` (``bcp_connection_pool.h``) — RAII pool of M pre-connected ODBC handles with BCP enabled. Exception-safe constructor with rollback. Used per parallel persist call.
- Benchmark: ``--workers N`` CLI argument in ``benchmarks/bcp_throughput.py`` for parallel BCP benchmarking.
- Tests: ``test_datastore_satisfies_repository_protocol`` verifying ``DataStore`` satisfies the ``Repository`` protocol via ``isinstance``.
- Tests: ``test_public_module_reexports`` validating ``pygim.persistence`` re-exports ``DataStore``, ``acquire_datastore``, ``Format``.

Changed
~~~~~~~
- Persistence: Migrated from monolithic ``mssql_strategy.cpp`` / ``repository_v2`` to a layered core/adapter/strategy architecture. ``Repository<Backend>`` generic facade, ``ConnectionPool<Backend>`` with ``std::expected`` checkout, ``RepositoryAdapter<Backend>`` one-hop pybind11 boundary, ``BackendPolicy`` C++20 concept, and ``MssqlBackend`` concrete backend. All template instantiation at the pybind11 edge (``bindings.cpp``).
- Persistence: Added ``connection_pool.h`` (thread-safe bounded pool with RAII ``ConnectionHandle``), ``backend_policy.h`` (C++20 concept), ``query.h``/``dialect.h`` (fluent query builder + dialect rendering), ``arrow_import.h`` (PyCapsule ``__arrow_c_stream__`` + depth-limited fallback import), and ``bindings.cpp`` (``acquire_repo`` factory with pool_size/batch_size/bcp_workers params).
- Persistence/MSSQL BCP: Column-major AVX2 path is now strictly opt-in: only enabled if ``PYGIM_FORCE_SIMD=avx2``. All hardware auto-detection is removed; scalar is the default.
- Persistence/MSSQL BCP: Profile-aware activation: AVX2 is only enabled if ``plan_avx2_blocks`` finds at least 2 eligible blocks. Otherwise, scalar path is used to avoid unnecessary vector overhead.
- Persistence/MSSQL BCP: Eliminated per-row ``bcp_colptr`` redirect loop in column-major path — replaced N per-column ODBC calls per row with a single ``memcpy`` from the pre-filled mini-batch buffer into the original staging buffer. Reduces redirect overhead from ~500 ms to ~24 ms for 1 M rows (exhaustive profile).
- Persistence/MSSQL BCP: Promoted micro-metrics (fixed-copy, colptr-redirect, string-pack, sendrow) from hot-only to stage-level timing — always collected when any timing is enabled (~1 % overhead on 1 M rows). Makes per-component breakdown visible in default benchmark runs.
- Persistence/MSSQL BCP: DRY refactor of BcpMetrics→Python dict builder in ``repository.h`` — extracted lambda eliminates duplicated 18-field dict construction across RowMajor/ColumnMajor strategy casts.
- Persistence/MSSQL BCP: Column-major AVX2 transpose now precomputes eligible contiguous 8x4-byte blocks once per run and reuses the per-column copied marker buffer across mini-batches, removing repeated block eligibility scans and hot-loop allocations.
- Persistence/MSSQL BCP: Added AVX2 4x4 transpose support for contiguous 8-byte fixed-width columns (int64/uint64/double/duration) behind ``PYGIM_ENABLE_AVX2_8B=1`` for controlled validation; default AVX2 path remains 4-byte-block optimized.
- Packaging: Made ``tabulate>=0.9`` a required runtime dependency (no fallback formatter in ``benchmarks/bcp_throughput.py``).
- Persistence/MSSQL BCP: Added row-loop micro-metrics (fixed-copy, colptr redirect, string packing, sendrow) and exposed them via ``persist_dataframe(...)['bcp_metrics']`` for benchmark analysis.
- Utils/Timing: Added structured ``QuickTimer`` reporting API (snapshot with total + subtimers) and switched BCP metric extraction to consume ``timer.report()`` instead of repeated ad-hoc lookups.
- Build: Raised minimum arrow-cpp build dependency to >= 15 (tested at 23.0.1). Enforced at compile time via ``static_assert`` in ``bcp_types.h`` — builds against arrow-cpp < 15 fail with an explicit message directing the user to ``conda install -c conda-forge 'arrow-cpp>=15' 'pyarrow>=15'``. Removed the ``PYGIM_HAVE_ARROW_STRING_VIEW`` compile-time gate; ``bind_string_view`` is now unconditionally compiled.
- Build: Removed ``PYGIM_HAVE_ODBC`` and ``PYGIM_HAVE_ARROW`` compile-time feature flags. ODBC and Arrow C++ are now mandatory build dependencies (fail-fast philosophy).
- Build: Removed dependency probing from ``setup.py``; compilation fails directly if headers/libraries are missing.
- C++: Removed all ``#if PYGIM_HAVE_ODBC`` / ``#if PYGIM_HAVE_ARROW`` conditional compilation guards from 8+ source files.
- Python: Removed ``HAVE_ODBC`` / ``HAVE_ARROW`` module-level attributes from ``repository_v2`` and ``mssql_strategy`` pybind modules.
- CLI: Simplified ``show_support()`` to report extension importability only, without checking feature flag attributes.
- Registry: Override path optimized to single unordered_map probe (eliminated double lookup).
- Registry: ``__repr__`` now includes ``policy``, ``hooks`` flag, and current ``size``.
- Docs: Expanded ``_pygim_fast/registry.h`` with architectural overview & guidance.
- Project instructions updated to reflect new registry surface area (PR #4).
- Refactored factory and registry internals into explicit ``core`` (pybind-free) and ``adapter`` (pybind boundary) headers.
- Renamed pybind module translation units to ``bindings.cpp`` and updated build naming logic so modules remain ``pygim.factory`` and ``pygim.registry``.
- Persistence/MSSQL: Simplified native ``persist_dataframe`` Arrow path to prefer DataFrame ``__arrow_c_stream__`` for direct native ingestion, with IPC serialization (``write_ipc``) as fallback, removing Python-side Arrow orchestration from the hot path.
- Persistence/MSSQL: Process Arrow input batch-by-batch in BCP ingestion to avoid full-table materialization and preserve correctness for multi-batch data.
- Playground stress harness: Add explicit ``--arrow`` CLI flag (mutually exclusive with ``--no-arrow``) to set ``PYGIM_ENABLE_ARROW_BCP`` for reproducible Arrow-path runs.
- Persistence/MSSQL: Split ``persist_dataframe`` orchestration helpers into dedicated detail translation units (Arrow strategies vs bulk-upsert/result shaping) to keep ``mssql_strategy.cpp`` focused on pybind bindings.
- Persistence/MSSQL: Introduced OOP-style ``persist_dataframe`` orchestration with lightweight request/view objects and path-specific classes (Arrow path vs bulk-upsert path), delegating pybind lambda control flow to a dedicated orchestrator.
- Persistence: Introduced ``ExtractionPolicy`` as the single, explicit point of ``py::object`` inspection for bulk data — Arrow C stream, Polars, and Python iterables all convert here to a ``DataView`` before reaching any strategy.
- Persistence: Introduced ``DataView = std::variant<ArrowView, TypedBatchView>`` to replace per-method data passing; ``ArrowView`` carries a zero-copy ``RecordBatchReader``, ``TypedBatchView`` carries a column-major ``TypedColumnBatch``.
- Persistence: Collapsed ``bulk_insert``, ``bulk_upsert``, and ``persist_arrow`` virtual methods on ``Strategy`` into a single ``persist(TableSpec, DataView, PersistOptions)``; ``PersistMode`` enum (Insert/Upsert) and ``PersistOptions`` carry all per-call parameters.
- Persistence: Updated ``StrategyCapabilities`` from five flags to three (``can_fetch``, ``can_save``, ``can_persist``); bulk-level granularity was premature given the single-strategy-per-repo invariant.
- Docs: Updated abstract PlantUML diagrams (architecture + sequence) to represent ideal end-state for all backlog phases (Phase 1–5 + M), including pipeline internals, SchemaCache, BufferPool, and MeasurementHarness.
- Persistence: ``persist_dataframe`` gains ``bcp_batch_size=0`` parameter; when 0 (default), BCP uses its 100 000-row commit default; pass an explicit value to bound memory per batch on very wide or high-cardinality datasets.
- C++23 modernization: replace ``std::string`` concatenation with ``std::format`` across ``repository.h``, ``adapter.h``, ``sql_helpers.h``, ``odbc_error.h``, ``dialect.h``.
- C++23 modernization: replace ``std::all_of`` with ``std::ranges::all_of`` in ``sql_helpers.h``.

Fixed
~~~~~- Repository/MSSQL BCP: Fix parallel BCP never activating — Polars exports the entire DataFrame as a single ``RecordBatch``, so ``max_workers`` was clamped to ``min(hw_concurrency, 1) = 1`` and always fell back to single-connection. Fix: slice large ``RecordBatch``es into N sub-batches (zero-copy via ``RecordBatch::Slice``) before partitioning across workers. Additionally fix ``batch_flush_seconds`` metric aggregation from ``+=`` (sum) to ``std::max`` (wall-clock) for consistent parallel reporting.- Repository: Fix BCP commit-frequency regression — ``persist_dataframe`` was passing ``batch_size=1000`` directly to BCP, causing 1 000 ``bcp_batch()`` server-side commits for a 1 M-row load; MERGE used the same parameter but runs all batches inside a single transaction (one commit), so BCP was paradoxically slower (2.88 MB/s) than MERGE (5.98 MB/s). Root cause was a units mismatch: ``batch_size`` on the MERGE path caps SQL parameter count per statement, while on the BCP path it sets the commit frequency. Fix: add separate ``bcp_batch_size=0`` parameter to ``persist_dataframe``; when 0, BCP uses its internal 100 000-row default (10 commits for 1 M rows instead of 1 000), restoring expected throughput ordering.
- Persistence: Remove ``to_arrow(compat_level=oldest)`` compat materialization path from ``ExtractionPolicy`` — superseded by the Arrow C++ >= 15 build requirement. ``ImportRecordBatchReader`` and ``bind_string_view`` now accept Polars 1.x ``StringView`` (``"vu"`` format) natively; no Python-side IPC round-trip needed.
- Tests: Ensured override semantics correctly raise when ``override=True`` and key is missing.
- Added edge-case tests for factory missing getitem/override behavior and registry key tuple validation + ``find_id`` variant fallback.
- Persistence/MSSQL Arrow BCP: Fix variable-length text/date/timestamp binding requirements (terminator metadata) and per-row fixed-width column pointer binding to prevent fallback/duplicate-row insertion behavior.
- Persistence/MSSQL Arrow persist: Added c-stream compatibility bridge using Arrow reader ``_export_to_c`` when table-level ``__arrow_c_stream__`` is unavailable, enabling ``arrow_c_stream_bcp`` on environments that previously fell back to IPC.
- Fix ``pygim/__init__.py`` unconditional ``__all__`` causing ``NameError`` when repository extension is not installed. ``DataStore``/``acquire_datastore`` now conditionally included.
- Fix ``arrow_export.h`` raw ``new``/``delete`` pattern for ``ArrowArrayStream``. Replaced with ``std::unique_ptr`` + custom deleter for exception-safe RAII cleanup.
- Fix ``BackendPolicy`` concept underspecification: ``connect()`` now requires 2-arg signature ``(string_view, int)`` matching actual ``ConnectionPool`` usage.
- Fix ``ArrowBuilder`` coupling to ODBC headers: replaced ``SQL_DATE_STRUCT``/``SQL_TIMESTAMP_STRUCT``/``SQL_SS_TIME2_STRUCT`` with portable ``detail::DateStruct``/``detail::TimestampStruct``/``detail::Time2Struct``. Layout verified via ``static_assert`` in strategy layer.
- Fix inconsistent exception types at Python boundary: register ``py::exception<std::runtime_error>`` as ``GimError`` (``RuntimeError`` subclass) in ``bindings.cpp``, aligning with project ``GimError`` hierarchy.
- Fix magic number ``256`` in ``bcp_bind_dispatch.h``: extracted to named constant ``kInitialStringBufSize``.

Performance
~~~~~~~~~~~
- Persistence/MSSQL BCP: Parallel BCP now achieves **65–78 MB/s** (4–16 workers) on 1 M rows × 11 columns vs 33 MB/s single-connection — a 2–2.4× throughput improvement. Docker SQL Server w/ tmpfs + delayed durability.
- Reduced overhead on override operations through consolidated probe.

Docs
~~~~
- Updated ``docs/design/repository_backlog.md``: Phase 2 (SIMD) closed out with findings — AVX2 proven non-beneficial (sendrow 85%+ of row_loop), Phase 3 (multi-threaded transpose) deprioritized with rationale, M.1/M.3 marked done.
- Updated design documents to reflect removal of compile-time feature flags (``arrow_bcp_status.md``, ``arrow_bcp_implementation.md``, architecture diagrams, performance analysis docs).
- Updated file path references across design docs from ``mssql_strategy_bcp.cpp`` to ``bcp/bcp_strategy.cpp``.
- Removed ``PYGIM_ENABLE_ARROW_BCP`` env gate from sequence diagram (no longer used).
- Added inline binding docstrings for new registry APIs.
- Added educational examples demonstrating hooks and override semantics.
- Expanded class- and method-level documentation in registry/factory core+adapter headers with explicit rationale, argument, return, and exception notes.
- Update PlantUML diagrams (class, abstract architecture, load sequence) to reflect ``BackendPolicy`` 2-arg connect, portable temporal structs, ``unique_ptr`` RAII export, and ``GimError`` bridge.
- Update ``repository_architecture.md`` with new subsections: Portable Temporal Structs (§4.7), RAII Arrow Export (§4.8), GimError Exception Bridge (§4.9).

0.0.1 (Initial)
---------------
- Project scaffolding.
- Basic extension modules layout.
- Initial registry & supporting infrastructure (pre-enhancement state).
