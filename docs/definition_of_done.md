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
