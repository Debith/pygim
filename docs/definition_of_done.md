# Definition of done

A change to pygim is done when every line below holds. The list is what the
pathlike, PathSet and mapping-toolkit work settled on; a PR that cannot tick a
line says so in its description and why.

## Where the code lives

- **Everything is C++26.** Behaviour lives in `src/_pygim_fast/`; Python under
  `src/pygim/` and `src/_pygim/` is routing glue (lazy exports, the CLI, stub
  generation), never logic. Python exposure is the minimum a user needs.
- **Three layers, one direction.** `core` headers are pybind-free and
  `constexpr` (provable); `adapter` headers hold pybind11 and the Python-facing
  types; `bindings.cpp` only registers. A core header never includes pybind11.
- **General by design.** A component is written as the general thing — a
  concept with engines, a template over its id, key, word or policy type —
  even while one instantiation exists, with a default alias for the common
  case. pygim is a library for learning advanced approaches; generality is the
  point, not something a second consumer has to earn.
- **One public entry per concept.** No two names for one thing (`file` and
  `path`), no factory beside a constructor. Variation is chosen in the adapter
  by argument (`store=`, `engine=`), never by ambient or global state, and
  never by coupling to the IoC container. Names do not clash with Python
  builtins. A typed subclass exists only where behaviour differs by type.
- **Superseded code and prose are removed**, not annotated. Git history keeps
  them. An old module is folded into the new one, not kept beside it.

## What proves it

- **Every invariant is a compile-time proof.** A law stated in a comment is a
  `static_assert` in `tests/static/*_proofs.cpp`, wired into an extension's
  sources so a build that breaks it does not link. Laws are proven over more
  than one instantiation (both engines, several widths).
- **The Python surface has unit tests** in `tests/unittests/`, including the
  edge corpus, cross-platform cases (a Windows path needs a drive to be
  absolute), and parity against the reference the feature mirrors (pathlib,
  the previous module's behaviour) where one exists.
- **A test names its own world.** Nothing a test reads may come from the machine it runs on —
  not the user's configuration, a shared directory, a running service, the clock or the working
  directory — or a green run says only that this machine happens to be in the right state. Where a
  real dependency *is* the thing under test (a real server over real pipes, a real database), it
  runs against test data it created: a temporary directory, a throwaway database, a local
  container. A test pointed at live data does not merely fail; it writes into someone's work.
- **A performance claim is a measurement.** Every number in a changelog,
  docstring or design note comes from a benchmark under `benchmarks/`,
  recorded through `_results.py` against the commit. An optimisation is
  justified by a same-process A/B (old and new compiled side by side), never
  by comparing runs on a machine whose load drifts.
- **A test that reads the repository skips without it.** CI removes `src/` and
  tests the installed package; a source-tree check (layering, stubs) must
  `skip` when the tree is absent rather than fail. A memory or timing
  measurement runs on a fresh heap (a subprocess), never against a heap that
  remembers what an earlier test freed.
- **CI is green on the full matrix** (3 OS x every supported Python) before
  the work is called done; after a push, the run is watched, not assumed.
  Compiler flags are per-extension `flags_if_supported`; there is no
  availability `#ifdef`. A build that lacks a dependency fails, never skips.

## What explains it

- **Every new header carries a worked example** in its file comment, and
  every public member a `///` comment giving what it does, what it costs, and
  the concrete result on that example. A documentation-only commit changes no
  code: the comment-stripped text is identical before and after.
- **Every new binding has a runnable example** under `docs/examples/` in the
  house style (a scratch directory, assertions, callouts pointing at the
  argument being introduced), listed in the examples README and executed by
  `tests/examples/`. Visual design notes (class or sequence diagrams) accompany
  a change of shape.
- **The stub is current.** `src/pygim/*.pyi` documents the public contract;
  the generated block is regenerated (`pygim stubs`) and a test enforces it.
- **The design note describes the present**, in `docs/design/`, and says why:
  the rule, what it buys (measured), the decisions taken and rejected in a
  paragraph each, the rules to keep, what is open. It does not narrate the
  history of superseded designs.
- **The changelog bullet describes what ships**, not the path to it, and says
  BREAKING when it is.

## How it is wired

Configuration is read in one place — the composition root — and passed down as a value. For the
commands that is `cli_oo`, for the server `enact.run`; both call `_pygim._config.from_process`,
and nothing below either of them looks at the environment, the working directory or the platform.
`_pygim._config.read` is the only function that inspects environment variables at all.

Three rules follow, and two of them are tests rather than conventions (`test_layering.py`):

- **Nothing reads configuration except the wiring.** A component that reads the environment cannot
  be built in a test without arranging the machine around it, and a test that has to arrange the
  machine is no longer exercising the code that ships.
- **A constructor sets values.** No logic, no loops, no IO. Anything that must be read is read by
  a builder — `enact.build` reads the clock, the files this process loaded and the vocabulary on
  disk, then hands the server plain values.
- **Tests wire with the same function production uses.** `build(where)` in a test, `build(where)`
  in `run`. What differs is the `Environment` passed in, which is configuration, which is exactly
  what is meant to differ.

Environment variables are legitimate at a process boundary — a reloading server cannot pass an
argument to its own successor — and the boundary is the wiring, never one layer in.

## What it leaves behind

An operation is not done when it returns the right answer; it is done when what it wrote is right
and what it did not write is absent. For anything that refuses, a test says so explicitly: the
audit log, the head views, the content objects and the vocabulary are byte-identical afterwards.
Prefer to check this from outside the process — `oo enact call` makes every operation reachable
from a shell, and the defects that survive in-process tests live in what a process does on its way
in and out (design 03 §3.5.2, §3.5.3).

## What it looks like

- **Output is read at a glance.** Anything a person reads — a command's output, a served page —
  gets a visible title, indentation that means something, and a blank line where the subject
  changes. Colour marks the few things worth acting on and is never the only signal: every
  coloured thing carries a word as well, because roughly one man in twelve cannot tell red from
  green (WCAG 2.2, Use of Color).
- **Colour is conditional output.** Off when the stream is not a terminal, when `NO_COLOR` is set
  to any value, when `TERM=dumb`, or when `--no-color` is passed — checked for stdout and stderr
  separately. The result goes to stdout, logs and errors to stderr, so a pipe carries the answer
  and the person still sees the rest (clig.dev, Output).

## What it says when it fails

- **Every error names its subject.** A parse error carries the file and the
  line, a rejected value the value and the rule it broke, a registry conflict
  the engine or key at fault — the way the engine registry's static
  assertions name the offending engine and the JSONL reader names the file
  and line. The Python exception type follows the meaning (ValueError for a
  bad value, TypeError for a wrong kind, RuntimeError for a failed
  operation), and the message is enough to act on without reading code.

## How it is delivered

- **A store, a set, a table reports its exact bytes** under the same `bytes`
  key in `stats()`; process-wide memory (`pygim.utils.rss_bytes`) is a
  benchmark probe, never a per-object size.
- **Lifetime and threading are stated** in the header: what is append-only,
  who is the single writer, what holds the GIL, what a handle keeps alive.
- **Commits explain why**, carry the co-author trailer, and never bundle
  unrelated work; PRs are stacked when one depends on another, each green on
  its own, and squash-merged. A PR body carries the summary, the measurements
  and the test plan.
