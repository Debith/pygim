# pathlike: the open engine registry

How `pygim.path(...)` knows which decoder handles a file, why that set of
decoders is open, and what the build proves about it on every compile.

## The rule

**Adding a file format is adding one file.** A format is a header in
`src/_pygim_fast/pathlike/adapter/engines/`, and nothing else is edited: not a
table, not an enum, not a switch, not the Python bindings, not the tests that
sweep the registry.

```
adapter/engines/csv.h              <- the whole contribution
```

```cpp
namespace pygim::pathlike::engines {
struct csv {                                            // struct name == file stem
    static constexpr std::array<std::string_view, 1> exts{".csv"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{
        .name = "csv", .label = "fast-csv",
        .doc = "CSV via fast-csv: rows as lists, header row as keys.",
        .exts = exts, .aliases = aliases,
    };
    static py::object load(const file& f, detail::KeyCache& keys);   // pybind + parser live here
    static void write(const file& f, py::handle obj);
};
}
```

After the next build: `pygim.path("x.csv")` is a `pathlike.csvfile`,
`.engine == "fast-csv"`, `engine="csv"` and `engine="fast-csv"` both select it,
the error text `unknown engine: 'xml' (known: csv/fast-csv, json/simdjson, ...)`
lists it, the module docstrings describe it, `pathlike.ENGINES` reports it, and
the parametrised tests in `tests/unittests/test_pathlike.py` exercise it.
`pygim stubs` then refreshes the generated block of `pathlike.pyi` (a test
fails until it is run).

## The path value: an RFC 3986 URI behind a pathlib-shaped API

`file` holds a `uri` (`uri.h`): scheme, authority, root flag, and a list of
decoded segments — RFC 3986 decomposed as in Appendix B, recomposed as in
§5.3, normalised as in §6.2 only when asked. A **strategy** policy maps native
path text onto that value and back, the way pathlib's `PurePosixPath` and
`PureWindowsPath` do (pathlib calls this a *flavour*; here it is a strategy): `posix_strategy` ("/" separates, "//" is a preserved
root), `windows_strategy` (both separators; a drive is the first segment as in
`file:///C:/x`, a UNC host is the authority as in `file://srv/share/x`).
`file` is `basic_file<native_strategy>`; both strategies are stateless policy
types, so the Windows rules are provable on any host.

Consequences that are visible from Python:

- `str()` / `os.fspath()` return pathlib's normalised spelling (`a//b/` ->
  `a/b`, `./x` -> `x`, `""` -> `.`); equality and hashing compare the value.
- `pygim.path()` accepts `file://` URIs (decoded like `Path.from_uri`:
  `file:///abs/x`, `file://localhost/abs/x`; `file://host/share/x` is a UNC
  path on Windows and a ValueError on POSIX, as in pathlib 3.14); a relative
  file URI or any other `scheme://` raises ValueError.
- `.uri` renders an absolute path per RFC 3986 with percent-encoding of every
  non-pchar byte (`file:///a%20b`, `file://host/share/x`); a relative path
  keeps the `file://<path>` spelling.
- `with_name` / `with_suffix` validate their arguments like pathlib.

Where the RFC and pathlib disagree, pathlib wins for the path algebra and the
RFC governs only the text form: empty segments are collapsed and `..` is kept
when parsing *paths* (RFC `remove_dot_segments` is an explicit
`uri::normalized()`), `join` appends segments (RFC reference resolution would
replace the last one), and percent-encoding applies only when rendering or
parsing a URI.

The whole algebra — `name`, `stem`, `suffix`, `suffixes`, `parts`, `parent`,
`parents`, `joined`, `with_*`, `is_absolute`, `as_uri`, `repr`, `ext_key` —
is `constexpr`; only the filesystem half (`exists`, `read_bytes`, `glob`,
`mkdir`, `absolute`, `resolve`, ...) touches `std::filesystem`, at the OS
boundary. `tests/static/pathlike_parity_proofs.cpp` is **generated from
pathlib** (`gen_pathlike_parity_proofs.py`): one `static_assert` per fact
`PurePosixPath` / `PureWindowsPath` reports for the corpus, replayed against
both strategies at compile time in every build; a test asserts the committed
file matches the interpreter's pathlib. `pathlike_core_proofs.cpp` pins the
RFC parser/renderer/normaliser and the URI mapping rules.

**Compiler note.** The libstdc++ of GCC 13 and 14 cannot constant-evaluate a
short `std::string` that escapes a function into the assertion expression,
nor a string-holding struct returned by value straight into a member, nor
vector comparisons of temporaries. The code therefore builds values in place
(`parse_into`, `join_into`) and every proof helper returns a `bool` computed
inside a `consteval` function on a local object. With that discipline the
same proofs pass on GCC 13.4, GCC 14.3 and GCC 16 (verified).

## Mechanism

| Layer | File | Role |
|---|---|---|
| vocabulary | `pathlike/uri.h`, `pathlike/core.h` | pybind-free: the RFC 3986 `uri` value; `engine_info` (name, label, doc, extensions, aliases), `sv_list`; the strategies and `basic_file<Strategy>` (pins `const engine_info*`) |
| registry | `pathlike/engine_list.h` | pybind-free: `EngineMeta` concept, `engine_list<Es...>` (the extension and selector tables as `StaticRegistryCore` over `flat_storage`, inventories, `visit`/`for_each`, `resolve`, the proofs, `conflict_report`) |
| discovery | `setup.py::_apply_typelist` + `[extension.typelist]` in `ext.pathlike.toml` | globs `adapter/engines/*.h` (sorted by stem) into `build/gen/pathlike/pathlike_engines.gen.h`: the includes and `using Engines = engine_list<engines::json, ...>` |
| dispatch | `adapter/adapter.h` | `Engine` concept (adds `load`/`write`), `load()`, `write()`, `wrap()`, `bind_typed()`, `engines_record()` — every one a fold over the pack |
| bindings | `adapter/bindings.cpp` | includes the generated header; docstrings, error inventories, typed classes and `ENGINES` are derived; asserts the proofs on the real pack |
| proofs | `tests/static/pathlike_core_proofs.cpp` | the same predicates on synthetic packs, positive and negative |
| Python | `pygim/_stubs.py`, `pygim stubs` | renders the stub's generated block from `pathlike.ENGINES`; a test keeps it current |

Identity is the address of each engine's `static constexpr engine_info info`
(a C++17 inline variable: exactly one per program). `file` pins that pointer;
`engine_list::index_of` turns it into a pack index and `visit(i, f)` calls the
descriptor's static function — one dispatch primitive for reading, writing,
wrapping and binding.

## What every build proves

`static_assert(Engines::holds())` in `bindings.cpp` sweeps the real pack:

- names are `[a-z][a-z0-9_]*` (they become Python class names), labels and
  aliases are lower-case with no blanks, every engine has a doc sentence and at
  least one extension;
- extensions are lower-case with one leading dot (exactly what `ext_key()` can
  produce), and each belongs to exactly one engine;
- every `engine=` selector (name, label, alias) belongs to exactly one engine;
- every extension and every selector resolves back to its owner;
- the case-fold chain is exact and the near-misses stay unknown — generated
  from the table (`.YAML`, `yaml`, `.yaml `, `YAML`, `.yaml`), not hand-listed.

`all_implemented(Engines{})` instantiates the `Engine` concept per descriptor,
so a header with the wrong `load`/`write` signature fails naming that engine.

With reflection (GCC 16, `-freflection`) two more proofs run:
`std::is_same_v<Engines, reflected_engines_t<engine_list>>` (the generated
list is exactly the set of engine structs the compiler sees) and
`names_match_identifiers(Engines{})` (every `info.name` is its struct's
identifier).

Under C++26 the assertion's message is `Engines::conflict_report()`, e.g.
`'.json' is claimed by both 'json' and 'csv'` — the one C++26 feature in use
(P2741, gated on `__cpp_static_assert >= 202306L`). CI builds that degrade to
C++23 get a fixed message and the same proof.

`tests/static/pathlike_core_proofs.cpp` proves the predicates themselves: a
good synthetic pack passes and answers every lookup correctly, and each broken
pack (duplicate extension, upper-case extension, two-dot extension, selector
clash, bad name, self-alias, missing doc, ...) fails exactly the predicate that
should catch it, with the expected report text.

## Why this shape

- **Why a generated header, and what reflection adds.** The glob is the
  portable source of the pack: CI degrades the `c++26` flag to `c++23` on GCC
  13 and Apple clang, and C++ has no directory include, so the engine headers
  must be `#include`d from a generated file regardless. P2996 static
  reflection (GCC 16 with `-freflection`, which `setup.py` passes whenever the
  compiler accepts it — `flags_if_supported` in the manifest) is used for what
  it is uniquely good at: an independent enumeration. Under
  `PYGIM_PATHLIKE_REFLECTION` (GCC 16 predefines `__cpp_impl_reflection`, the
  standard spelling is `__cpp_reflection`; both are honoured), `engine_list.h` defines
  `reflected_engines_t<engine_list>` (every class type in namespace
  `engines`, sorted by identifier) and `names_match_identifiers()`, and
  `bindings.cpp` asserts that the generated list equals the compiler's view
  and that each engine's `info.name` is its struct name. A stray engine struct
  missing from the list, or a misnamed one, fails to build with a named
  assertion. Compilers without reflection build the same code without those
  two proofs.
  `-freflection` is passed through the manifest's `flags_if_supported`, probed
  together with the extension's `-std=` (GCC 16 only accepts it under
  `c++26`), in the same spirit as the existing `-std=` degradation: the code
  that ships is identical either way, only the proof set grows.
- **Compiler reality.** The conda gcc 14.3 has none of reflection, pack
  indexing, `#embed` or `= delete("reason")`. The system `/usr/bin/g++-16`
  (a GCC 16 trunk snapshot) has all of them, and reflection behind
  `-freflection`; build with `CXX=/usr/bin/g++-16 CC=/usr/bin/gcc-16` to get
  the reflection proofs. Pack indexing, expansion statements, `#embed` and
  `= delete("reason")` were evaluated and not adopted: none removes a line
  that C++23 folds do not already handle, so gating them would be decoration.
- **How `RegistryCore` IS used.** `wiring/registry/core.h` is written against
  the mapping toolkit's `storage` concept, so one core serves both phases:
  over `hash_storage` it is the run-time registry `Registry` and `Factory`
  use; over `flat_storage` (`StaticRegistryCore`) it is a literal type built in
  one constant evaluation. pathlike's extension and selector tables are such
  static registries, built from the engine pack; a second engine claiming a
  key is a thrown duplicate, i.e. a build error. What was rejected is the
  *self-registering* use of a runtime registry — static initialisers of
  otherwise unreferenced translation units filling a map at import — which
  would move every cross-engine invariant from the compiler to import time and
  relies on dynamic initialisation the standard permits to be deferred.
  Population stays asymmetric by nature: a compile-time registry must receive
  its complete set in one expression; only a run-time one can accumulate.
- **Why not a TOML manifest as the source of truth.** It would need a header
  *and* a manifest line, a generator that writes into tracked files under
  `src/`, and a Python-side re-statement of the C++ invariants. The directory
  is a better manifest: it cannot disagree with itself.
- **Why `sv_list` instead of `std::span`.** A three-member borrowed view that
  is trivially usable in constant expressions on every compiler in the matrix;
  MSVC's constexpr `std::span` history is the reason.

## Adding an engine — checklist

1. Create `adapter/engines/<name>.h` with the descriptor above (the struct
   name must equal the file stem; keep the implementation in `detail` if the
   struct name would shadow a namespace the implementation uses — see
   `toml.h`).
2. Rebuild. A duplicate extension or selector, a malformed name, or a missing
   `load`/`write` is a compile error naming the engine.
3. Run `pygim stubs` and commit `pathlike.pyi` with the header.
4. Add format-specific behaviour tests and a `docs/examples/pathlike/` example
   if the repo's norms call for them; the registry sweep tests need nothing.
