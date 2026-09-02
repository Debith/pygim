# C++ Runtime & Linking Model

How pygim's C++ extensions link against the C++ runtime and their native
dependencies, and why. Every decision here exists because something broke
without it; the incident reports below are the evidence.

## Overview

| Concern | Decision | Where |
|---|---|---|
| C++ runtime (Linux) | `-static-libstdc++` + `-Wl,--exclude-libs,ALL` | `setup.py` global link args |
| Arrow libraries | Runtime resolution from the installed `pyarrow` package | RPATH `$ORIGIN/../pyarrow` + Python-side preload |
| ODBC driver manager | unixODBC (`odbc`) on POSIX, `odbc32` from the Windows SDK | `setup.py::_odbc_library_name()` |
| MSSQL BCP entry points | Resolved at runtime (`dlopen` / `LoadLibraryW`), never linked | `persistence/strategy/mssql/bcp/bcp_api.h` |
| ODBC headers on Windows | `<windows.h>` must precede `<sql.h>`/`<sqltypes.h>` | `persistence/odbc_headers.h` |
| Missing native deps | Build **aborts** — extensions are never skipped | `setup.py::_require_dep()` |

## Why `-static-libstdc++`

The extensions are built with a newer GCC (conda's `gxx_linux-64`, or the
manylinux toolchain in CI) than the libstdc++ available at runtime in some
target environments. Statically bundling the runtime means an extension
built with GCC *N* still loads on a system whose shared libstdc++ predates
GCC *N*'s symbols.

## Incident: the libstdc++ interposition segfault (2026-09-01)

**Symptom.** `pygim.path(f).read()` on a TOML file segfaulted — but *only*
when `pyarrow` had been imported earlier in the process. Import order alone
was harmless; the crash happened inside the reader. First observed as a
pytest crash in `test_toml_read_matches_tomllib` after a conftest change
started preloading pyarrow, which made it look like a test-infrastructure
problem. It was not.

**Mechanism.** `-static-libstdc++` copies the C++ runtime *into* each
extension, but by default those ~2,500 `std::` symbols were **exported**
from the `.so` (static archives are compiled without hidden visibility, so
`pybind11`'s `-fvisibility=hidden` does not cover them). Meanwhile
`libarrow.so` — loaded by `import pyarrow` — links the *system*
`libstdc++.so.6` dynamically. Two different builds of the C++ runtime were
now live in one process with overlapping, interposable symbol tables: some
references resolved into one copy, some into the other. Mixed runtime
internals (locale/facet state, unwinder personality data) are not designed
to be split that way, and the first code path that crossed the seam — the
TOML reader — crashed.

Nothing about this was specific to the dev machine. Released manylinux
wheels are built the same way, and `pyarrow` is a hard dependency that any
persistence user imports — so end users would eventually have hit the same
class of crash, with no reproducible recipe beyond "sometimes pathlike
segfaults".

**Fix.** Link every extension with `-Wl,--exclude-libs,ALL`, which marks
symbols pulled from static archives (i.e. libstdc++.a) as local. Each
extension now uses its own bundled runtime consistently; libarrow uses the
system one; neither can interpose the other. Exported `std::` symbols went
from ~2,463 to 5 (libstdc++'s inline Unicode tables — `STB_GNU_UNIQUE`
symbols that must remain visible; they are data tables, not state, and are
harmless).

**Regression check.** An extension must not export the runtime:

```sh
nm -D --defined-only src/pygim/pathlike.cpython-*.so | grep -c ' _ZSt\| _ZNSt'
# expect a single-digit count (inline unicode tables), not thousands
```

And the smoke test for the original bug:

```sh
python -c "import pyarrow, pygim; print(pygim.path('x.toml').read())"
```

## How Arrow libraries are resolved

Arrow-linked extensions (`datagen`, `_persistence`, `_persistence_test`,
`_fetch_benchmark`) carry `NEEDED libarrow.so.<major>` and resolve it at
load time via, in order:

1. **RPATH `$ORIGIN/../pyarrow`** — works for a wheel installed into
   site-packages, where `pygim/` and `pyarrow/` are siblings.
2. **RPATH `$CONDA_PREFIX/lib`** — baked in for conda dev builds.
3. **Already-loaded library with the same SONAME** — the dynamic loader
   reuses `libarrow.so.<major>` if `import pyarrow` already brought it in.

Path 3 is what makes **editable installs** work: the extension sits in
`src/pygim/`, so `$ORIGIN/../pyarrow` points nowhere. Therefore
`pygim.persistence` and `create_df` import `pyarrow` *before* touching the
extension, and `tests/conftest.py` preloads it before any test module.
Because build-time and runtime pyarrow must share the SONAME major, always
build with `--no-build-isolation` in a dev environment — an isolated build
pulls the newest pyarrow, links its SONAME, and the resulting extension
cannot load against the environment's older pyarrow.

## Windows specifics

- **Header order:** the Windows SDK's `<sqltypes.h>` uses `DWORD`/`INT64`
  without declaring them; including it before `<windows.h>` fails with
  `error C4430: missing type specifier - int assumed`. All ODBC includes go
  through `persistence/odbc_headers.h`, which includes `<windows.h>` (with
  `WIN32_LEAN_AND_MEAN` and `NOMINMAX`) first. Never include
  `<sql.h>`/`<sqlext.h>`/`<sqltypes.h>` directly.
- **No `<msodbcsql.h>`:** that header ships with the MSSQL driver's Client
  SDK, which build machines need not have. The few BCP constants and types
  it would provide are declared portably in `bcp_api.h`, and the `bcp_*`
  entry points are resolved at runtime — `dlopen("libmsodbcsql-18.so")` on
  POSIX, `LoadLibraryW(L"msodbcsql18.dll")` (17 fallback) on Windows. A
  missing driver is a runtime error with an install hint, never a build
  dependency.
- **Driver manager:** unixODBC's library is `odbc`; the Windows SDK's is
  `odbc32`. `setup.py::_odbc_library_name()` picks per platform.

## Fail-fast build policy

`setup.py::_require_dep()` aborts the build with an actionable,
per-platform install hint when a native dependency is missing. Extensions
are **never** skipped: a skipped extension leaves whatever stale `.so` an
earlier build produced sitting in `src/pygim/`, and an editable install
happily loads it — deferring the failure to an unrelated moment at runtime
(see the RPATH decay above: the stale binary may reference libraries from
an environment that no longer exists). `test_setup_helpers.py` pins this
behaviour and guards against the skip pattern returning.

## Conditional-compilation policy

**Rule: the code that compiles must be the same everywhere.** Every line of
first-party C++ compiles on Linux, macOS and Windows, locally and in CI. If
something the build needs is missing, that is discovered *before*
compilation begins (`setup.py::_require_dep`, `_require_compiler`) and the
build aborts with an install hint — never by preprocessor fallback that
quietly produces a different binary.

Conditional compilation is allowed in exactly four situations:

1. **Platform selection** between complete, equivalent implementations —
   `#if defined(_WIN32)` choosing `LoadLibraryW` vs `dlopen` in
   `bcp_api.h`. A platform API cannot be installed as a dependency; both
   branches carry the full feature.
2. **Macro hygiene** — `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `#undef BOOL/INT`,
   include guards, and defining spec-canonical ODBC constants the platform
   headers omit (`SQL_COPT_SS_BCP`, `DB_IN`). These never change behaviour;
   they defend the macro namespace or supply constants whose values are
   fixed by specification.
3. **Explicit opt-in flags with environment-independent defaults** —
   `PYGIM_BCP_PROFILING`, `PYGIM_SCOPE_LOGGING_ENABLED`. Default-off on
   every machine; enabling is a deliberate developer action, never a
   side effect of what happens to be installed.
4. **The bounded compiler-standard fallback** —
   `setup.py::_first_supported_std` walks c++26 → c++2c → c++23 and prints
   what it chose. Compiler capability is the one dependency that cannot be
   installed (Apple Clang will not grow c++26 support on demand), so the
   code must remain valid at the floor standard — which CI proves by
   actually compiling there.

**Never allowed:** `__has_include`, or `#ifdef <symbol>` that drops
functionality when a header or library is absent. Removed offenders, kept
here as precedent:

- `#ifdef SQL_DATETIME` around a switch case in `sql_type_map.h` —
  `SQL_DATETIME` is core ODBC (spec value 9); the guard could only ever
  silently remove a type mapping on a hypothetical broken platform.
- `#ifdef VERSION_INFO … #else "dev"` in the bindings — masked a
  build-system regression as version `"dev"`; now `#error`.
- `except ImportError: return table` in `create_df` — polars is a hard
  dependency; the fallback changed the return type on a broken install.

`test_setup_helpers.py::test_no_availability_conditional_compilation`
enforces the bans mechanically (vendored `third_party/` exempt).
