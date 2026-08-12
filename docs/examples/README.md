# pygim examples

Runnable, self-verifying examples for the public `pygim` API. Every file is a
standalone script: it sets up everything it needs, backs every behaviour
claim with an `assert`, and prints a single `... OK` line at the end -- so
the examples double as living documentation that can be executed to verify
the library.

## Running

Run any example directly:

```bash
python docs/examples/ioc/example_01_basic_container.py
```

Or run them all:

```bash
for f in docs/examples/*/example_*.py; do python "$f" || break; done
```

## Index

| Area | Example | What it teaches |
| ------ | --------- | ----------------- |
| ioc | [example_01_basic_container.py](ioc/example_01_basic_container.py) | Registering providers, transient vs. singleton lifecycles, named variants, decorator registration, provider decorators, interface validation, introspection |
| ioc | [example_02_autowire.py](ioc/example_02_autowire.py) | Opt-in constructor autowiring from type hints across a multi-layer object graph, default-value fallback, guard rails |
| ioc | [example_03_testing_with_overrides.py](ioc/example_03_testing_with_overrides.py) | Swapping real implementations for fakes in tests, strict two-way override semantics, singleton cache invalidation |
| registry | [example_01_basic_registry.py](registry/example_01_basic_registry.py) | String- and object-keyed registration, strict override semantics, introspection, `find_id`, the identity policy |
| registry | [example_02_registry_with_hooks.py](registry/example_02_registry_with_hooks.py) | `on_register` / `on_pre` / `on_post` hooks, decorator registration, manual post triggering, capacity pre-reservation |
| factory | [example_01_basic_factory.py](factory/example_01_basic_factory.py) | Name-to-creator mapping, decorator registration, creation with arguments, override semantics, `use_module` plugin loading |
| factory | [example_02_interface_enforcement.py](factory/example_02_interface_enforcement.py) | Factories that validate every product against an interface at creation time |
| each | [example_01_broadcasting.py](each/example_01_broadcasting.py) | Broadcasting attribute reads and method calls over any iterable, argument forwarding, the dunder guard rail |
| pathset | [example_01_path_collections.py](pathset/example_01_path_collections.py) | Set semantics over filesystem paths, removal and cloning, bulk file reading, glob-style matching |
| pathlike | [example_01_reading_configs.py](pathlike/example_01_reading_configs.py) | One-call YAML/JSON/TOML decoding, YAML 1.2 scalar typing (hex/big ints, 1.1-isms stay strings), TOML datetimes, strict JSON errors |
| pathlike | [example_02_engines_and_writing.py](pathlike/example_02_engines_and_writing.py) | Engine pinning and precedence, write() round-trips with trap strings, non-finite float policy, TOML read-only |
| pathlike | [example_03_traversal_and_performance.py](pathlike/example_03_traversal_and_performance.py) | glob/rglob/iterdir with pin inheritance, the PathSet bridge, GIL-released parallel reads, key_cache interning |
| persistence | [arrow_bcp_quickstart.md](arrow_bcp_quickstart.md) | Quickstart for the Arrow/BCP persistence layer (prose walkthrough, requires a database) |

## Conventions

- Files are ordered per area: `example_01_*` introduces the area, later
  numbers build on it.
- Every claim in a comment is backed by a nearby `assert`; if the library's
  behaviour changes, the example fails rather than silently going stale.
- When a call introduces an argument for the first time, an arrow callout
  above the call explains it in place:

  ```text
  #                   ┌─ interface: what call sites ask for; also the lookup key
  #                   │           ┌─ provider: how to build it
  #                   ▼           ▼
  container.register(Repository, MemoryRepository)
  ```

  Later calls reuse the argument without re-explaining it. When the argument
  sits on the last line of a multi-line call, the callout goes below it
  instead, pointing up (`▲ ... └─`), so it never interrupts the call.
- Examples clean up any files they create and require nothing beyond an
  installed `pygim` (the persistence quickstart is the documented exception).
- Each file starts with `# type: ignore`: the compiled extension modules
  ship no type stubs yet, and the examples favour runtime-verified behaviour
  over static typing.
