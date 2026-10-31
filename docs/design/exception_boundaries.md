# Exception boundaries: where a C++ error becomes Python's

Status: built 2026-10-05 · Owner: Debith

How an error raised in pygim's C++ reaches Python: which Python type it arrives as, and the few
places C++ must catch for itself because pybind11 cannot.

## Scenarios

| What happens | What Python sees |
|---|---|
| `path("cfg.json").read()` on a file that does not parse | `ValueError: JSON parse error (cfg.json): ...` |
| a document that is not UTF-8, or a JSONL file whose last line was cut off mid-write | `ValueError`, naming the file (and the line) |
| `path("missing").read_bytes()`, a directory read, `mkdir` over an existing name | the `OSError` subclass pathlib raises for the same call: `FileNotFoundError`, `IsADirectoryError` (`PermissionError` on Windows), `FileExistsError` |
| `Container().resolve(IService)` with nothing registered; `registry["x"]`; `factory.create("x")` | `KeyError`, as a mapping raises |
| `container.register(I, P, lifecycle="forever")` | `ValueError` |
| a database connection or driver call fails | `pygim.persistence.PersistenceError` — a `RuntimeError` and a `pygim.core.explib.GimError` |
| a user's `class Mine(path)` raises in `__init__` | that exception, unchanged |
| a worker thread fails | an exception in the caller, never an aborted interpreter |

## The rule: pybind11 owns the call, C++ owns what pybind11 cannot see

pybind11 wraps every bound function in one try/catch and translates whatever it catches
(`translate_exception` in `pybind11/detail/internals.h`): `std::invalid_argument` to `ValueError`,
`std::out_of_range` to `IndexError`, `std::bad_alloc` to `MemoryError`, any other
`std::exception` to `RuntimeError`, and a `py::error_already_set` back to the Python exception it
holds. An adapter therefore never catches only to rethrow. C++ catches in four places, and only
there:

1. **A thread body.** An exception that leaves a `std::thread` calls `std::terminate`, and the
   interpreter dies before pybind11 is reached (exit 134, seen in the fetch benchmark). Every body
   catches everything into a `std::exception_ptr`; the caller rethrows the first after `join`.
2. **A destructor or `noexcept` function** — the same `std::terminate`. They catch everything.
3. **A C-API result.** A C-API call returns NULL with the Python error set, and pybind11
   translates exceptions, not NULLs: an empty object returned instead becomes pybind11's
   "Unable to convert function return value" `TypeError`, the real error demoted to `__cause__`.
   A NULL is always `throw py::error_already_set();`.
4. **Adding the subject** — the file, the line, the key (definition_of_done.md: every error names
   its subject). Catch, rethrow the *same type* with the subject appended, and let
   `py::error_already_set` and `py::builtin_exception` pass untouched.

## The type follows the meaning

The core throws a type that names what went wrong, pybind-free; the module registers how Python
sees it, once.

| Meaning | C++ (core) | Python |
|---|---|---|
| a bad value — an argument, a document that does not parse, text that is not UTF-8 | `std::invalid_argument` | `ValueError` |
| a file operation the OS refused | `pathlike::file_error` (errno, path) | `OSError(errno, strerror, filename)` — Python picks the subclass by errno |
| a lookup that found nothing | `core::missing_key` (`wiring/common/errors.h`) | `KeyError` |
| a failed persistence operation | `core::persistence_error` | `PersistenceError(GimError, RuntimeError)` |
| any other failed operation | `std::runtime_error` | `RuntimeError` |

**Registration is local to its module.** `py::register_local_exception` and
`py::register_local_exception_translator` apply to the functions of the module that registered
them. `py::register_exception` is global: every pygim extension shares one pybind11 internals, and
global translators are tried newest first, so one registered for a C++ type would rename every
other extension's errors of that type. A bare `py::exception<T>(m, ...)` makes a class and
registers nothing — nothing ever raises it. `wiring/common/adapter_errors.h`'s
`translate_locally<E>(python_type)` is the general form.

## What it costs

A `try` costs nothing on the path that does not throw (table-based unwinding). A local translator
adds one rethrow and catch to each exception its own module throws, and nothing to any other
module. `read_bytes` checks for a directory only when a read returned no bytes, so a file with
content pays no extra `stat`.

## What guards it

`tests/unittests/test_exception_boundaries.py`: a test per scenario above, through public names
only, and three scans of the C++ sources for the whole class of each seam — every thread body
catches, every stolen C-API result is checked, every registration is local. Each scan is run
against its own hard case.

## Decided and rejected (2026-10-05)

- **Rejected: catching in every adapter.** It repeats the try pybind11 already has, it cannot
  reach a thread or a NULL, and the obvious `catch (const std::exception&)` swallows
  `py::error_already_set`, turning a Python `KeyError` into a `RuntimeError`.
- **Rejected: keeping `RuntimeError` beside every new type.** A class with two bases cannot be the
  `OSError` subclass Python chooses by errno, and catching `RuntimeError` also swallowed
  `RecursionError` and `NotImplementedError`. The change is BREAKING for code that caught
  `RuntimeError` for these cases. `PersistenceError` keeps `RuntimeError` because a failed
  operation is what `RuntimeError` means.

## Open

- `std::runtime_error` sites whose meaning is a bad value or a wrong kind: persistence's "Invalid
  table identifier" and "unsupported type"; wiring's duplicate registration and override of a
  missing key.
- `_fetch_benchmark` ships in every wheel: an opt-in build flag, or `load` reporting its timing
  through a generic hook (#133).
