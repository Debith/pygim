# Unpacking pass — cluster H

[The report](../2026-09-28-unpacking-pass.generated.html) · design docs (persistence, pathlike, mapping, C++ linking, PlantUML, pathset) · 52 unpacked, 4 left as written, over two rounds.

| Term | Meaning |
|---|---|
| Pass cluster | One group of documents, edited by one agent under the shared instructions (`__notes__/unpacking/instructions.md`). |
| Unpacked | A passage whose compressed conceptual steps were rewritten as explicit statements, meaning preserved. |
| Left as written | A passage judged compressed whose intended meaning was ambiguous, so unpacking it would have required an assumption; logged instead of edited. |
| Round | Round 1 ran under the original rules; round 2 re-read every file under the owner's refined rules of 2026-09-28 — mechanisms stated rather than their endpoints, a measurement explained before its metric is named, logic never replaced by intent, and no explaining of standard notation. |
| Text before | The passage exactly as the document had it. |
| Text after | The passage as it reads now. |
| Steps made explicit | The conceptual steps a reader previously had to reconstruct, now stated in the text. |
| Candidate defect | A logged passage that reads as a contradiction, a stale claim or a missing definition — a finding about the content, not the wording; nothing was changed at these. |
| Where | The file the passage is in, and the nearest heading above it. |
| Passage | The compressed text, exactly as the document has it. |
| Why it could not be unpacked safely | The two readings, in a phrase — choosing between them would have meant adding an assumption. |
| Documents | The files assigned to the cluster's agent. |
| Every row | The cluster's evidence page: each unpack in full, and each passage left as written. |
| Unpacked, round 1 | Passages unpacked under the original rules. |
| Unpacked, round 2 | Passages unpacked when the file was read again under the owner's refined rules. |
| Outcome, 2026-09-28 | What became of the candidate defect when the owner ruled on it: the fix now in the source, or parked. |

## Every unpack of round 1, in full

### H1. `docs/design/persistence_architecture.md` — 4.1 Concepts as Core Contracts

| | |
|---|---|
| Text before | New backends satisfy the concepts — no core code changes. |
| Text after | A new backend is written to satisfy the concepts, and the<br>compiler checks that it does. Core code depends only on the concepts, so adding<br>a backend changes no core code. |
| Steps made explicit | a new backend implements what the concepts require; the compiler verifies the backend against the concepts; core code refers only to the concepts, never to concrete backends; therefore adding a backend edits no core code |

### H2. `docs/design/persistence_architecture.md` — 4.4 Format as Runtime Enum (Not Template Parameter)

| | |
|---|---|
| Text before | Format is a<br>runtime `enum class Format { Polars, Pandas }` member, yielding ONE template<br>instantiation per backend (not 2×). |
| Text after | Format is a<br>runtime `enum class Format { Polars, Pandas }` member. Had Format been a template<br>parameter, every backend would need one instantiation per format — two per<br>backend. As a runtime member, the format is chosen when the call runs, so each<br>backend needs exactly ONE template instantiation. |
| Steps made explicit | as a template parameter, Format would multiply instantiations: one per backend per format, i.e. two per backend; as a runtime member, the format choice happens when the call runs, not when the template is stamped out; therefore each backend needs exactly one template instantiation |

### H3. `docs/design/persistence_architecture.md` — 4.5 Template Instantiation at the Edge

| | |
|---|---|
| Text before | `test_bindings.cpp` uses `py::module_local()`<br>to avoid type conflicts with the production module. |
| Text after | Some of the C++ types `test_bindings.cpp` binds are<br>also bound by the production module, and loading both modules in one process<br>would make those registrations conflict. `py::module_local()` keeps the test<br>module&#x27;s bindings local to it, so the two modules can coexist. |
| Steps made explicit | the test module binds some of the same C++ types as the production module; loading both modules in one process would make those registrations conflict; py::module_local() keeps the test module&#x27;s bindings local to it; so both modules can be loaded together |

### H4. `docs/design/persistence_architecture.md` — 4.7 Portable Temporal Structs

| | |
|---|---|
| Text before | Instead, it defines<br>portable structs (`detail::DateStruct`, `detail::TimestampStruct`, `detail::Time2Struct`)<br>that are binary-compatible with their ODBC counterparts. |
| Text after | Instead, it defines<br>portable structs (`detail::DateStruct`, `detail::TimestampStruct`, `detail::Time2Struct`)<br>with the same byte layout as their ODBC counterparts, so core code can read a<br>buffer the ODBC driver filled through its own structs, without the ODBC headers. |
| Steps made explicit | core defines its own structs; those structs have the same byte layout as the ODBC ones; therefore a buffer the ODBC driver filled can be read through core&#x27;s structs; so core never includes ODBC headers |

### H5. `docs/design/persistence_architecture.md` — 6.1 Single-Connection BCP Save

| | |
|---|---|
| Text before | Subsequent batches: `rebind_columns()` — pointer-only update via `bcp_rebind_dispatch` (same schema) |
| Text after | Subsequent batches: `rebind_columns()` — the schema is unchanged across batches, so the type bindings from the first batch still hold and only the buffer pointers need updating, via `bcp_rebind_dispatch` |
| Steps made explicit | all batches of one save share the schema; therefore the first batch&#x27;s type bindings remain valid; only the buffer addresses change from batch to batch; so the rebind updates pointers only |

### H6. `docs/design/persistence_architecture.md` — 7.3 Stale Connection Retry

| | |
|---|---|
| Text before | 3. No proactive health checks (avoids latency on the common path) |
| Text after | 3. No proactive health checks — checking each connection before use would add latency to every load, including the common case where nothing is wrong; a stale connection is instead detected by its error and handled by this retry |
| Steps made explicit | a proactive health check would test each connection before use; that test would cost latency on every load; the common case is a healthy connection, so the cost would usually buy nothing; instead a stale connection announces itself as an error; the retry above handles that error |

### H7. `docs/design/persistence_architecture.md` — 10.2 Configurable Tuning Parameters

| | |
|---|---|
| Text before | - `packet_size` is capped at 16384 with `Encrypt=yes` (TLS record limit) |
| Text after | - with `Encrypt=yes` the packets travel inside TLS records, and a TLS record carries at most 16384 bytes — so `packet_size` is capped at 16384 |
| Steps made explicit | Encrypt=yes wraps the network packets in TLS records; a TLS record carries at most 16384 bytes; therefore packet_size cannot exceed 16384 |

### H8. `docs/design/pathlike_engine_registry.md` — The path value: an RFC 3986 URI behind a pathlib-shaped API

| | |
|---|---|
| Text before | `file` is `basic_file&lt;native_strategy&gt;`; both strategies are stateless policy<br>types, so the Windows rules are provable on any host. |
| Text after | `file` is `basic_file&lt;native_strategy&gt;`. Both strategies are stateless policy<br>types: nothing in them depends on the host, so a build on any platform can<br>instantiate `windows_strategy` and run its rules in the compile-time proofs —<br>the Windows rules are provable without a Windows machine. |
| Steps made explicit | the strategies are stateless policy types; nothing in them depends on the host platform; so any platform&#x27;s build can instantiate windows_strategy; the compile-time proofs then exercise the Windows rules there; hence Windows behaviour is proven without a Windows machine |

### H9. `docs/design/pathlike_engine_registry.md` — The path value: an RFC 3986 URI behind a pathlib-shaped API

| | |
|---|---|
| Text before | Where the RFC and pathlib disagree, pathlib wins for the path algebra and the<br>RFC governs only the text form: empty segments are collapsed and `..` is kept<br>when parsing *paths* (RFC `remove_dot_segments` is an explicit<br>`uri::normalized()`), `join` appends segments (RFC reference resolution would<br>replace the last one), and percent-encoding applies only when rendering or<br>parsing a URI. |
| Text after | Where the RFC and pathlib disagree, pathlib wins for the path algebra and the<br>RFC governs only the text form. Parsing a *path* collapses empty segments and<br>keeps `..`, as pathlib does; the RFC&#x27;s `remove_dot_segments` is not applied<br>there — it runs only as the explicit `uri::normalized()`. `join` appends<br>segments, as pathlib does; the RFC&#x27;s reference resolution would instead<br>replace the last one. And percent-encoding applies only when rendering or<br>parsing a URI. |
| Steps made explicit | rule: pathlib governs the path algebra, the RFC only the text form; path parsing collapses empty segments and keeps .., pathlib&#x27;s behaviour; the RFC&#x27;s remove_dot_segments is not applied at parse time; it exists only as the explicit uri::normalized(); join appends segments as pathlib does; RFC reference resolution would instead replace the last segment; percent-encoding happens only when a URI is rendered or parsed |

### H10. `docs/design/pathlike_engine_registry.md` — The path value: an RFC 3986 URI behind a pathlib-shaped API

| | |
|---|---|
| Text before | `tests/static/pathlike_parity_proofs.cpp` is **generated from<br>pathlib** (`gen_pathlike_parity_proofs.py`): one `static_assert` per fact<br>`PurePosixPath` / `PureWindowsPath` reports for the corpus, replayed against<br>both strategies at compile time in every build; a test asserts the committed<br>file matches the interpreter&#x27;s pathlib. |
| Text after | `tests/static/pathlike_parity_proofs.cpp` is **generated from<br>pathlib** (`gen_pathlike_parity_proofs.py`): the generator runs `PurePosixPath`<br>and `PureWindowsPath` over the corpus and writes one `static_assert` per fact<br>they report. Compiling that file replays every fact against both strategies,<br>so each build re-proves the parity at compile time. A separate test asserts<br>that the committed file still matches what the interpreter&#x27;s pathlib reports. |
| Steps made explicit | the generator runs PurePosixPath and PureWindowsPath over the corpus; for each fact pathlib reports it writes one static_assert; compiling the generated file replays every fact against both strategies; so every build re-proves parity at compile time; a separate test checks the committed file against what the current interpreter&#x27;s pathlib reports |

### H11. `docs/design/pathlike_engine_registry.md` — Core and adapter: the engine parses, the adapter materialises — settled 2026-09-23, not built yet

| | |
|---|---|
| Text before | **Scenario.** ENACT&#x27;s store reads two YAML files in C++, its vocabulary and its source<br>inventory, and a store&#x27;s core may not include pybind11. pathlike ships a rapidyaml engine, but an<br>engine is `load(file) -&gt; py::object`: parsing, and turning the result into Python, sit in one<br>function in a header that includes pybind11 (the example above says so: `pybind + parser live<br>here`). So ENACT wrote its own wrapper |
| Text after | **Scenario.** ENACT&#x27;s store reads two YAML files in C++: its vocabulary and its source<br>inventory. A store&#x27;s core may not include pybind11. pathlike ships a rapidyaml engine, but that<br>engine cannot serve such a core: an engine is `load(file) -&gt; py::object`, so parsing, and turning<br>the result into Python, sit in one function, in a header that includes pybind11 (the example<br>above says so: `pybind + parser live here`). So ENACT wrote its own wrapper |
| Steps made explicit | ENACT&#x27;s store reads two YAML files in C++; the store&#x27;s core may not include pybind11; pathlike&#x27;s engine couples parsing and Python conversion in one function, in a header that includes pybind11; therefore the shipped engine cannot serve that core; so ENACT duplicated the parsing wrapper itself |

### H12. `docs/design/pathlike_engine_registry.md` — Mechanism

| | |
|---|---|
| Text before | Identity is the address of each engine&#x27;s `static constexpr engine_info info`<br>(a C++17 inline variable: exactly one per program). `file` pins that pointer;<br>`engine_list::index_of` turns it into a pack index and `visit(i, f)` calls the<br>descriptor&#x27;s static function — one dispatch primitive for reading, writing,<br>wrapping and binding. |
| Text after | An engine&#x27;s identity is the address of its `static constexpr engine_info info`:<br>as a C++17 inline variable it exists exactly once per program, so one engine is<br>one address. A `file` records which engine handles it by pinning that pointer.<br>Dispatch is then two steps: `engine_list::index_of` turns the pinned pointer<br>into the engine&#x27;s index in the pack, and `visit(i, f)` calls that descriptor&#x27;s<br>static function. Reading, writing, wrapping and binding all go through this<br>one primitive. |
| Steps made explicit | each engine has one static constexpr engine_info; as a C++17 inline variable it exists once per program, so its address identifies the engine; a file pins that pointer to record its engine; index_of converts the pointer to the engine&#x27;s pack index; visit calls that descriptor&#x27;s static function by index; read, write, wrap and bind all use this one primitive |

### H13. `docs/design/pathlike_engine_registry.md` — Why this shape

| | |
|---|---|
| Text before | The glob is the<br>  portable source of the pack: CI degrades the `c++26` flag to `c++23` on GCC<br>  13 and Apple clang, and C++ has no directory include, so the engine headers<br>  must be `#include`d from a generated file regardless. |
| Text after | The glob is the<br>  portable source of the pack. C++ has no directory include, so some file must<br>  spell out the `#include` of every engine header; the generated file is that<br>  file. And it is needed regardless of reflection: CI degrades the `c++26`<br>  flag to `c++23` on GCC 13 and Apple clang, so the build cannot rely on<br>  reflection to enumerate the engines. |
| Steps made explicit | C++ cannot include a directory, so some file must list every engine header&#x27;s #include; the generated file is that list; reflection could enumerate the engines instead, but CI degrades c++26 to c++23 on compilers that lack it; so the build cannot rely on reflection for the pack; hence the generated file is needed regardless |

### H14. `docs/design/pathlike_engine_registry.md` — Why this shape

| | |
|---|---|
| Text before | `-freflection` is passed through the manifest&#x27;s `flags_if_supported`, probed<br>  together with the extension&#x27;s `-std=` (GCC 16 only accepts it under<br>  `c++26`), in the same spirit as the existing `-std=` degradation: the code<br>  that ships is identical either way, only the proof set grows. |
| Text after | `-freflection` is passed through the manifest&#x27;s `flags_if_supported`. The<br>  probe tests it together with the extension&#x27;s `-std=`, because GCC 16 accepts<br>  the flag only under `c++26` — probed alone it would be rejected. This is the<br>  same spirit as the existing `-std=` degradation: the code that ships is<br>  identical either way, only the proof set grows. |
| Steps made explicit | -freflection goes through flags_if_supported; GCC 16 accepts -freflection only when the c++26 standard flag is present; so probing the flag alone would report it unsupported; therefore the probe tests the flag together with the -std= flag |

### H15. `docs/design/pathlike_engine_registry.md` — Why this shape

| | |
|---|---|
| Text before | pathlike&#x27;s extension and selector tables are such<br>  static registries, built from the engine pack; a second engine claiming a<br>  key is a thrown duplicate, i.e. a build error. |
| Text after | pathlike&#x27;s extension and selector tables are such<br>  static registries, built from the engine pack. When a second engine claims a<br>  key already taken, the insert throws; the table is built in a constant<br>  evaluation, where a throw cannot be evaluated — so the duplicate fails the<br>  compile, a build error. |
| Steps made explicit | a second engine claiming a key makes the registry insert throw; the table is built inside a constant evaluation; a throw cannot be constant-evaluated; so the constant evaluation fails and the duplicate becomes a compile error |

### H16. `docs/design/pathlike_engine_registry.md` — Why this shape

| | |
|---|---|
| Text before | What was rejected is the<br>  *self-registering* use of a runtime registry — static initialisers of<br>  otherwise unreferenced translation units filling a map at import — which<br>  would move every cross-engine invariant from the compiler to import time and<br>  relies on dynamic initialisation the standard permits to be deferred. |
| Text after | What was rejected is the<br>  *self-registering* use of a runtime registry: each engine&#x27;s translation<br>  unit, otherwise unreferenced, carries a static initialiser that adds the<br>  engine to a shared map when the module is imported. That shape fails twice.<br>  First, every cross-engine invariant the compiler now checks would instead be<br>  checked at import time. Second, it relies on dynamic initialisation the<br>  standard permits to be deferred, so an unreferenced translation unit&#x27;s<br>  registration may not have run by the time the map is read. |
| Steps made explicit | the rejected pattern: each engine translation unit carries a static initialiser that adds the engine to a shared runtime map at import; those translation units are otherwise unreferenced; objection one: cross-engine invariants would then be checked at import time instead of by the compiler; objection two: the standard permits deferring such dynamic initialisation; so an unreferenced translation unit&#x27;s registration may not have run when the map is read |

### H17. `docs/design/pathlike_engine_registry.md` — Why this shape

| | |
|---|---|
| Text before | The directory<br>  is a better manifest: it cannot disagree with itself. |
| Text after | The directory<br>  is a better manifest: a separate manifest can disagree with the headers it<br>  lists, but the directory *is* the set of headers — there is no second<br>  statement to fall out of step. |
| Steps made explicit | a separate manifest is a second statement of the engine set; two statements can disagree; the directory is itself the set of headers; so with the directory as manifest there is no second statement to fall out of step |

### H18. `docs/design/pathlike_engine_registry.md` — Why this shape

| | |
|---|---|
| Text before | A three-member borrowed view that<br>  is trivially usable in constant expressions on every compiler in the matrix;<br>  MSVC&#x27;s constexpr `std::span` history is the reason. |
| Text after | A three-member borrowed view that<br>  is trivially usable in constant expressions on every compiler in the matrix.<br>  `std::span` would be the standard choice, but MSVC&#x27;s support for `std::span`<br>  in constant expressions has a history of defects — that history is why a<br>  small view of pygim&#x27;s own is used instead. |
| Steps made explicit | std::span would be the standard type for a borrowed view; MSVC&#x27;s constexpr std::span support has a history of defects; the tables must work in constant expressions on every compiler in the matrix; so pygim carries its own three-member view |

### H19. `docs/design/pathlike_engine_registry.md` — Adding an engine — checklist

| | |
|---|---|
| Text before | 1. Create `adapter/engines/&lt;name&gt;.h` with the descriptor above (the struct<br>   name must equal the file stem; keep the implementation in `detail` if the<br>   struct name would shadow a namespace the implementation uses — see<br>   `toml.h`). |
| Text after | 1. Create `adapter/engines/&lt;name&gt;.h` with the descriptor above (the struct<br>   name must equal the file stem). When the struct&#x27;s name matches a namespace<br>   the implementation must refer to, the struct&#x27;s own name shadows that<br>   namespace inside it, and the namespace can no longer be named there — keep<br>   the implementation in `detail`, outside the struct, as `toml.h` does. |
| Steps made explicit | the engine struct&#x27;s name must equal the file stem; inside the struct, its own name shadows an identically named namespace; so the implementation could no longer name that namespace from inside the struct; writing the implementation in detail, outside the struct, avoids the shadowing; toml.h is the existing example |

### H20. `docs/design/mapping_toolkit.md` — The mapping toolkit

| | |
|---|---|
| Text before | Everything here is `constexpr` end to end, so the same code is a<br>runtime table and a compile-time proof. |
| Text after | Everything here is `constexpr` end to end, so the same code serves<br>twice: at run time it is the table a component uses, and in a constant<br>evaluation it is the object the proofs assert their laws over. |
| Steps made explicit | everything is constexpr end to end; therefore the same code runs at run time as the actual table; and runs in constant evaluation as the object the proofs assert laws over |

### H21. `docs/design/mapping_toolkit.md` — Why concepts, not one container

| | |
|---|---|
| Text before | Those three properties are exactly what `storage` cannot express<br>  (owned-key `find`, caller-supplied value, mandatory `erase`) — which is why<br>  this is a second concept beside it rather than a storage engine. |
| Text after | Those three properties are exactly what `storage` cannot express:<br>  `storage` is probed by the key type it owns, an interner by borrowed bytes;<br>  `storage` takes its value from the caller, an interner assigns it; `storage`<br>  must offer `erase`, an interner may never remove. That is why this is a<br>  second concept beside `storage` rather than one of its engines. |
| Steps made explicit | storage is probed by the owned key type; an interner is probed by borrowed bytes living anywhere; storage takes its value from the caller; an interner assigns the value; storage must offer erase; an interner may never remove, since held ids must stay valid; these three mismatches are why interner is a second concept, not a storage engine |

### H22. `docs/design/mapping_toolkit.md` — The components

| | |
|---|---|
| Text before | `count_united` &amp; co answer the SIZE of an algebra result as a popcount over the bitmaps, 64 ids per step, no set built (20-90x the build at 1M paths) |
| Text after | `count_united` &amp; co answer the SIZE of an algebra result as a popcount over the bitmaps, 64 ids per step, no set built — at 1M paths, 20-90x faster than building the result set and counting it |
| Steps made explicit | the counts are computed as popcounts over the bitmaps, without building the result set; the comparison baseline for the speedup is building the result set and counting it; at 1M paths the popcount route is 20-90x faster than that build |

### H23. `docs/design/mapping_toolkit.md` — `basic_id_set` by example

| | |
|---|---|
| Text before | For the default `id_set`, id 70 is bit 6<br>of word 1; a word one side does not have is treated as all zeros. |
| Text after | For the default `id_set`, whose words are<br>64 bits wide, id 70 is bit 6 of word 1: dividing the id by 64 names the word,<br>the remainder names the bit. A word one side does not have is treated as all<br>zeros. |
| Steps made explicit | the default id_set&#x27;s word is 64 bits (uint64_t); dividing an id by 64 names its word; the remainder names the bit within that word; so id 70 is word 1, bit 6 |

### H24. `docs/design/mapping_toolkit.md` — Laws (what `mapping_proofs.cpp` asserts)

| | |
|---|---|
| Text before | instruction 1.1 ns — so the flag, not a table, is the fix; MSVC and ARM pick<br>the instruction on their own. |
| Text after | instruction 1.1 ns — so the fix is the flag, which lets `std::popcount`<br>compile to the instruction, not a lookup table of our own; MSVC and ARM pick<br>the instruction on their own. |
| Steps made explicit | without -mpopcnt, std::popcount compiles to a library call at 3.4 ns per word; the hardware instruction beats every measured software form (1.1 ns vs 2.1 and 1.6); the flag lets std::popcount compile to the instruction; so passing the flag, not adding a lookup table, is the fix |

### H25. `docs/design/cpp_runtime_linking.md` — Why `-static-libstdc++`

| | |
|---|---|
| Text before | The extensions are built with a newer GCC (conda&#x27;s `gxx_linux-64`, or the<br>manylinux toolchain in CI) than the libstdc++ available at runtime in some<br>target environments. Statically bundling the runtime means an extension<br>built with GCC *N* still loads on a system whose shared libstdc++ predates<br>GCC *N*&#x27;s symbols. |
| Text after | The extensions are built with a newer GCC (conda&#x27;s `gxx_linux-64`, or the<br>manylinux toolchain in CI) than the libstdc++ available at runtime in some<br>target environments. Linked dynamically, an extension built with GCC *N*<br>would ask the target system&#x27;s shared libstdc++ for symbols that first appear<br>in GCC *N*&#x27;s runtime; on a system whose libstdc++ predates them, the load<br>fails. Statically bundling the runtime removes the demand: the extension<br>carries its own copy, so it still loads there. |
| Steps made explicit | the build GCC is newer than some target systems&#x27; libstdc++; a dynamically linked extension would request symbols that first appear in the newer runtime; an older system libstdc++ lacks them, so the load fails; static bundling makes the extension carry its own runtime copy; no demand is made of the system library, so the extension loads |

### H26. `docs/design/cpp_runtime_linking.md` — How Arrow libraries are resolved

| | |
|---|---|
| Text before | Path 3 is what makes **editable installs** work: the extension sits in<br>`src/pygim/`, so `$ORIGIN/../pyarrow` points nowhere. Therefore<br>`pygim.persistence` and `create_df` import `pyarrow` *before* touching the<br>extension, and `tests/conftest.py` preloads it before any test module. |
| Text after | Path 3 is what makes **editable installs** work. In an editable install the<br>extension sits in `src/pygim/`, so `$ORIGIN/../pyarrow` points nowhere; the<br>extension then depends on `libarrow` already being loaded when it is. That is<br>why `pygim.persistence` and `create_df` import `pyarrow` *before* touching<br>the extension, and `tests/conftest.py` preloads it before any test module:<br>importing `pyarrow` brings `libarrow` into the process, and the extension&#x27;s<br>load reuses it. |
| Steps made explicit | in an editable install the extension is in src/pygim/, so the RPATH $ORIGIN/../pyarrow points nowhere; the extension then depends on libarrow already being loaded at its own load time; importing pyarrow loads libarrow into the process; so pygim.persistence, create_df and conftest import pyarrow before touching the extension; the dynamic loader then reuses the already-loaded libarrow |

### H27. `docs/design/pathlike_writes.md` — Today

| | |
|---|---|
| Text before | forces neither the file nor the folder; the temporary name is fixed, so it is safe only under the commit lock, which the store&#x27;s constructor does not take; nothing removes a leftover |
| Text after | forces neither the file nor the folder; the temporary name is fixed, so two unlocked writers would share one temporary file — it is safe only under the commit lock, and the store&#x27;s constructor writes without taking that lock; nothing removes a leftover |
| Steps made explicit | the temporary file name is fixed; two writers without a lock would therefore share one temporary file and interleave; so the helper is safe only when writers are serialised by the commit lock; the store&#x27;s constructor writes without taking that lock |

### H28. `docs/design/pathlike_writes.md` — Making it the only way the store writes

| | |
|---|---|
| Text before | today&#x27;s helpers fail it at the first step. |
| Text after | today&#x27;s helpers fail it at the first step — `write_atomically` never forces the<br>   temporary file. |
| Steps made explicit | the asserted order starts with forcing the object&#x27;s temporary file to the disk; write_atomically forces neither the file nor the folder; so today&#x27;s helpers fail the ordering test at that first step |

### H29. `docs/design/pathlike_writes.md` — In Python

| | |
|---|---|
| Text before | Not in Python yet, as sequencing rather than a decision against: a way to write in pieces<br>(`pygim.path` has no `open()`, and every caller so far writes a whole value — the C++<br>`AtomicWrite` already streams), and a durable append (nothing in Python appends). When a caller<br>needs either, it takes the same shape: an option on a write, not a new name. Never `replace`:<br>pathlib&#x27;s `Path.replace(target)` renames, and `pygim.path` keeps pathlib&#x27;s meanings. |
| Text after | Two things are not in Python yet, and their absence is sequencing rather than a decision against<br>them. A way to write in pieces is not there because `pygim.path` has no `open()` and every caller<br>so far writes a whole value — the C++ `AtomicWrite` already streams. A durable append is not<br>there because nothing in Python appends. When a caller needs either, it takes the same shape: an<br>option on a write, not a new name. The name `replace` is never used for this: pathlib&#x27;s<br>`Path.replace(target)` means rename, `pygim.path` keeps pathlib&#x27;s meanings, and an atomic write<br>under that name would give it a second one. |
| Steps made explicit | two capabilities are absent: piecewise writes and durable appends; their absence is ordering of work, not rejection; piecewise writes: pygim.path has no open() and every caller writes whole values; the C++ AtomicWrite already streams; durable append: nothing in Python appends yet; when either is needed, it arrives as an option on an existing write, not a new method; replace is excluded: pathlib&#x27;s Path.replace means rename, pygim keeps pathlib&#x27;s meanings, and reusing the name would give it a second meaning |

### H30. `docs/design/pathlike_writes.md` — Open

| | |
|---|---|
| Text before | decided with the first implementation; both remove a surprise from the table above, and neither can carry the owner without privileges |
| Text after | decided with the first implementation; each would remove one surprise from the difference table above — the permissions row, the symbolic-link row — and even then the owner cannot be carried over without privileges |
| Steps made explicit | the two options correspond to two rows of the visible-difference table: permissions and symbolic links; adopting each option would remove that row&#x27;s surprise; the owner still cannot be carried over, because changing an owner requires privileges |

### H31. `docs/design/plantuml_relationship_pattern.md` — Ownership / Composition

| | |
|---|---|
| Text before | Use composition or aggregation to show stored lifetime relationships. Direction may be<br>chosen for layout clarity; the semantic payload is the diamond, not an upward orientation. |
| Text after | Use composition or aggregation to show stored lifetime relationships. Here the ownership<br>meaning is carried by the diamond marker, not by which way the arrow points — so an<br>ownership arrow does not need to point upward, and its direction may be chosen for<br>layout clarity. |
| Steps made explicit | in composition and aggregation the diamond marker carries the ownership meaning; arrow direction therefore carries no ownership semantics; upward direction is reserved for hierarchy elsewhere in this pattern; so an ownership arrow need not point upward and may route for layout |

### H32. `docs/design/pathset_storage.md` — The rule

| | |
|---|---|
| Text before | The table is append-only, so a row never<br>dangles. |
| Text after | The table is append-only: a row is never removed<br>and never renumbered, so a row id held anywhere stays valid — it never<br>dangles. |
| Steps made explicit | append-only means rows are never removed and never renumbered; so a row id stored anywhere keeps referring to the same row; hence a row never dangles |

### H33. `docs/design/pathset_storage.md` — The rule

| | |
|---|---|
| Text before | A `pygim.path` object is a handle `(table, row, pin)` (`adapter/pathview.h`):<br>name, parent, suffix, equality and hash are read by row; the core value<br>(`basic_file`, `core.h`) is built on demand for the operations whose rules<br>live there and interned back. |
| Text after | A `pygim.path` object is a handle `(table, row, pin)` (`adapter/pathview.h`).<br>Name, parent, suffix, equality and hash are read straight from the table, by<br>row. For the operations whose rules live in the core value (`basic_file`,<br>`core.h`), the handle builds that value from the row on demand, applies the<br>operation there, and interns the result back into the table. |
| Steps made explicit | cheap operations (name, parent, suffix, equality, hash) read the table directly by row; some operations&#x27; rules are defined in the core value basic_file; for those the handle builds the core value from the row on demand; the operation is applied in the core; the result is interned back into the table |

### H34. `docs/design/pathset_storage.md` — The rule

| | |
|---|---|
| Text before | A `PathStore`<br>(`adapter/path_store.h`) is a table with a Python face: `path(text, store=s)`<br>puts the row in s&#x27;s table and every derived path stays there, so a lifetime<br>is chosen by argument. |
| Text after | A `PathStore`<br>(`adapter/path_store.h`) is a table with a Python face. `path(text, store=s)`<br>puts the row in s&#x27;s table, and every path derived from it stays in that table<br>too — so the argument chooses the table a path and its derivations live in,<br>and choosing the table is choosing the lifetime. |
| Steps made explicit | path(text, store=s) inserts the row into s&#x27;s table; every path derived from it stays in that same table; so the argument chooses the table a path and its derivations live in; and the table chosen determines the lifetime |

### H35. `docs/design/pathset_storage.md` — The rule

| | |
|---|---|
| Text before | Semantics come from the core, not from string tricks: a strategy&#x27;s<br>`tokenise()` feeds exactly what `parse_into()` produces, `value(row)` is the<br>uri `file(text)` holds, `hash(row)` is that value&#x27;s hash, and `render(row)`<br>its text — proven in constant evaluation over the flat interner in<br>`tests/static/pathlike_core_proofs.cpp`. |
| Text after | Semantics come from the core, not from string tricks. The strategy&#x27;s<br>`tokenise()`, which feeds the table its segments, produces exactly what the<br>core&#x27;s `parse_into()` produces; `value(row)` is the uri `file(text)` holds;<br>`hash(row)` is that value&#x27;s hash; and `render(row)` is its text. Each of<br>these equalities is proven in constant evaluation over the flat interner in<br>`tests/static/pathlike_core_proofs.cpp`. |
| Steps made explicit | the strategy&#x27;s tokenise() is what feeds the table its segments; it produces exactly the segments the core&#x27;s parse_into() produces; value(row), hash(row) and render(row) equal the core file&#x27;s uri, hash and text; each equality is proven in constant evaluation in pathlike_core_proofs.cpp |

### H36. `docs/design/pathset_storage.md` — What it buys, measured

| | |
|---|---|
| Text before | Bulk operations and derived paths are where the table wins; a handle per<br>element still costs a Python object, so `scan()` reuses one for a pass. |
| Text after | Bulk operations and derived paths are where the table wins. A handle per<br>element still costs a Python object each, so `scan()` creates one handle and<br>re-points it at each row for the length of a pass. |
| Steps made explicit | iterating with a fresh handle per element costs a Python object per element; that per-object cost is what the bulk wins would drown in; so scan() creates one handle and re-points it at each row for the pass |

### H37. `docs/design/pathset_storage.md` — No Arrow boundary

| | |
|---|---|
| Text before | The path column is not zero-copy — the trie must be<br>rendered first, at ~125 ms per million paths, more than pyarrow needs to<br>build the same array from Python strings — and only the dictionary-encoded<br>names column truly was. |
| Text after | The path column is not zero-copy: a path exists as trie<br>rows, so its text must be rendered before it can be handed over, and that<br>render costs ~125 ms per million paths — more than pyarrow needs to build<br>the same array from Python strings. Only the dictionary-encoded names column<br>truly was zero-copy. |
| Steps made explicit | a path is stored as trie rows, not as contiguous text; so exporting the path column requires rendering the text first; that render costs ~125 ms per million paths; which already exceeds what pyarrow needs to build the same array from Python strings; only the dictionary-encoded names column was truly zero-copy |

### H38. `docs/design/pathset_storage.md` — No Arrow boundary

| | |
|---|---|
| Text before | The list bridge (`to_list()` then a polars Series)<br>was a quarter slower than the Arrow route, and linking `libarrow` would<br>inherit the persistence extension&#x27;s SONAME and runtime-seam problems<br>(`cpp_runtime_linking.md`). The export was removed; `to_list()` is the way<br>out. |
| Text after | The list bridge (`to_list()` then a polars Series)<br>was a quarter slower than the Arrow route. But keeping the Arrow route means<br>linking `libarrow`, and that link would inherit the persistence extension&#x27;s<br>SONAME and runtime-seam problems (`cpp_runtime_linking.md`). The export was<br>removed; the slower `to_list()` is the way out. |
| Steps made explicit | measured alone, the list bridge is a quarter slower than the Arrow export; but keeping the Arrow export requires linking libarrow; that link inherits the SONAME and runtime-seam problems documented in cpp_runtime_linking.md; so the export was removed and the slower to_list() is the accepted way out |

### H39. `docs/design/pathset_storage.md` — Rules to keep

| | |
|---|---|
| Text before | - **Append-only, single writer.** Rows are never removed or renumbered;<br>  whoever inserts holds the GIL. The table has no lock. |
| Text after | - **Append-only, single writer.** Rows are never removed or renumbered.<br>  Whoever inserts holds the GIL, and the GIL is what serialises the writers —<br>  the table has no lock of its own. |
| Steps made explicit | whoever inserts holds the GIL; the GIL is therefore what serialises writers; so the table itself needs no lock |

### H40. `docs/design/pathset_storage.md` — Open

| | |
|---|---|
| Text before | a regular expression on the name (once per<br>  distinct segment, since names are dictionary-encoded). |
| Text after | a regular expression on the name (names are<br>  dictionary-encoded — each distinct segment is stored once — so the<br>  expression runs once per distinct segment, not once per path). |
| Steps made explicit | names are dictionary-encoded: each distinct segment is stored once; so a regular expression need run once per distinct segment; not once per path that contains it |

## Every unpack of round 2, in full

### H41. `docs/design/persistence_architecture.md` — 4.2 Dialect-Based SQL Rendering

| | |
|---|---|
| Text before | `build_sql(Query, Dialect)` dispatches<br>at compile time. |
| Text after | `build_sql(Query, Dialect)` takes the<br>dialect as a type, so the compiler resolves which dialect&#x27;s rendering runs;<br>no dispatch is left for run time. |
| Steps made explicit | the dialect reaches build_sql as a type, not a value; a type is known to the compiler, so the compiler picks which dialect&#x27;s rendering is called; therefore no dispatch decision remains at run time |

### H42. `docs/design/persistence_architecture.md` — 4.12 LoadCache Pattern

| | |
|---|---|
| Text before | Invalidates when `conn_str` or `pool_size` changes. `NullLoadCache` is zero-cost<br>for non-MSSQL backends (empty struct). Eliminates 0.06–0.15s per-load connection<br>establishment overhead for repeated load operations. |
| Text after | Without the cache, each parallel load establishes its worker connections anew,<br>at 0.06–0.15s per load; with the pool kept alive, the next load reuses the<br>connections already established, so repeated loads pay that cost once. The<br>cache invalidates when `conn_str` or `pool_size` changes: the cached pool was<br>built with the old values, so it can no longer serve the new call.<br>`NullLoadCache` is zero-cost for non-MSSQL backends (empty struct). |
| Steps made explicit | without a cache, every load establishes worker connections anew, at 0.06–0.15s each; persisting the pool means the next load reuses connections already established; so the establishment cost is paid once across repeated loads; invalidation happens because a pool built with old conn_str or pool_size no longer matches the new call |

### H43. `docs/design/persistence_architecture.md` — 4.13 O(1) Dispatch Tables

| | |
|---|---|
| Text before | Zero branching in hot loops. Type resolution happens once during schema setup. |
| Text after | A column&#x27;s Arrow type is resolved once, during schema setup: the type indexes<br>the array, and the function pointer it selects is stored for that column. The<br>hot loops then call each column&#x27;s stored pointer directly, so no type branch<br>runs per row or per block. |
| Steps made explicit | at schema setup, each column&#x27;s Arrow type is used once as the index into the dispatch array; the function pointer that index selects is stored per column; the hot loop calls the stored pointer directly; therefore no type branch runs per row or per block |

### H44. `docs/design/pathlike_engine_registry.md` — What every build proves

| | |
|---|---|
| Text before | - extensions are lower-case with one leading dot (exactly what `ext_key()` can<br>  produce), and each belongs to exactly one engine; |
| Text after | - extensions are lower-case with one leading dot — exactly the form<br>  `ext_key()` can produce, so a table entry spelled any other way could never<br>  be matched by a lookup — and each belongs to exactly one engine; |
| Steps made explicit | lookups are keyed by ext_key()&#x27;s output; ext_key() can only produce lower-case with one leading dot; so an entry in any other spelling is unreachable, which is why the proof requires this spelling |

### H45. `docs/design/cpp_runtime_linking.md` — Incident: the libstdc++ interposition segfault (2026-09-01)

| | |
|---|---|
| Text before | (static archives are compiled without hidden visibility, so<br>`pybind11`&#x27;s `-fvisibility=hidden` does not cover them) |
| Text after | the linker copies the archive&#x27;s objects as they were<br>compiled, and libstdc++.a was compiled without hidden visibility —<br>`pybind11`&#x27;s `-fvisibility=hidden` applies only to the sources it compiles,<br>so it does not cover them |
| Steps made explicit | -fvisibility=hidden acts when a source is compiled, so it only covers the extension&#x27;s own sources; the archive&#x27;s objects were compiled earlier, without hidden visibility; the linker copies those objects as they are, so their symbols stay visible |

### H46. `docs/design/cpp_runtime_linking.md` — Incident: the libstdc++ interposition segfault (2026-09-01)

| | |
|---|---|
| Text before | which marks<br>symbols pulled from static archives (i.e. libstdc++.a) as local. Each<br>extension now uses its own bundled runtime consistently; |
| Text after | which marks<br>symbols pulled from static archives (i.e. libstdc++.a) as local. A local<br>symbol does not enter the extension&#x27;s dynamic symbol table, so the dynamic<br>loader can no longer resolve another module&#x27;s reference into the extension&#x27;s<br>copy, and the extension&#x27;s own references bind to its internal copy. Each<br>extension now uses its own bundled runtime consistently; |
| Steps made explicit | a local symbol is absent from the dynamic symbol table; so the dynamic loader cannot bind another module&#x27;s reference into the extension&#x27;s copy; and the extension&#x27;s own references bind internally, which is why each side uses its own runtime consistently |

### H47. `docs/design/plantuml_relationship_pattern.md` — Inheritance / Generalization

| | |
|---|---|
| Text before | Use when one type is a specialization of another. |
| Text after | (removed) |
| Steps made explicit | removed a notation explainer the owner ruled assumed knowledge |

### H48. `docs/design/plantuml_relationship_pattern.md` — Realization / Interface Implementation

| | |
|---|---|
| Text before | Use when a concrete type implements an interface, protocol, or abstract contract. |
| Text after | (removed) |
| Steps made explicit | removed a notation explainer the owner ruled assumed knowledge |

### H49. `docs/design/plantuml_relationship_pattern.md` — Usage / Dependency

| | |
|---|---|
| Text before | Use when one type calls, references, validates, builds, or otherwise depends on another.<br>These arrows do not imply hierarchy, so route them however best clarifies the picture. |
| Text after | These arrows do not imply hierarchy, so route them however best clarifies the picture. |
| Steps made explicit | removed a notation explainer the owner ruled assumed knowledge; kept the house routing rule, which is the section&#x27;s actual content |

### H50. `docs/design/plantuml_relationship_pattern.md` — Ownership / Composition

| | |
|---|---|
| Text before | Use composition or aggregation to show stored lifetime relationships. Here the ownership<br>meaning is carried by the diamond marker, not by which way the arrow points — so an<br>ownership arrow does not need to point upward, and its direction may be chosen for<br>layout clarity. |
| Text after | The ownership meaning is carried by the diamond marker, not by which way the arrow<br>points — so an ownership arrow does not need to point upward, and its direction may<br>be chosen for layout clarity. |
| Steps made explicit | removed a notation explainer the owner ruled assumed knowledge; kept the diamond premise because it justifies the house rule that ownership arrows are free to route |

### H51. `docs/design/pathset_storage.md` — The rule

| | |
|---|---|
| Text before | — so the argument chooses the table a path and its derivations live in,<br>and choosing the table is choosing the lifetime. |
| Text after | — so the argument chooses the table a path and its derivations live in.<br>A row is never removed, so it lives exactly as long as its table does, and<br>choosing the table is choosing how long those rows stay alive. |
| Steps made explicit | the store argument decides which table receives the row and its derivations; a row is never removed, so it exists as long as its table exists; therefore the choice of table is the choice of how long the rows live |

### H52. `docs/design/pathlike_writes.md` — Scenarios

| | |
|---|---|
| Text before | it gets complete lines only, numbered, and the half-written last one on its next read |
| Text after | it gets complete lines only, numbered; a half-written last line is held back, and a later read returns it once it is complete |
| Steps made explicit | a read returns only lines that are complete, each numbered; a last line still being written is not returned by this read; a later read returns that line once it has been completed |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/design/pathlike_engine_registry.md` — What every build proves | the case-fold chain is exact and the near-misses stay unknown — generated<br>  from the table (`.YAML`, `yaml`, `.yaml `, `YAML`, `.yaml`), not hand-listed. | either only an exact case-fold of a listed extension resolves and every other near-miss stays unknown, or the generated chain of derived spellings is itself what is checked to be exact; which of the five example spellings resolve is not decidable from the text |
| 1 | `docs/design/mapping_toolkit.md` — Next | The trie and the interners are the remaining non-templates over their id<br>  width; `basic_trie&lt;RowId, Key&gt;` and `basic_hashed_interner&lt;Id&gt;` would<br>  complete the pattern `basic_id_set` set. | the components table above already lists `basic_trie&lt;RowId, Key&gt;` and `basic_hashed_interner&lt;Id&gt;` as existing templates — and src/_pygim_fast/mapping/trie.h and intern.h confirm they are — so the bullet appears stale rather than a plan; &#x27;complete the pattern `basic_id_set` set&#x27; is also garbled, so the intended sentence cannot be reconstructed without assuming |
| 1 | `docs/design/pathset_storage.md` — What it buys, measured | 1M paths, best of 3, garbage collector off; &quot;objects&quot; is a list of path<br>handles, &quot;pathlib&quot; a list of `PurePath`. | the legend defines an &quot;objects&quot; column but the table&#x27;s columns are &quot;table&quot; and &quot;pathlib&quot;; either an objects column was dropped from the table or its data is folded into the &quot;+ ~140 per live handle&quot; note — cannot tell which without the benchmark |
| 2 | `docs/design/pathset_storage.md` — Open | Cross-table intersection still probes the other table once per member<br>(~170 ns/element); a merge that walks both tries in parent order would make<br>the probes sequential. | probes stay but become cache-friendly ordered accesses, or the walk replaces probing with an in-order merge altogether |
