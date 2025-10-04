# Copilot Project Instructions (pygim)

Concise, actionable guidance for AI agents contributing to this repo. Focus on the `pygim` package (multi-module Python + pybind11 C++ extensions) while avoiding speculative conventions.

## 1. Big Picture
- Purpose: Provide lightweight, high-performance Python utilities (registry/factory, path/file helpers, broadcast iteration helpers, DDD-style interfaces, CLI tooling).
- Dual-layer layout:
  - `src/pygim/` public Python API + compiled extension modules (`registry`, `factory`, `each`, `pathset`, `utils`).
  - `src/_pygim/` internal Python support (`_core` exceptions/typing, `_cli` app) + C++ sources in `src/_pygim_fast/` bound via pybind11.
- C++ templates wrap generic patterns (registry key policies, factories, path discovery, broadcasting each/proxy) and are exposed as minimal Python-facing classes. Python layer adds ergonomics & CLI.

## 2. Key Components & Patterns
| Area | Files / Notes | Pattern Guidance |
|------|---------------|------------------|
| Registry | `_pygim_fast/registry.[h|cpp]`, public `pygim/registry*.so` | Generic key policy (qualname vs identity). Accepts key as object or `(object_or_id, name)`. Hooks optional (on_register/on_pre/on_post). When extending: keep policy-specific key construction in C++; add Python sugar externally. |
| Factory | `_pygim_fast/factory.h` | Wraps internal `RegistryT<StringKeyPolicy,...>`. Enforces optional interface via runtime `isinstance`. Override rules: `override=True` requires existing entry; duplicate without override raises. Mirror this rule in added Python helpers. |
| Each / Proxy | `_pygim_fast/each.h` | Broadcast attribute/method over iterable. Caches method name between getattr & call. Avoid adding stateful Python wrappers that break this lifecycle. |
| PathSet | `_pygim_fast/pathset.[h|cpp]` | Immutable-ish set semantics around filesystem traversal + pattern matching. Prefer delegating heavy filtering to C++ extension; only compose filters in Python. |
| DDD Interfaces | `_pygim/_core/interfaces.py` | Abstract base interfaces (Entity, Repository, Service, etc.). Do NOT inject domain logic; only use for type/structural contracts. |
| CLI | `_pygim/_cli/_cli_app.py`, `pygim/__main__.py` | Simple click-based tasks: cleanup, coverage, AI placeholder. Expand by adding methods on `GimmicksCliApp`, then expose via a new `@cli.command()` in `__main__.py`. |
| Testing Helpers | `pygim/core/testing.py` | `run_tests()` wrapper w/ optional coverage; re-imports module for top-level coverage lines. Use when adding stand-alone test scripts. |

## 3. Developer Workflows
- Environment: Project typically uses a conda env named `py312` (activate first: `conda activate py312`). Always confirm `python -V` matches supported versions before building extensions.
- Install (editable dev): `pip install -e .[dev]` from repo root (pyproject uses setuptools + setuptools_scm).
- Run tests (Python + doctests): `pytest` (configured to include `src/` for doctest collection). CI expectations: short tracebacks (`--tb=short`), doctest continues on failure.
- Coverage (manual): Either CLI `pygim show-test-coverage` or `coverage run -m pytest && coverage report -m` (mirrors `_cli_app.py`).
- Build wheel: standard `python -m build` (no custom backend aside from setuptools). C++ extensions rely on pybind11 (declared in build-system + dev extras).
- Adding a new C++ extension: place sources under `src/_pygim_fast/`, follow existing binding style (`PYBIND11_MODULE(name, m) { ... }`), ensure module file name (target) matches import path `pygim.<name>` by updating build configuration if necessary (setup derives from setuptools discovery).

## 4. Conventions & Gotchas
- Naming: Internal modules prefixed with `_pygim`; exported symbols live under `pygim`. Avoid leaking private `_core` internals into public namespace unless explicitly added to `__all__` in a public module.
- C++ member naming: Class data members use `m_` prefix (e.g., `m_var`, `m_policy`, `m_registry`) – preserve this when adding fields.
- Errors: Use exception classes re-exported from `pygim.core.explib` (e.g., `GimError`, `DispatchError`) rather than generic `Exception` for library-level failures.
- Registry Keys: Qualname policy permits passing a string id directly; identity policy requires a Python object. Keep tuple form `(thing_or_id, name)` stable; adding new key forms requires modifying `make_key` template logic in C++ (not just Python).
- Hooks: Pre/post hooks only active when `hooks=True` at construction; do not assume const lookups are allowed with hooks enabled (non-const path executes pre-hooks).
- Factory Override Semantics: `override=True` must fail if original does NOT exist (inverse of many libraries). Preserve this invariant.
- Broadcast Proxy: After a method broadcast call, internal cached method name is cleared; Python wrappers must not hold cross-call state that assumes persistence.
- PathSet Filtering: Pattern matching uses custom `match_pattern` supporting `*`, `?`, and special cases `*` and `*.*`; replicate logic via C++ instead of re-implementing in Python for consistency.
- Coverage Helper: Re-import (`reload`) is required; don't restructure to module-level side effects that break idempotent reload during coverage.

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

## 6. Testing Guidance
- Put unit tests under `tests/unittests/` with `test_*.py` prefix. Use runtime behavior validations (e.g., ensure override semantics) not structural introspection of C++ templates.
- For C++ exposed functionality, favor black-box tests importing compiled modules from `pygim` package.

## 7. Safe Changes Checklist (pre-commit mentally)
- Imports: public modules import from `pygim.*`, internals from `_pygim.*`.
- C++ changes: rebuild locally (e.g., `pip install -e .` triggers). Run `pytest` after rebuild to confirm ABI compatibility.
- Preserve exception types & invariant semantics (registry duplicates, factory override rule, hook enabling gate).
- Keep this file current: Whenever introducing a new extension module, altering key policy semantics, hook lifecycle, or CLI surface, append/update the relevant section here in the same concise style.
- Performance changes: Add brief rationale + (if available) benchmark numbers to PR description; ensure no regression in existing behaviors/tests.

---
If any architectural area feels underspecified (e.g., future AI integration in CLI), request clarification before implementing. Provide feedback if adding a feature that spans Python + C++ so patterns can be documented here.
