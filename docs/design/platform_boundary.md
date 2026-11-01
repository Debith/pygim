# The operating system behind a boundary

Status: decided and built 2026-10-31 (D77) · Owner: Debith

The operating system is a model foreign to pygim's domain, as Python's object model is. The domain —
pathlib's rules, the glob, the parser, similarity — states its rules once, the same on every system.
What differs between systems sits behind one boundary, in two kinds:

- **The rules** a system has: which characters separate a path, whether names fold case, what an
  errno is called and which error `open()` raises for a folder. Domain-facing, written as data:
  compile-time tables, one per system, every one compiled everywhere — so the domain is tested under
  Windows' rules on Linux.
- **The calls** a system makes: listing a directory, opening and reading a file, forcing it to the
  disk, blocking signals, naming a thread, asking the caches' sizes. Infrastructure: one engine per
  system behind a concept; only the running system's is compiled.

The calls live in `src/_pygim_fast/platform/`; a rule lives there too, or with the domain it is the
rules of (pathlike's path strategies, the BCP driver's file names). The running system's are chosen
once, by `platform::native`. Nothing else asks which system it runs on: no `#if _WIN32`, `__APPLE__`
or `__linux__` outside that folder.

## Scenario

PR #39's CI (2026-10-31) failed 27 tests across the matrix. Thirteen depended on the system — seven
on Windows, five on macOS, one on Windows and Python's version — and two pygim defects among them
lived inside domain classes as `#ifdef`s:
`not_a_file::what()` worded an errno in MSVC's lowercase, and `add_file` raised IsADirectoryError for
a folder where Windows' `open()` raises PermissionError. Both are translations — an ACL's job — and
both were fixed with another `#ifdef` in the same class. The glob's own rules, its case and its
separators, are `#ifdef`s in glob_walk.h, so the glob under Windows' rules is tested nowhere: its tests
cannot run on Windows (they build symbolic links) and never ask for Windows' rules elsewhere.

On 2026-10-31, 59 conditionals on the system sat in 14 headers outside any boundary (counted by
`test_the_operating_system_is_asked_only_behind_the_platform_boundary`):

| Header | Conditionals | What they decide | Kind |
| --- | --- | --- | --- |
| pathlike/glob_walk.h | 11 | the case and separator rules; the listing (`opendir` against `directory_iterator`); a name's native spelling; Linux's one-`lstat` shortcut | rules and calls |
| pathlike/core.h | 7 | exclusive create, `fsync` (`F_FULLFSYNC` on macOS), unlink, a path's UTF-8, the native path strategy | calls, and the choice of rules |
| utils/interrupt.h | 7 | signal handlers and the wake-up pipe | calls |
| utils/parallel.h | 7 | the signal mask a pool's threads inherit; thread names | calls |
| pathlike/file_reads.h | 4 | the reads; an errno's words; `open()`'s error for a folder | calls and rules |
| utils/cpu.h, utils/memory.h | 8 | the caches; resident memory | calls |
| pathlike/adapter/path_text.h | 3 | a path's text at the Python boundary: the file system's encoding | rules |
| persistence/strategy/mssql/bcp/bcp_api.h, persistence/odbc_headers.h | 4 | loading the ODBC driver (`dlopen` against `LoadLibraryW`); the SDK's headers | calls |
| similarity/adapter/stream.h | 3 | whether the process forked since a stream opened | calls |
| utils/huge_pages.h, utils/kept_blocks.h | 4 | `madvise` for huge pages; `pthread_atfork` | calls |
| utils/timestamp.h | 1 | the clock | calls |

pathlike already has the shape in two places: `posix_strategy` and `windows_strategy` (core.h) are the
path rules as compile-time classes, both compiled everywhere and proven against pathlib's
PurePosixPath and PureWindowsPath; `posix_reads` and `windows_reads` (file_reads.h) are the reads
behind the `file_reads` concept. The boundary extends that pattern to the rest.

## Decision

| Option | What it is | Verdict |
| --- | --- | --- |
| A. leave it | an `#if` wherever a system differs | rejected: Windows' and macOS's behaviour is checked only in CI, and translations keep settling in domain classes |
| **B. the system's rules as data, its calls as engines** | `platform/` holds a rules table per system and an engine per system behind each concept; the domain is a template on the rules; a test_layering ratchet counts the conditionals left outside | **chosen** (Debith, 2026-10-31: "I like B") |
| C. a runtime interface | one abstract system, real or fake, called through virtual functions | rejected: a virtual call on the hot paths (a listing, a read), against making a component a template on its policy |

## What it does not cover

- **Compiler portability** — MSVC's constexpr step limit, Apple clang's late-parsed nested templates,
  `_MSC_VER` checks: build settings and code that every compiler accepts, not a system's rules.
- **Python's versions** — an exception set as `PyErr_SetObject(OSError, args)` that 3.10 matched by
  its type: the Python boundary's, the adapters'.
- **What a system's file systems do** — macOS refusing a name that is not UTF-8, or folding case: the
  rules can say so, but a test of the real system still meets it on that system.

## Sequence of work

Each step a refactor under the tests as they are (#36), its own commit, the ratchet lowered with it:

1. This document and the ratchet (`test_the_operating_system_is_asked_only_behind_the_platform_boundary`, 59).
2. The path rules: the glob's case default and separators from the path strategy; the glob's
   matching tested under Windows' rules on Linux — the coverage Windows lacks.
3. The listing: one engine per system behind a concept, the glob reading through it.
4. The errno translation as a table: an errno's words and `open()`'s error for a folder, out of
   `not_a_file`.
5. The writes: exclusive create, forcing to the disk, unlink.
6. The process: signals and the wake-up pipe (interrupt.h), the threads' signal mask and names
   (parallel.h), `pthread_atfork` (kept_blocks.h), fork detection (stream.h).
7. The machine: caches, resident memory, huge pages, the clock.
8. Loading a library: the ODBC driver (bcp_api.h, odbc_headers.h).
9. The adapters' (path_text.h).

## Built

| Header | What it holds | Kind |
| --- | --- | --- |
| platform/native.h | `native`: the running system's family, the one place that asks the compiler | the choice |
| pathlike/path_rules.h | `posix_strategy`, `windows_strategy`: separators, case, parsing; `native_strategy` | rules |
| platform/errors.h | an errno's words; `open_error_for<F>`, `open()`'s error for a folder; `os_error::of<F>`, what a system's error code tells Python's `OSError` (an errno, or Windows' own error, which Python maps) | rules and calls |
| platform/files.h | a path's text (`path_text_is_utf8`), exclusive create, write, force, close, unlink | rules and calls |
| platform/home.h | `home_of(user)`: whose home `~` names; posixpath's and ntpath's rules | rules and calls |
| platform/listing.h | a directory's listing with each entry's kind (`directory`, `file`, `link`, `other`), `entry_at`, `finds_a_name_by_stat` | calls |
| platform/reads.h | `posix_reads`, `windows_reads` behind the `file_reads` concept | calls |
| platform/process.h | a thread's name, the signal mask its threads inherit, `at_fork`, `process_id` | calls |
| platform/signals.h | `interrupt_handlers` (Ctrl-C, SIGTERM), `wakeup` (a pipe; an event) | calls |
| platform/caches.h | `system_caches`; the kernel's /sys list read by rules compiled everywhere | calls and rules |
| platform/memory.h | `resident_bytes`, `peak_resident_bytes`, `advise_huge_pages` | calls |
| platform/clocks.h | `thread_cpu_ns`, `counts_thread_cpu_time` | calls |
| platform/libraries.h | `library`: `first_of`, `last_match`, `function<F>` | calls |
| platform/windows_headers.h | `<windows.h>` lean and without `min` and `max`, for every header that calls it | — |
| persistence/.../bcp_api.h | `driver_files<F>`: the ODBC driver's library as each system names it | rules |

## Open

| Question | Lean |
| --- | --- |
| Q42: should Windows' rules be usable on Linux from Python, as pathlib's PureWindowsPath is? | yes, later: with the rules as data it costs little, but it is a feature, not the boundary |
