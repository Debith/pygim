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

Changed
~~~~~~~
- Registry: Override path optimized to single unordered_map probe (eliminated double lookup).
- Registry: ``__repr__`` now includes ``policy``, ``hooks`` flag, and current ``size``.
- Docs: Expanded ``_pygim_fast/registry.h`` with architectural overview & guidance.
- Project instructions updated to reflect new registry surface area (PR #4).
- Refactored factory and registry internals into explicit ``core`` (pybind-free) and ``adapter`` (pybind boundary) headers.
- Renamed pybind module translation units to ``bindings.cpp`` and updated build naming logic so modules remain ``pygim.factory`` and ``pygim.registry``.
- Repository/MSSQL: Simplified native ``persist_dataframe`` Arrow path to prefer DataFrame ``__arrow_c_stream__`` for direct native ingestion, with IPC serialization (``write_ipc``) as fallback, removing Python-side Arrow orchestration from the hot path.
- Repository/MSSQL: Process Arrow input batch-by-batch in BCP ingestion to avoid full-table materialization and preserve correctness for multi-batch data.
- Playground stress harness: Add explicit ``--arrow`` CLI flag (mutually exclusive with ``--no-arrow``) to set ``PYGIM_ENABLE_ARROW_BCP`` for reproducible Arrow-path runs.

Fixed
~~~~~
- Tests: Ensured override semantics correctly raise when ``override=True`` and key is missing.
- Added edge-case tests for factory missing getitem/override behavior and registry key tuple validation + ``find_id`` variant fallback.
- Repository/MSSQL Arrow persist: Export Polars IPC payloads with legacy-compatible ``compat_level=oldest`` to avoid unsupported Arrow view encodings in mixed Arrow-runtime environments.
- Repository/MSSQL Arrow BCP: Fix variable-length text/date/timestamp binding requirements (terminator metadata) and per-row fixed-width column pointer binding to prevent fallback/duplicate-row insertion behavior.

Performance
~~~~~~~~~~~
- Reduced overhead on override operations through consolidated probe.

Docs
~~~~
- Added inline binding docstrings for new registry APIs.
- Added educational examples demonstrating hooks and override semantics.
- Expanded class- and method-level documentation in registry/factory core+adapter headers with explicit rationale, argument, return, and exception notes.

0.0.1 (Initial)
---------------
- Project scaffolding.
- Basic extension modules layout.
- Initial registry & supporting infrastructure (pre-enhancement state).
