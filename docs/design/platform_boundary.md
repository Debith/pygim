# The operating system behind a boundary

Status: decided and built 2026-10-31 (D77); a component's shape decided and built 2026-11-03 (D106); the calls as a policy, so every system's logic is proven on every system, decided and built 2026-11-08 (D138) · Owner: Debith

The operating system is a model foreign to pygim's domain, as Python's object model is. The domain —
pathlib's rules, the glob, the parser, similarity — states its rules once, the same on every system.
What differs between systems sits behind one boundary, in two kinds:

- **The rules** a system has: which characters separate a path, whether names fold case, what an
  errno is called and which error `open()` raises for a folder. Domain-facing, written as data:
  compile-time tables, one per system, every one compiled everywhere — so the domain is tested under
  Windows' rules on Linux.
- **The calls** a system makes: listing a directory, opening and reading a file, forcing it to the
  disk, blocking signals, naming a thread, asking the caches' sizes. Infrastructure: one class per
  system for each component, in a file of its own, behind the component's concept; only the running
  system's is compiled ([the shape of a component](#the-shape-of-a-component-d106)).

The calls live in `src/_pygim_fast/platform/`; a rule lives there too, or with the domain it is the
rules of (pathlike's path strategies, the BCP driver's file names). The running system's are chosen
once, by `platform::native`. Nothing else asks which system it runs on: no `#if _WIN32`, `__APPLE__`
or `__linux__` outside that folder. Inside it, each component's header asks once, in the `#if` chain
that picks the running system's class, and a system's own file (`platform/linux/memory.h`,
`platform/windows/libraries.h`) never asks
(`test_inside_the_boundary_a_header_asks_once_and_a_systems_own_file_never`).

What is built so far: the headers the code on `main` asks of the system — `native.h` (the family,
decided once), `windows_headers.h`, `files.h` (a path's text), `memory.h` (the resident-memory probes,
a class per system under `linux/`, `macos/` and `windows/`) and `libraries.h` (loading the ODBC driver,
a class each under `posix/` and `windows/`) — and that code moved behind them. One header came ahead of
its caller: `locks.h` (a file locked across processes, the calls each under `posix/` and `windows/`),
whose first caller is a file store's commit lock on a branch not yet in `main`. It is here so that CI,
which runs for pull requests into `main`, compiles each system's calls; that branch has no pull
request. The other headers this document names (the listing, the reads, the process's and the signals'
calls, the caches, the clocks) arrive with the code that calls them, each behind the same boundary and
in the same shape.

## Scenario

PR #39's CI (2026-10-31) failed 27 tests across the matrix. Thirteen depended on the system — seven
on Windows, five on macOS, one on Windows and Python's version — and two pygim defects among them
lived inside domain classes as `#ifdef`s:
`not_a_file::what()` worded an errno in MSVC's lowercase, and `add_file` raised IsADirectoryError for
a folder where Windows' `open()` raises PermissionError. Both are translations — an ACL's job — and
both were fixed with another `#ifdef` in the same class. The glob's own rules, its case and its
separators, are `#ifdef`s in glob_walk.h, so the glob under Windows' rules is tested nowhere: its tests
cannot run on Windows (they build symbolic links) and never ask for Windows' rules elsewhere.

On 2026-10-31, on PR #39's branch, 59 conditionals on the system sat in 14 headers outside any
boundary (counted by `test_the_operating_system_is_asked_only_behind_the_platform_boundary`).
`main`, which has less of that code, had 14 in five files: pathlike's path text and native rules, the
ODBC headers and driver loading, the memory probes. The first step moves those, and the test then
allows none outside the folder. PR #39's branch had:

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

## The shape of a component (D106)

A component is one subject of the system's calls: the process's memory, loading a library, and with
PR #39's code the listing, the reads, the writes, the process. Each system gets one class for it,
holding all of that component's calls (Q51: the huge-page advice joins the memory probes, not a
contract of its own), in a file of its own under the folder of the most general system it serves.
The component's header in `platform/` states what every class answers, as a concept, and picks the
running system's, once:

```
platform/
    native.h              the family, decided once
    windows_headers.h     <windows.h>, lean; nothing on other systems
    files.h               a path's text: path_text_rules; the choice of native_files; text_from_fs and fs_from_text over it
    posix/files.h         posix_files: the system's own bytes, string() both ways
    windows/files.h       windows_files: UTF-8 from the wide native form, u8string() both ways
    libraries.h           library_loader; basic_posix_loader and basic_windows_loader, the logic over each system's
                          calls (D138); the choice of native_loader; basic_library, and library over it
    memory.h              process_memory; basic_linux_memory, the logic over Linux's calls (D138); the choice of native_memory
    posix/libraries.h     posix_dl_calls: dlopen, dlsym, glob; posix_loader over them
    windows/libraries.h   win32_library_calls: LoadLibraryW, GetProcAddress; windows_loader over them
    linux/memory.h        linux_proc_calls: a pread of /proc/self/statm, the page size, getrusage; linux_memory over them
    macos/memory.h        macos_memory: the Mach task, getrusage in bytes (no logic above the calls, no policy)
    windows/memory.h      windows_memory: GetProcessMemoryInfo (no logic above the call, no policy)
    locks.h               file_lock_calls; the choice of native_lock_calls (no logic above the calls, no policy)
    posix/locks.h         posix_lock_calls: open, flock, close
    windows/locks.h       win32_lock_calls: _wsopen_s, LockFileEx and UnlockFileEx through the descriptor's handle, _close
    (tests/static/platform_proofs.cpp proves every logic class over fake calls, in every build, on every system)
```

The memory component, in pseudocode:

```python
# platform/memory.h
class ProcessMemory(Protocol):               # a concept
    def resident_bytes() -> int: ...         # 0 when the call fails, never raises
    def peak_resident_bytes() -> int: ...    # never below resident_bytes()

if WINDOWS: NativeMemory = WindowsMemory     # windows/memory.h, included only here
elif MACOS: NativeMemory = MacosMemory       # macos/memory.h
else:       NativeMemory = LinuxMemory       # linux/memory.h

assert NativeMemory satisfies ProcessMemory  # checked by the compiler when it builds

# utils/memory.h, a caller: never asks which system
def resident_bytes(): return platform.NativeMemory.resident_bytes()
```

Memory splits three ways where loading a library splits two: POSIX has no call for the current
reading, and `getrusage`'s peak is kilobytes on Linux and bytes on macOS. A component whose forms all
compile on every system still gets a class per system: `files.h`'s two conversions are standard C++
on both, so `posix_files` and `windows_files` are both compiled and proven everywhere, and the family
chooses `native_files` once. The Python boundary follows the same shape outside the platform:
`path_text.h`'s `posix_path_text` and `windows_path_text`, chosen by `platform::native`. Until
2026-11-08 both headers branched on a flag, `if constexpr (path_text_is_utf8)`, which let a component
pick the system's rule itself; the layering test now refuses a branch on a platform value outside
`platform/`.

| Option | What it is | Verdict |
| --- | --- | --- |
| A. `#if` inside the functions | one header per component, each system's code interleaved in every function (as first built, 2026-11-01) | rejected: a flag in every function, and no shape the systems are held to |
| **B. a class per system, a file each** | the component's header holds the concept and the one choice; a system's files ask nothing | **chosen** (Debith, 2026-11-03: "let's do with B") |
| C. B, and every system's class checked on every build | each class's declaration compiled everywhere, its system calls only on its own system | not now: it splits classes of two or three functions in two, and CI's matrix catches the same drift in the same run; worth it when a component grows large (the listing, the reads) — taken up by D138, which splits each class into its logic and its calls |

## The calls as a policy (D138)

Where to draw the line between what a test can prove anywhere and what only the system can prove:
at the system call. A per-system class is two things. Its **logic** — which name to open first, that
the newest file is the last of glob's sorted matches, that Windows widens a name and has no glob, that
statm's second field is the resident count, that `ru_maxrss` is kilobytes on Linux, that a peak never
reads below the present — depends on the family, not on the system being present. Its **calls** —
`dlopen`, `glob`, `LoadLibraryW`, `pread`, `getrusage` — are the behaviour, and exist only on their own
system. So a class with logic above its calls takes them as a type parameter, the system's real
wrappers by default, a fake in the proofs
(Debith, 2026-11-08: "if we build a small wrapper for each system call, we could replace them in the
test time ... that could be compile time thing so execution would not see the wrapper at all"):

```python
class Win32LibraryCalls:                     # windows/libraries.h: one wrapper per call, no logic
    load_library(wide_name) -> handle        # LoadLibraryW
    proc_address(handle, name) -> address    # GetProcAddress

class BasicWindowsLoader[Calls]:             # libraries.h: the logic, compiled everywhere
    def open(name): return Calls.load_library(widen(name))
    def newest(pattern): return None         # Windows has no glob

windows_loader = BasicWindowsLoader[Win32LibraryCalls]   # what ships: the wrappers inlined, nothing seen at run time
BasicWindowsLoader[FakeWin32Calls]                       # tests/static/platform_proofs.cpp, on Linux too: the logic over recorded answers
```

The testing rule that follows: **prove the logic on every system** (the proofs, constexpr over fakes,
compiled by every build — so the Windows loader's logic is proven on Linux at compile time); **prove
each call on its own system** (CI's matrix, which is why its Windows and macOS jobs are not
optional); **prove the join once per system** (the suite's probe tests on the real calls there). The
proofs reach the error paths the real system rarely gives a test: a library that is not installed, a
symbol that is missing, a peak the kernel has not caught up with, procfs absent.

| Option | What it is | Verdict |
| --- | --- | --- |
| **A. a type parameter per class** | `basic_posix_loader<Calls>`, `basic_linux_memory<Calls>`: the calls struct is a type whose static functions inline; the system's file holds only the struct and the alias | **chosen**: no cost at run time, the logic constexpr and proven everywhere, the calls struct an inventory of what pygim asks each system for |
| B. a table of function pointers swapped at test time | one runtime table per component, a fake installed by a test hook | rejected: an indirect call on every probe, a hook in the library (#133), and D77 already rejected a runtime interface |
| C. link-time substitution | a fake calls file compiled in place of the system's | rejected: per build, not per test, so the real and the fake cannot be proven in one run |

The line it draws: wrap what only an operating system can answer, never the standard library
(`files.h`'s text classes have no calls and take no policy). The wrappers stay thin, one call each,
so a fake has nothing to imitate but return values; the real call's behaviour stays CI's to prove.

The smallest form that keeps the intention, as built:

- **A policy only where logic sits above the calls.** The loaders have it (the first name that
  opens, the newest match, Windows' widening and missing glob) and so has Linux's memory (statm's
  second field, the kilobyte peak, the larger of peak and present). macOS's and Windows' memory
  classes hand the system's reading on as it is, 0 when the call fails, so a fake of them would prove
  only itself: they stay their calls, and CI on their own systems proves them.
- **The system's own types.** A handle stays `void*`, as the system gives it; a fake's handles are the
  addresses of its own objects. A failed reading is 0, as the contract already says, so no wrapper
  needs an optional: Linux's peak of 0 makes the present reading win by the same comparison.
- **No concept for the calls.** The template's body is their contract, and the component's own
  concept, `library_loader` or `process_memory`, checks every instantiation, the real one in the
  header and each fake in the proofs.
- **Each proof can fail.** Seven defects planted one at a time in the logic — the first match taken
  for the newest, a name not widened, a later name kept over the first, a glob on Windows, statm's
  first field read, the peak taken as bytes, a peak allowed to trail — each fails a proof
  (2026-11-08; the glob on Windows survived a first version of the proofs, which asked for a pattern
  that matched nothing, and the proof now asks for an installed name).

CPython has the same shape where a subsystem is large (`thread_pthread.h` or `thread_nt.h`;
`posixpath` or `ntpath`, chosen once in `os.py`) and not in `Modules/posixmodule.c` (18,482 lines,
1,146 preprocessor conditionals, 3.14). Its two loaders, `dynload_shlib.c` and `dynload_win.c`, take
a file's name in different forms, so `importdl.c` asks the system again at the call: what a shared
concept rules out.

## What it does not cover

- **Compiler portability** — MSVC's constexpr step limit, Apple clang's late-parsed nested templates,
  `_MSC_VER` checks: build settings and code that every compiler accepts, not a system's rules.
- **Python's versions** — an exception set as `PyErr_SetObject(OSError, args)` that 3.10 matched by
  its type: the Python boundary's, the adapters'.
- **What a system's file systems do** — macOS refusing a name that is not UTF-8, or folding case: the
  rules can say so, but a test of the real system still meets it on that system.

## Sequence of work

Each step a refactor under the tests as they are (#36), its own commit, the ratchet lowered with it:

1. This document, the ratchet (`test_the_operating_system_is_asked_only_behind_the_platform_boundary`), and the
   conditionals `main` has, behind the boundary: `native.h`, `files.h` (a path's text), `memory.h`,
   `libraries.h` and `windows_headers.h`.
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
| platform/native.h | `native`: the running system's family, decided once | the choice |
| platform/files.h | a path's text: `posix_files` and `windows_files`, the `native_files` the family chooses, and `text_from_fs` and `fs_from_text` over it | rules |
| platform/memory.h | `process_memory`; `basic_linux_memory`, Linux's logic over its calls, proven over fakes everywhere (D138); `native_memory`, the running system's | logic, and the choice |
| platform/linux/memory.h | `linux_proc_calls`: one wrapper a call (the statm line, the page size, the peak in kilobytes); `linux_memory` over them | calls |
| platform/macos/, windows/memory.h | `macos_memory`, `windows_memory`: `resident_bytes`, `peak_resident_bytes`, the system's reading as it is | calls |
| platform/libraries.h | `library_loader`; `basic_posix_loader`, `basic_windows_loader`, the logic over each system's calls (D138); `native_loader`; `basic_library` and `library` over it: `first_of`, `last_match`, `function<F>`, `address_of` | logic, and the choice |
| platform/posix/, windows/libraries.h | `posix_dl_calls`, `win32_library_calls`: one wrapper a call; `posix_loader`, `windows_loader` over them | calls |
| tests/static/platform_proofs.cpp | every logic class over constexpr fake calls, compiled by every build of `pygim.utils`: the first name that opens, a missing library and symbol, the newest match and no match, Windows' widened name and missing glob, statm's resident count, the kilobyte peak, a peak below the present, a failed getrusage, procfs absent; and the running system's file-lock calls, compiled against their concept | the proofs |
| platform/locks.h | `file_lock_calls`: open, lock, unlock and close a file locked across processes, over an `int` descriptor on every system; `native_lock_calls`, the running system's. No caller on `main` yet | the choice |
| platform/posix/, windows/locks.h | `posix_lock_calls` (open, flock retried on EINTR, close), `win32_lock_calls` (_wsopen_s, LockFileEx and UnlockFileEx through the descriptor's handle, _close) | calls |
| platform/windows_headers.h | `<windows.h>` lean and without `min` and `max`, for every header that calls it | — |
| pathlike/core.h | `posix_strategy`, `windows_strategy`: separators, case, parsing; `native_strategy` | rules |
| persistence/.../bcp_api.h | `driver_files<F>`: the ODBC driver's library as each system names it | rules |

Arriving with the code that calls them, from PR #39's branch, each behind the same boundary; each
component's calls in the shape above, a class per system in a file of its own (D106):

| Header | What it will hold | Kind |
| --- | --- | --- |
| pathlike/path_rules.h | the path strategies, out of core.h | rules |
| platform/errors.h | an errno's words; `open_error_for<F>`, `open()`'s error for a folder; `os_error::of<F>`, what a system's error code tells Python's `OSError` (an errno, or Windows' own error, which Python maps) | rules and calls |
| platform/files.h | exclusive create, write, force, close, unlink, `file_size<F>` (a folder has none, on every system) | rules and calls |
| platform/home.h | `home_of(user)`: whose home `~` names; posixpath's and ntpath's rules | rules and calls |
| platform/listing.h | a directory's listing with each entry's kind (`directory`, `file`, `link`, `other`), `entry_at`, `finds_a_name_by_stat` | calls |
| platform/reads.h | `posix_reads`, `windows_reads` (posix/reads.h, windows/reads.h) behind the `file_reads` concept | calls |
| platform/process.h | a thread's name, the signal mask its threads inherit, `at_fork`, `process_id` | calls |
| platform/signals.h | `interrupt_handlers` (Ctrl-C, SIGTERM), `wakeup` (a pipe; an event) | calls |
| platform/caches.h | `system_caches`; the kernel's /sys list read by rules compiled everywhere | calls and rules |
| platform/memory.h | `advise_huge_pages`, on each system's memory class beside the probes (Q51) | calls |
| platform/clocks.h | `thread_cpu_ns`, `counts_thread_cpu_time` | calls |

## Open

| Question | Lean |
| --- | --- |
| Q42: should Windows' rules be usable on Linux from Python, as pathlib's PureWindowsPath is? | yes, later: with the rules as data it costs little, but it is a feature, not the boundary |
