# pygim examples

Runnable, self-verifying examples for the public `pygim` API. Every file is a
standalone script: it sets up everything it needs, backs every behaviour
claim with an `assert`, and prints a single `... OK` line at the end -- so
each example is documentation that is also a check: running the file executes
its asserts, and a claim the library no longer honours fails instead of
reading as if it were still true.

## Running

Run any example directly:

```bash
python docs/examples/ioc/example_01_basic_container.py
```

Or run them all, as the test suite does (`tests/unittests/test_examples.py`: each
example to its OK line, each `┌─ name:` callout over its argument, one fact an
`assert`, and lines under 100 characters):

```bash
python -m pytest tests/unittests/test_examples.py
```

## Index

| Area | Example | What it teaches |
| ------ | --------- | ----------------- |
| code | [example_01_parse_a_tree.py](code/example_01_parse_a_tree.py) | `code.parse()`: a tree of files walked, read and parsed in C++; a file that is not Python listed and raised on request; nodes as views with ast's kinds, fields and positions; `dump()` equal to `ast.dump`; bytes read in the encoding their cookie declares |
| datagen | [example_01_test_data.py](datagen/example_01_test_data.py) | `create_df` and `datagen.generate`: a schema of Arrow and SQL type names made into a table in C++, what each kind of column holds, the same seed's same data and another's different data, nulls by fraction, PyArrow when asked, what is refused |
| ioc | [example_01_basic_container.py](ioc/example_01_basic_container.py) | Registering providers, transient vs. singleton lifecycles, named variants, decorator registration, provider decorators, interface validation, introspection |
| ioc | [example_02_autowire.py](ioc/example_02_autowire.py) | Opt-in constructor autowiring from type hints across a multi-layer object graph, default-value fallback, guard rails |
| ioc | [example_03_testing_with_overrides.py](ioc/example_03_testing_with_overrides.py) | Swapping real implementations for fakes in tests, strict two-way override semantics, singleton cache invalidation |
| registry | [example_01_basic_registry.py](registry/example_01_basic_registry.py) | String- and object-keyed registration, strict override semantics, introspection, `find_id`, the identity policy |
| registry | [example_02_registry_with_hooks.py](registry/example_02_registry_with_hooks.py) | `on_register` / `on_pre` / `on_post` hooks, decorator registration, manual post triggering, capacity pre-reservation |
| factory | [example_01_basic_factory.py](factory/example_01_basic_factory.py) | Name-to-creator mapping, decorator registration, creation with arguments, override semantics, `use_module` plugin loading |
| factory | [example_02_interface_enforcement.py](factory/example_02_interface_enforcement.py) | Factories that validate every product against an interface at creation time |
| each | [example_01_broadcasting.py](each/example_01_broadcasting.py) | Broadcasting attribute reads and method calls over any iterable, argument forwarding, the dunder guard rail |
| pathlike | [example_01_read_a_config.py](pathlike/example_01_read_a_config.py) | One call from a path to native Python objects |
| pathlike | [example_02_typed_files.py](pathlike/example_02_typed_files.py) | `.engine` naming the decoding library (rapidyaml/simdjson/toml++); `yamlpath`/`jsonpath`/`tomlpath` types mirroring it through pins and derived paths |
| pathlike | [example_03_pathlib_parity.py](pathlike/example_03_pathlib_parity.py) | os.PathLike integration, name components, `/` composition |
| pathlike | [example_04_yaml_12_scalars.py](pathlike/example_04_yaml_12_scalars.py) | YAML 1.2 scalar typing: hex/octal/big ints, 1.1-isms staying strings, dot-form floats, quoting |
| pathlike | [example_05_json_and_toml.py](pathlike/example_05_json_and_toml.py) | Strict JSON with filename in errors; TOML with real datetime objects |
| pathlike | [example_06_engine_pinning.py](pathlike/example_06_engine_pinning.py) | Pinning an engine at construction, refusal to guess, pin inheritance, per-call override |
| pathlike | [example_07_writing.py](pathlike/example_07_writing.py) | write() round-trips for all three formats, trap-string quoting, non-finite float policies, TOML mapping roots |
| pathlike | [example_08_traversal.py](pathlike/example_08_traversal.py) | glob/rglob/iterdir, sorted+deduplicated results, pin inheritance, the PathSet bridge |
| pathlike | [example_09_parallel_and_key_cache.py](pathlike/example_09_parallel_and_key_cache.py) | GIL-released parallel reads, key_cache interning semantics and proof |
| pathlike | [example_10_jsonl.py](pathlike/example_10_jsonl.py) | JSON Lines read as a list (one item per document), blank lines ignored, a parse error naming its line, a list written back one line per item |
| pathlike | [example_12_path_stores.py](pathlike/example_12_path_stores.py) | `PathStore` and `store=`: the default table and a private one, derived paths inheriting their table, equality across stores without identity, a PathSet over a store, a lifetime chosen by argument |
| pathlike | [example_13_read_each.py](pathlike/example_13_read_each.py) | `PathSet.read_each()`: contents as views in member order while threads read ahead without the GIL, memoryview with no copy, `text()` on demand, what is left out and what raises, `threads=` and `ahead_bytes=`, `read_all_files()` |
| similarity | [example_01_find_duplicates.py](similarity/example_01_find_duplicates.py) | All-pairs comparison of one language's functions and classes as Arrow tables (joining pairs to units), `threshold` and `min_size`, what the score ignores (local names, docstrings, annotations, literal values) and what it does not, imports resolved to one call, classes through their best method pairing, large results (`limit`, `columns`, `batch_size`, `stream_pairs` to Parquet), `add_files` and one index per language |
| utils | [example_01_process_memory.py](utils/example_01_process_memory.py) | `rss_bytes` / `peak_rss_bytes` and the MiB forms: a before/after delta around an allocation, and why only the delta means anything |
| utils | [example_02_quick_timer.py](utils/example_02_quick_timer.py) | `QuickTimer` with its clock and report given: laps measured exactly on a `ManualTime`, `Report.hooks()` callbacks, the default stderr text |
| utils | [example_03_gimdict.py](utils/example_03_gimdict.py) | `gimdict`: values merged by strategy — numbers added, anything else replaced, a strategy per type and per key, `deep` and `union` for containers — and everything else a dict's |
| pathlike | [example_11_path_collections.py](pathlike/example_11_path_collections.py) | `PathSet` as one table: set semantics, `+`/`-` with paths or sets, `clone`, Filters (`ext`/`name`/`absolute`, `& | ~`) and lazy Queries, `read_all_files`, `match_pattern`, counting without building |
| persistence | [example_01_building_queries.py](persistence/example_01_building_queries.py) | `Query` and `MssqlDialect` with no database: the fluent builder, T-SQL rendering, raw SQL passing through, identifier quoting |
| persistence | [arrow_bcp_quickstart.md](arrow_bcp_quickstart.md) | Quickstart for the Arrow/BCP persistence layer (prose walkthrough, requires a database) |

## Conventions

- Files are ordered per area: `example_01_*` introduces the area, later
  numbers build on it.
- Every claim in a comment is backed by a nearby `assert`; if the library's
  behaviour changes, the example fails rather than silently going stale.
- One fact per `assert`, as `value == literal` with named values: `assert a and b`
  is two facts a reader has to take apart, and its failure names neither. Lines
  stay under 100 characters: an example is read before it is run.
- When a call introduces an argument for the first time, an arrow callout
  above the call explains it in place:

  ```text
  #                  ┌─ interface: what call sites ask for; also the lookup key
  #                  │           ┌─ provider: how to build it
  #                  ▼           ▼
  container.register(Repository, MemoryRepository)
  ```

  Later calls reuse the argument without re-explaining it. When the argument
  sits on the last line of a multi-line call, the callout goes below it
  instead, pointing up (`▲ ... └─`), so it never interrupts the call.
- Examples clean up any files they create and require nothing beyond an
  installed `pygim` (the persistence quickstart is the documented exception).
- Each file starts with `# type: ignore`. Part of the public surface carries
  stubs (`src/pygim/*.pyi` — pathlike, similarity, code, utils, core.testing — each
  held to its bindings by a test), but the rest of the compiled modules the examples
  call (registry, ioc, each, …) ship none yet, so a type checker would flag
  every call into those; the marker silences that, and the examples favour
  runtime-verified behaviour over static typing.
