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
- Initial CHANGELOG with retroactive notes for registry enhancement work.
- Registry: Decorator-based registration via ``@registry.register(key, override=False)``.
- Registry: ``registered_keys()`` introspection method for current logical ids.
- Registry: ``find_id(obj)`` fast reverse lookup (qualname policy only).
- Registry: Optional ``capacity`` constructor arg for upfront map reservation.
- Registry: Explicit ``post(key, value)`` trigger to manually invoke post hooks.
- Examples: Two runnable registry examples under ``docs/examples/registry/`` (basic & hooks).
- Added dedicated implementation folders for factory and registry under ``src/_pygim_fast/{factory,registry}/``.
- Added PlantUML architecture reference ``docs/design/core_adapter_bindings_convention.puml`` for core/adapter/bindings layering.
- CI: Add release workflow that builds wheels via ``cibuildwheel`` and publishes tagged releases to PyPI.
- CI: Auto-tag main whenever the ``dev`` branch is merged, driven by PR labels ``release:major``/``release:minor``/default patch.
- Added native C++ ``QuickTimer`` utility (`src/_pygim_fast/utils/quick_timer.h`) with ordered subtimers, immediate stop reporting, and destructor summary output.
- Benchmark: Consolidated ``benchmarks/bcp_throughput.py`` into a multi-profile benchmark with simple (7 cols), mixed (9 cols), and complex (11 cols) dataset profiles. Supports ``--dataset all`` for side-by-side comparison and ``--compare-strategies`` for row_major vs column_major matrix runs.
- Repository/MSSQL BCP: Parallel BCP with ``bcp_workers=N`` parameter on ``persist_dataframe()``. Creates N independent ODBC connections, partitions Arrow RecordBatches by row count, and runs worker threads in parallel. ``bcp_workers=0`` (default) uses single-connection. Falls back to single-connection when batch count < workers.
- Repository/MSSQL BCP: ``BcpConnectionPool`` (``bcp_connection_pool.h``) — RAII pool of M pre-connected ODBC handles with BCP enabled. Exception-safe constructor with rollback. Used per parallel persist call.
- Benchmark: ``--workers N`` CLI argument in ``benchmarks/bcp_throughput.py`` for parallel BCP benchmarking.

Changed
~~~~~~~
- Repository/MSSQL BCP: Column-major AVX2 path is now strictly opt-in: only enabled if ``PYGIM_FORCE_SIMD=avx2``. All hardware auto-detection is removed; scalar is the default.
- Repository/MSSQL BCP: Profile-aware activation: AVX2 is only enabled if ``plan_avx2_blocks`` finds at least 2 eligible blocks. Otherwise, scalar path is used to avoid unnecessary vector overhead.
- Repository/MSSQL BCP: Eliminated per-row ``bcp_colptr`` redirect loop in column-major path — replaced N per-column ODBC calls per row with a single ``memcpy`` from the pre-filled mini-batch buffer into the original staging buffer. Reduces redirect overhead from ~500 ms to ~24 ms for 1 M rows (exhaustive profile).
- Repository/MSSQL BCP: Promoted micro-metrics (fixed-copy, colptr-redirect, string-pack, sendrow) from hot-only to stage-level timing — always collected when any timing is enabled (~1 % overhead on 1 M rows). Makes per-component breakdown visible in default benchmark runs.
- Repository/MSSQL BCP: DRY refactor of BcpMetrics→Python dict builder in ``repository.h`` — extracted lambda eliminates duplicated 18-field dict construction across RowMajor/ColumnMajor strategy casts.
- Repository/MSSQL BCP: Column-major AVX2 transpose now precomputes eligible contiguous 8x4-byte blocks once per run and reuses the per-column copied marker buffer across mini-batches, removing repeated block eligibility scans and hot-loop allocations.
- Repository/MSSQL BCP: Added AVX2 4x4 transpose support for contiguous 8-byte fixed-width columns (int64/uint64/double/duration) behind ``PYGIM_ENABLE_AVX2_8B=1`` for controlled validation; default AVX2 path remains 4-byte-block optimized.
- Packaging: Made ``tabulate>=0.9`` a required runtime dependency (no fallback formatter in ``benchmarks/bcp_throughput.py``).
- Repository/MSSQL BCP: Added row-loop micro-metrics (fixed-copy, colptr redirect, string packing, sendrow) and exposed them via ``persist_dataframe(...)['bcp_metrics']`` for benchmark analysis.
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
- Repository/MSSQL: Simplified native ``persist_dataframe`` Arrow path to prefer DataFrame ``__arrow_c_stream__`` for direct native ingestion, with IPC serialization (``write_ipc``) as fallback, removing Python-side Arrow orchestration from the hot path.
- Repository/MSSQL: Process Arrow input batch-by-batch in BCP ingestion to avoid full-table materialization and preserve correctness for multi-batch data.
- Playground stress harness: Add explicit ``--arrow`` CLI flag (mutually exclusive with ``--no-arrow``) to set ``PYGIM_ENABLE_ARROW_BCP`` for reproducible Arrow-path runs.
- Repository/MSSQL: Split ``persist_dataframe`` orchestration helpers into dedicated detail translation units (Arrow strategies vs bulk-upsert/result shaping) to keep ``mssql_strategy.cpp`` focused on pybind bindings.
- Repository/MSSQL: Introduced OOP-style ``persist_dataframe`` orchestration with lightweight request/view objects and path-specific classes (Arrow path vs bulk-upsert path), delegating pybind lambda control flow to a dedicated orchestrator.
- Repository: Introduced ``ExtractionPolicy`` as the single, explicit point of ``py::object`` inspection for bulk data — Arrow C stream, Polars, and Python iterables all convert here to a ``DataView`` before reaching any strategy.
- Repository: Introduced ``DataView = std::variant<ArrowView, TypedBatchView>`` to replace per-method data passing; ``ArrowView`` carries a zero-copy ``RecordBatchReader``, ``TypedBatchView`` carries a column-major ``TypedColumnBatch``.
- Repository: Collapsed ``bulk_insert``, ``bulk_upsert``, and ``persist_arrow`` virtual methods on ``Strategy`` into a single ``persist(TableSpec, DataView, PersistOptions)``; ``PersistMode`` enum (Insert/Upsert) and ``PersistOptions`` carry all per-call parameters.
- Repository: Updated ``StrategyCapabilities`` from five flags to three (``can_fetch``, ``can_save``, ``can_persist``); bulk-level granularity was premature given the single-strategy-per-repo invariant.
- Docs: Updated abstract PlantUML diagrams (architecture + sequence) to represent ideal end-state for all backlog phases (Phase 1–5 + M), including pipeline internals, SchemaCache, BufferPool, and MeasurementHarness.
- Repository: ``persist_dataframe`` gains ``bcp_batch_size=0`` parameter; when 0 (default), BCP uses its 100 000-row commit default; pass an explicit value to bound memory per batch on very wide or high-cardinality datasets.

Fixed
~~~~~- Repository/MSSQL BCP: Fix parallel BCP never activating — Polars exports the entire DataFrame as a single ``RecordBatch``, so ``max_workers`` was clamped to ``min(hw_concurrency, 1) = 1`` and always fell back to single-connection. Fix: slice large ``RecordBatch``es into N sub-batches (zero-copy via ``RecordBatch::Slice``) before partitioning across workers. Additionally fix ``batch_flush_seconds`` metric aggregation from ``+=`` (sum) to ``std::max`` (wall-clock) for consistent parallel reporting.- Repository: Fix BCP commit-frequency regression — ``persist_dataframe`` was passing ``batch_size=1000`` directly to BCP, causing 1 000 ``bcp_batch()`` server-side commits for a 1 M-row load; MERGE used the same parameter but runs all batches inside a single transaction (one commit), so BCP was paradoxically slower (2.88 MB/s) than MERGE (5.98 MB/s). Root cause was a units mismatch: ``batch_size`` on the MERGE path caps SQL parameter count per statement, while on the BCP path it sets the commit frequency. Fix: add separate ``bcp_batch_size=0`` parameter to ``persist_dataframe``; when 0, BCP uses its internal 100 000-row default (10 commits for 1 M rows instead of 1 000), restoring expected throughput ordering.
- Repository: Remove ``to_arrow(compat_level=oldest)`` compat materialization path from ``ExtractionPolicy`` — superseded by the Arrow C++ >= 15 build requirement. ``ImportRecordBatchReader`` and ``bind_string_view`` now accept Polars 1.x ``StringView`` (``"vu"`` format) natively; no Python-side IPC round-trip needed.
- Tests: Ensured override semantics correctly raise when ``override=True`` and key is missing.
- Added edge-case tests for factory missing getitem/override behavior and registry key tuple validation + ``find_id`` variant fallback.
- Repository/MSSQL Arrow BCP: Fix variable-length text/date/timestamp binding requirements (terminator metadata) and per-row fixed-width column pointer binding to prevent fallback/duplicate-row insertion behavior.
- Repository/MSSQL Arrow persist: Added c-stream compatibility bridge using Arrow reader ``_export_to_c`` when table-level ``__arrow_c_stream__`` is unavailable, enabling ``arrow_c_stream_bcp`` on environments that previously fell back to IPC.

Performance
~~~~~~~~~~~
- Repository/MSSQL BCP: Parallel BCP now achieves **65–78 MB/s** (4–16 workers) on 1 M rows × 11 columns vs 33 MB/s single-connection — a 2–2.4× throughput improvement. Docker SQL Server w/ tmpfs + delayed durability.
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

0.0.1 (Initial)
---------------
- Project scaffolding.
- Basic extension modules layout.
- Initial registry & supporting infrastructure (pre-enhancement state).
