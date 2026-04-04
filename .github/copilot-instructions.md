# Copilot Project Instructions (pygim)

Concise, actionable guidance for AI agents contributing to this repo. Focus on the `pygim` package (multi-module Python + pybind11 C++ extensions) while avoiding speculative conventions.

!! IMPORTANT !! Never install any virtual environment. Always use conda activate to switch to the appropriate environment (e.g., `conda activate py312`).

**Feedback Style**: All the time during the discussion, be brutally honest, analytical, and constructively critical to the smallest detail. Highlight risks, trade-offs, and alternatives explicitly. Make it clear if I am wrong or if there are better approaches. The goal is to ensure the highest quality and maintainability of the codebase, even if it means challenging assumptions or suggesting significant changes. No sugar-coating, just politeness.

**Initial Tasks**: Create yourself following initial tasks to stay consistently aligned with the project goals and conventions. These tasks will be based on the sections outlined below and will evolve as you contribute:
  - Familiarize yourself with the project structure, especially the `src/pygim/` and `src/_pygim/` directories, to understand the separation between public API and internal implementation.
  - Ensure documentation is updated and comprehensive for any new features or changes you introduce, following the existing style and conventions in the codebase. Also ensure documentation is accurate and reflects the current state of the code, especially if you are modifying existing functionality.
  - Adhere strictly to the coding conventions outlined in the "Conventions & Gotchas". Create yourself a checklist based on these conventions to review your code before submission.

**No Duplication**: Avoid leaving duplicated content (docs, diagrams, or code). If new files supersede old ones, update references and remove or slim the originals to prevent drift.

## 1. Big Picture
- Purpose: Provide lightweight, high-performance Python utilities (registry/factory, path/file helpers, broadcast iteration helpers, DDD-style interfaces, CLI tooling).
- Dual-layer layout:
  - `src/pygim/` public Python API + compiled extension modules (`registry`, `factory`, `each`, `pathset`, `utils`).
  - `src/_pygim/` internal Python support (`_core` exceptions/typing, `_cli` app) + C++ sources in `src/_pygim_fast/` bound via pybind11.
- C++ templates wrap generic patterns (registry key policies, factories, path discovery, broadcasting each/proxy) and are exposed as minimal Python-facing classes. Python layer adds ergonomics & CLI.

## 2. Key Components & Patterns
| Area | Files / Notes | Pattern Guidance |
|------|---------------|------------------|
| Registry | `_pygim_fast/registry.[h|cpp]`, public `pygim/registry*.so` | Policy-based (qualname vs identity). Keys accepted as object or `(object_or_id, name)`; qualname policy also accepts bare string id. Optional hooks (`on_register`, `on_pre`, `on_post`) compiled out when disabled. Features: single-probe override (`override=True` requires existing key), decorator form `@registry.register(key, override=False)`, introspection `registered_keys()`, fast id lookup `find_id(obj)` (qualname policy), optional capacity pre-reservation in ctor, explicit `post(key, value)` trigger, informative `__repr__` (policy, hooks, size). Keep key construction & hook execution in C++; only add ergonomic sugar in Python. |
| Factory | `_pygim_fast/factory.h` | Wraps internal `RegistryT<StringKeyPolicy,...>`. Enforces optional interface via runtime `isinstance`. Override rules: `override=True` requires existing entry; duplicate without override raises. Mirror this rule in added Python helpers. |
| Each / Proxy | `_pygim_fast/each.h` | Broadcast attribute/method over iterable. Caches method name between getattr & call. Avoid adding stateful Python wrappers that break this lifecycle. |
| PathSet | `_pygim_fast/pathset.[h|cpp]` | Immutable-ish set semantics around filesystem traversal + pattern matching. Prefer delegating heavy filtering to C++ extension; only compose filters in Python. |
| DDD Interfaces | `_pygim/_core/interfaces.py` | Abstract base interfaces (Entity, Repository, Service, etc.). Do NOT inject domain logic; only use for type/structural contracts. |
| CLI | `_pygim/_cli/_cli_app.py`, `pygim/__main__.py` | Simple click-based tasks: cleanup, coverage, AI placeholder. Expand by adding methods on `GimmicksCliApp`, then expose via a new `@cli.command()` in `__main__.py`. |
| Testing Helpers | `pygim/core/testing.py` | `run_tests()` wrapper w/ optional coverage; re-imports module for top-level coverage lines. Use when adding stand-alone test scripts. |
| Repository | `_pygim_fast/repository/{core,strategy,adapter}/` | Core/adapter one-hop architecture: `Repository<Backend>` generic facade, `RepositoryAdapter<Backend>` pybind11 boundary, `ConnectionPool<Backend>` thread-safe pool with `std::expected`. MSSQL backend via BCP pipeline (`strategy/mssql/bcp/`). `BackendPolicy` C++20 concept verified via `static_assert` in `bindings.cpp`. Arrow-only core (no `py::object` below adapter). GIL released before core operations. Build requires `arrow-cpp >= 15` and ODBC Driver 18. LoadCache for persistent parallel pool reuse. PK auto-detection via ODBC metadata. Benchmarks: 956K rows/s write (101 MB/s), 1.25M rows/s load (122 MB/s) with 8 workers. |

## 3. Developer Workflows
- Environment: Project typically uses a conda env named `py312` (activate first: `conda activate py312`). Always confirm `python -V` matches supported versions before building extensions.
- Install (editable dev): `pip install -e .[dev]` from repo root (pyproject uses setuptools + setuptools_scm).
- Run tests (Python + doctests): `pytest` (configured to include `src/` for doctest collection). CI expectations: short tracebacks (`--tb=short`), doctest continues on failure.
- Coverage (manual): Either CLI `pygim show-test-coverage` or `coverage run -m pytest && coverage report -m` (mirrors `_cli_app.py`).
- Build wheel: standard `python -m build` (no custom backend aside from setuptools). C++ extensions rely on pybind11 (declared in build-system + dev extras).
- Adding a new C++ extension: place sources under `src/_pygim_fast/`, follow existing binding style (`PYBIND11_MODULE(name, m) { ... }`), ensure module file name (target) matches import path `pygim.<name>` by updating build configuration if necessary (setup derives from setuptools discovery).
  - Registry constructor parameters (current): `policy: str`, `hooks: bool`, optional `capacity: int` (use to reserve underlying map when bulk-registering to avoid rehash).
- Repository extension build: requires `arrow-cpp >= 15` and MS ODBC Driver 18 (`msodbcsql18`). Install via conda: `conda install -c conda-forge 'arrow-cpp>=15' pyarrow unixodbc`. Compile-time `static_assert` in `bcp_types.h` fails with actionable message if Arrow version is too old.

## 4. Conventions & Gotchas
- Naming: Internal modules prefixed with `_pygim`; exported symbols live under `pygim`. Avoid leaking private `_core` internals into public namespace unless explicitly added to `__all__` in a public module.
- C++ member naming: Class data members use `m_` prefix (e.g., `m_var`, `m_policy`, `m_registry`) – preserve this when adding fields.
- Errors: Use exception classes re-exported from `pygim.core.explib` (e.g., `GimError`, `DispatchError`) rather than generic `Exception` for library-level failures.
- Registry Keys: Qualname policy permits passing a string id directly; identity policy requires a Python object. Keep tuple form `(thing_or_id, name)` stable; adding new key forms requires modifying `make_key` template logic in C++ (not just Python).
- Hooks: Pre/post hooks only active when `hooks=True` at construction; do not assume const lookups are allowed with hooks enabled (non-const path executes pre-hooks).
- Registry Override: `override=True` enforces existence (raises if missing). Implementation is optimized to a single unordered_map probe—avoid reintroducing double lookups.
- Registry Decorator: Preferred ergonomic path for functions/classes: `@registry.register("id")` or `@registry.register(obj, "alt_name")`. Decorator returns the original object unmodified.
- Registry Introspection: Use `registered_keys()` to obtain list of current logical ids (ordering not guaranteed). Use `find_id(obj)` (qualname policy only) to resolve an object's registered id or `None`.
- Registry Repr Contract: `Registry(policy=<qualname|identity>, hooks=<True|False>, size=<n>)`. Maintain fields & ordering when extending; add new fields only if broadly useful.
- Factory Override Semantics: `override=True` must fail if original does NOT exist (inverse of many libraries). Preserve this invariant.
- Broadcast Proxy: After a method broadcast call, internal cached method name is cleared; Python wrappers must not hold cross-call state that assumes persistence.
- PathSet Filtering: Pattern matching uses custom `match_pattern` supporting `*`, `?`, and special cases `*` and `*.*`; replicate logic via C++ instead of re-implementing in Python for consistency.
- Coverage Helper: Re-import (`reload`) is required; don't restructure to module-level side effects that break idempotent reload during coverage.
- Repository GIL Pattern: Adapter releases GIL (`py::gil_scoped_release`) before calling core `save()`/`load()`. No `py::object` crosses into `core/`; Arrow C Data Interface is the boundary.
- Repository BackendPolicy: New backends must satisfy the `BackendPolicy` concept (Connection, SaveImpl, LoadImpl, Dialect, **LoadCache** types + connect/reset/close/name). Verified via `static_assert` in `bindings.cpp`.
- Repository BCP Ordering: BCP must be enabled on the connection (`SQL_COPT_SS_BCP`) BEFORE `SQLDriverConnect`. Violating this causes silent BCP failures.
- Repository Connection Pool: `checkout()` returns `std::expected<ConnectionHandle, PoolError>` — timeout and pool-closed are control flow, not exceptions.
- Repository Arrow Import: `import_record_batch_reader()` must be called WITH GIL held. Supports PyCapsule (`__arrow_c_stream__`), Polars DataFrame (recursive `.to_arrow()` with depth guard), and PyArrow RecordBatchReader/Table.
- Repository LoadCache: `MssqlLoadCache` provides persistent `LoadConnectionPool` reuse across `load()` calls — eliminates per-load connection establishment cost. `NullLoadCache` is zero-cost for non-MSSQL backends. Cache invalidates on connection string or pool size change.
- Repository PK Auto-Detect: When `partition_column` is empty and `workers > 1`, `detect_partition_column()` uses SQLPrimaryKeys() + SQLColumns() to find the first integer PK column. Falls back to single-threaded load if none found.
- Repository Stale Connection Retry: Parallel load retries once on stale connection (SQLSTATE 08S01, 08001, HY000). Clears cache before retry. No proactive health checks (avoids latency on the common path).
- Repository Dual Pool Pattern: `ConnectionPool` for shared operations (save, metadata), `LoadConnectionPool` for dedicated parallel load workers. They serve different purposes — don't merge.

### C++ Performance Philosophy
- Standard: Target C++20 features by default (concepts, `std::variant`, ranges, designated initializers where sensible) — prefer zero-overhead abstractions.
- Hot paths: Minimize Python<->C++ boundary crossings; batch work in C++ when possible rather than per-element round trips.
- Allocation: Avoid transient heap allocations in tight loops (favor reserve + reuse, small local structs, `std::string_view` when safe).
- Branching: Reduce unpredictable branches in iteration utilities (e.g., `each` broadcast) — hoist invariant checks outside loops.
- SIMD / vectorization: Open to adding explicit SIMD (e.g., via `<immintrin.h>` or std::experimental once available) where profiling shows arithmetic hotspots — gate behind clear helper functions to preserve portability.
- Hooks & optional features: Keep disabled code paths compiled out via templates / boolean non-type params (`EnableHooks`) to avoid runtime branching.
- Measure first: Any micro-optimization or SIMD addition should be backed by a (or added) benchmark before merging; avoid premature complexity.

## 5. Adding Features (Examples)
- New CLI command: add method to `GimmicksCliApp`, then decorate a function in `pygim/__main__.py` with `@cli.command()` invoking that method.
- Extend registry with new hook type: add storage + `add_on_<name>` + `run_<name>` in `Hooks` struct (both enabled/disabled specializations), then expose binder method in `registry.cpp`.
- Add Python convenience around Factory: write a thin wrapper that calls underlying extension methods; do not replicate registry logic in Python.
- Extend registry introspection: mirror existing pattern—add C++ method on wrapper, bind with docstring, then write minimal test exercising both hook states & policies.
- Add registry deletion (future): will require policy-consistent key resolution + hook decision (likely new `on_remove`); update tests for size & registered_keys invariants.
- Add new database backend: create `strategy/<name>/` directory with `backend.h` (satisfying `BackendPolicy`), `dialect.h`, `save_impl.h`, `load_impl.h`. Add `static_assert` in new `bindings.cpp` or colocate with existing. Extend `ext.repository.toml` sources.
- Extend save/load pipeline: modify `SaveImpl`/`LoadImpl` in the strategy directory; core `Repository<Backend>` forwards to these via associated types.
- Add repository transform hooks: call `add_pre_transform(fn)` / `add_post_transform(fn)` on the adapter — hooks run WITH GIL at the Python boundary.

## 6. Testing Guidance
- Put unit tests under `tests/unittests/` with `test_*.py` prefix. Use runtime behavior validations (e.g., ensure override semantics) not structural introspection of C++ templates.
- For C++ exposed functionality, favor black-box tests importing compiled modules from `pygim` package.
- Registry Tests Matrix: Always parametrize over (policy × hooks). Cover: register/get, duplicate rejection, override success/failure (missing key), decorator path, hook invocation counts (when enabled), introspection (registered_keys, find_id), representation, and negative id forms (string for identity policy).
- Repository Tests: `ext.repository_test.toml` builds a test-only pybind11 module (`_repository_test`) with `py::module_local()` to avoid type conflicts. Parametrize by backend when multiple backends exist. Tests requiring a live database use `test_repository.py`; C++ unit tests via `test_bindings.cpp`.

## 7. Safe Changes Checklist (pre-commit mentally)
- Imports: public modules import from `pygim.*`, internals from `_pygim.*`.
- C++ changes: rebuild locally (e.g., `pip install -e .` triggers). Run `pytest` after rebuild to confirm ABI compatibility.
- Preserve exception types & invariant semantics (registry duplicates, factory override rule, hook enabling gate).
- Changelog: update `CHANGELOG.rst` under `Unreleased` for meaningful user-facing, architecture, testing, performance, or workflow changes.
- Keep this file current: Whenever introducing a new extension module, altering key policy semantics, hook lifecycle, or CLI surface, append/update the relevant section here in the same concise style.
- Performance changes: Add brief rationale + (if available) benchmark numbers to PR description; ensure no regression in existing behaviors/tests.
 - Examples: When adding notable surface area (e.g., new registry capabilities), place runnable, minimal examples under `docs/examples/<area>/` and verify via a quick local run.
