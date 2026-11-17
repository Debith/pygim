# Errors: one home for every exception pygim raises

Status: built 2026-11-16 · Owner: Debith

Everything pygim raises is a `GimError`, and also the Python exception its contract needs, so
`except GimError` catches it all while `except KeyError` keeps working for code written against
Python's contracts. This page is about where those classes are made and how a module raises one.

## Scenarios

| What happens | What Python sees |
|---|---|
| `from pygim.errors import GimError` | the class every pygim module raises |
| `gimdict().set_default_strategy("AVG")` | `GimOptionError: Given input ``AVG`` not among: sum, max, min, replace`, a `ValueError` too |
| `except GimOptionError as e` | `e.value == "AVG"`, `e.options == ("sum", "max", "min", "replace")` |
| `import pygim.utils` in a fresh interpreter | `pygim.errors` imported by utils' load, nothing else of pygim |

## The classes

| Class | Also | Meaning |
|---|---|---|
| `GimError` (under `GimException`) | | everything pygim raises |
| `MissingKeyError` | `KeyError` | a lookup found nothing |
| `GimOptionError(value, options)` | `ValueError` | a value none of its options; `value` and `options` say which |
| `UnrecognizedTypeError(given_type, expected_types)` (under `DispatchError`) | | a value, or a type, that is none of the types expected |

One class per meaning, each also the builtin its meaning needs. A class with arguments keeps them
as its `args`, so a pickled error is made again the same way.

## Where they are made: `pygim.errors`

The classes are made once, by the C++ extension `pygim.errors` (`src/_pygim_fast/errors/`), so every
module raises the same class objects:

1. **Python** imports them from there: `from pygim.errors import GimError`.
2. **Another extension** takes them through the C table `pygim.errors` publishes: the capsule
   `pygim.errors._C_API`, laid out in `errors/table.h`. It takes the table once, as it loads
   (`pygim::errors::load()` in its `PYBIND11_MODULE`), never while running.
3. **It raises** one with the arguments Python would call the class with:
   `pygim::errors::raise(&table::option_error, value, options)` (`errors/adapter.h`).

Only plain C data crosses between the shared objects: the table's version and the classes, as the
interpreter's own objects, as `datetime` and NumPy publish theirs to other extensions. A reader built
for a newer table than the one it finds refuses it with `ImportError`; a new class goes at the
table's end, with the version raised.

## Imports run from Python to C++, never back

`pygim.errors` imports nothing of pygim, and no C++ code imports a pygim module by name:
`test_layering` fails on one. Every import between pygim's modules therefore runs one way, and none
can close a loop.

Until 2026-11-16 the classes were Python (`_pygim/_core/_exceptions.py`, exported by
`pygim.core.explib`). An extension that imported them as it loaded closed a loop with any of that
Python which imported the extension back: with `_error_msgs.py` importing such an extension,
`import pygim.core.explib` first failed with `ImportError: initialization failed` ("partially
initialized module 'pygim.core.explib' has no attribute 'MissingKeyError'"), and the other order
worked. Persistence made a second class named `GimError` besides, which `except GimError` did not
catch and nothing raised.

## Decided and rejected (2026-11-16)

- **The classes in C++, in a module of their own** (Debith: "There is no reason exceptions need to
  be defined in python side"). Rejected: a header every extension compiles, the first to load making
  the classes, which leaves which module made them to the load order; the classes inside an
  existing extension, which would make every module that raises load that one.
- **`pygim.core.explib` removed**, not kept as a re-export: one place to import errors from.
- **The classes nothing used removed**: `EntangledError` and its two subclasses,
  `ShaSumTargetNotFoundError`, and the message helpers `type_error_msg` and `file_error_msg`.
  `UnrecognizedTypeError` stays and is used for "takes X, not Y" (Debith, "Always. Dogfood.").
- **`GimOptionError(value, options)`** keeps its arguments and its message, written in C.
  `GimException`'s message as a list joined by `sep`, which nothing used, is gone: the classes take
  arguments as Python's own do.
- **A message is the same in every build** (Debith, 2026-11-17). Rejected: a
  detail level that made messages vaguer in a release, so that an attacker learns less. Its default
  would have come from `python -O`, and the application's wiring could set it. What reaches a user
  is the application's to filter, as Django does with `DEBUG = False`. A vaguer message would also
  take the detail out of the operator's logs, where an incident is debugged. And few deployments run
  with `-O`, so the default would seldom have applied. What a message must not say, it never says,
  in any build: a URL or a connection string may carry a password, so an error names its kind or
  its host instead of repeating it.

## Open

- `UnrecognizedTypeError`'s shape: a `TypeError` base, its `DispatchError` parent, a subject in its
  message.
- What replaces `type_error_msg` and `file_error_msg` for assertions.
- Persistence's failures as a class of their own here, a `GimError` and a `RuntimeError`.
