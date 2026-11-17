# Errors: one home for every exception pygim raises

Status: built 2026-11-16; pygim's own C++ errors 2026-11-17 · Owner: Debith

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
| `gimdict({"host": 1, "port": 2})["hots"]` | ```MissingKeyError: Key ``hots`` not among: host, port```, a `KeyError` too; `e.within` is the gimdict |
| `utils.calculate_rate(1, "XB", 1, "s")` | C++ with no Python throws `pygim::errors::option_error`; Python sees `GimOptionError`, a `ValueError` too |

## The classes

Each is a `GimError`, and also the builtin its meaning needs: where Python's own machinery catches
that builtin (`get()` and `in` catch `KeyError`, `getattr(o, n, default)` catches `AttributeError`,
iteration by index stops at `IndexError`) or where the type pygim stands in for raises it (pathlib's
`FileNotFoundError`, `json.dumps`'s `TypeError`). A meaning nothing in Python catches is `GimError`
alone.

| Class | Also | Meaning |
|---|---|---|
| `GimError` (under `GimException`) | | everything pygim raises |
| `MissingKeyError(key, within=None)` | `KeyError` | a lookup found nothing; `within` is where it looked |
| `GimOptionError(value, options)` | `ValueError` | a value none of its options; `value` and `options` say which |
| `UnrecognizedTypeError(given_type, expected_types, subject=None)` | `TypeError` | a value, or a type, that is none of the types expected, said by type or in words |
| `ArgumentError` | `TypeError` | a call pygim cannot take: an unknown keyword, an argument missing or too many |
| `SignatureError` | `TypeError` | a callable whose signature pygim cannot use (autowire, `get_schema`) |
| `UnsupportedColumnTypeError` | `TypeError` | an Arrow or SQL column type pygim cannot map |
| `InvalidValueError` | `ValueError` | the right type, outside what is accepted |
| `ParseError` | `ValueError` | text that is not its format, or bytes that are not UTF-8, naming the file and line |
| `DuplicateKeyError` | `ValueError` | a key registered twice; not a `KeyError`, which `get()` would swallow |
| `CircularDependencyError` | `ValueError` | wiring that depends on itself, as `graphlib.CycleError` |
| `InvalidStateError` | `RuntimeError` | an operation the object's state does not allow, and pygim's own "cannot happen" |
| `BackendError` | `RuntimeError` | ODBC, BCP or Arrow reported a failure |
| `GimOSError(errno, strerror, filename)` and `GimFileNotFoundError`, `GimFileExistsError`, `GimNotADirectoryError`, `GimPermissionError` | `OSError` and the one errno names | the system refused a file operation, as pathlib raises it |
| `GimOverflowError` | `OverflowError` | a quantity past what its type holds |
| `IndexOutOfRangeError` | `IndexError` | a position past the end |
| `MissingAttributeError` | `AttributeError` | an attribute that is not there |
| `DispatchError` | | a call could not be dispatched; nothing raises it since `UnrecognizedTypeError` left it |

A class with arguments keeps them as its `args`, so a pickled error is made again the same way.
`test_layering` refuses a raise of a builtin anywhere in pygim's C++ or Python: 151 on 2026-11-17,
none since.

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

## C++ with no Python throws pygim's own errors

Code with no Python, such as a core header, throws a class of pygim's own (`errors/core.h`): one per
meaning, each the counterpart of a `pygim.errors` class. A C++-only consumer catches it by class.
`errors::load()` registers a translator, local to the extension, that turns each into its Python
class as it reaches Python:

| C++ (`errors/core.h`) | Python |
|---|---|
| `pygim::errors::missing_key(key, keys)` | `MissingKeyError(key, within)`, `within` the keys as a tuple |
| `pygim::errors::option_error(value, options)` | `GimOptionError(value, options)` |
| `pygim::errors::error(message)` | `GimError(message)` |

Code that holds the Python values raises the Python class directly (`errors::raise`), so the
caller's own key object, not a C++ copy of it, is the error's `args[0]`.

The messages are made once, in `errors/core.h`: `what()` says them in C++, and the Python classes'
`__str__` call the same functions, so the two cannot drift apart. A missing key lists up to ten
keys; past that it says how many, and the closest by CPython's own "Did you mean" measure (a third
of the characters may change; nothing among 750 or more): ```Key ``hostt`` not among 13 keys;
closest: host```. Its keys are read only when the message is, so a miss that `get()` or `in` catches
costs no message.

What holds the design together:
- a C++ error holds plain data only: one that held a Python object would release it without the
  GIL when code that released the GIL threw it;
- an error is caught in the extension that threw it: each compiles its own copy of the core
  headers, and no C++ exception crosses between extensions;
- the translator is local to each extension: one registered globally would be added again by
  every extension's load, and each one adds a rethrow (measured: today's raise 2.9 µs, a
  translated one 3.5 µs, 1.4 µs more for each translator before it).

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

## Decided and rejected (2026-11-17)

- **pygim's own C++ exception classes**, translated by each extension (Debith: "can't we specify
  our own c++ exceptions that we throw?"). Rejected: an abstract factory with a Python family and a
  C++ family of errors, since a thrown exception already serves a C++-only consumer and there is
  no second family to choose; mapping the standard library's exceptions to pygim's classes, since
  `std::runtime_error`, 75 of the 107 standard throws, names no meaning.
- **`MissingKeyError(key, within)`**, `within` kept by reference and read when the message is, as
  `AttributeError.obj` is. Rejected: copying the keys when raising, which costs every miss that
  `get()` or `in` catches; a hand-written string naming where it looked, which drifts from the
  truth. A pickled one drops `within`, as a pickled `AttributeError` drops `obj`.
- **Every raise site converted** (Debith: "all"): each of pygim's raises becomes a class of
  `pygim.errors`, so `except GimError` holds for all of them.

## Open

- `DispatchError` is left with no subclass and nothing that raises it.
- What replaces `type_error_msg` and `file_error_msg` for assertions.
