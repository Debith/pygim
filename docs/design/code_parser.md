# The code component: Python parsed in C++

The first end-to-end version, built 2026-10-25 on `core/pathlike-batch-read`. It records what was
built, what was decided on the way and what else could have been chosen, so each decision can be
taken back up. Decisions made without Debith are marked *(taken)*, and their alternatives are
listed beside them.

## What it does

```python
from pygim import code

corpus = code.parse("Lib", "**/*.py")       # walk, read, parse: C++ only, the GIL released, Ctrl-C owned
corpus.stats()                              # {'files': 1846, 'failed': 4, 'nodes': 3321029, 'names': 81668, 'seconds': 0.064, ...}
corpus.errors                               # [(path, message, line, column), ...] for files that are not Python
tree = corpus["Lib/ast.py"]                 # a view: no Python object per node
cls = tree.find("ClassDef")[0]              # ast's kinds, fields and positions
cls.field("name"), cls.lineno, cls.source
tree.dump(include_attributes=True)          # == ast.dump(ast.parse(src), include_attributes=True) in 3.14
code.parse_source("x = 1")                  # one text, same Tree
```

One call starts the run, and Python only inspects what it produced (#270). What the views
return is made only on request: a constant's value, a name, a list of children.

## Where it lives

`src/_pygim_fast/code/` is the code component's (D27), which it shares with CodeBuilder (#259, on
`core/code-builder`): `python/` holds the parser (pybind-free), `corpus.h` the run, and
`adapter/corpus_bindings.h` the Python face, registered from `bindings.cpp` as CodeBuilder's
registers its own, so the two branches meet in one module with one call each. The decision
numbers below are this work's; D54-D59 were D32-D37 until 2026-10-30, the numbers #259 gave
CodeBuilder's own decisions.

## How it runs

```
code.parse(root, pattern)                  adapter/corpus_bindings.h: interrupt_scope, GIL released
 └─ corpus_run (corpus.h)                  one parallel::task_pool for all of it
      ├─ a directory task                  glob_step (pathlike/glob_walk.h): adds directories and file tasks
      └─ a file task                       native_reads, then parse on the same worker (#276: fused)
           └─ a workspace                  taken from a stack: scratch rows, a name table, finished files
                ├─ source_text             the encoding (PEP 263), UTF-8 checked, line starts
                ├─ lexer                   tokens on demand, a ring of 2 in the parser: the token and the next
                ├─ basic_parser            a facade (D60): module(), made of the parts below; rows as nodes close
                │   ├─ token_cursor        cursor.h: the current token and the next, pulled from the lexer
                │   ├─ the rules           grammar/: statements, compound, patterns, expressions (Pratt), atoms, strings
                │   └─ their helpers       grammar/: make_target, future_imports, string_pieces
                └─ syntax_tree             rows packed to preorder in one reverse pass
 collect()                                 names merged into one table on every core, trees renumbered, files sorted
```

The rows, on `a + b * c`. A node's row is written when the node closes, after its children's:

| step | rows written (postorder) | what is known |
| --- | --- | --- |
| 1 | `a` | a Name; its parent not yet |
| 2 | `a`, `b`, `c` | `*` binds tighter: `b` and `c` wait |
| 3 | `a`, `b`, `c`, `b*c` | BinOp, size 3: its subtree starts at `b` |
| 4 | `a`, `b`, `c`, `b*c`, `a+b*c` | BinOp, size 5; `left`/`right` set on its children |
| pack | `a+b*c`, `a`, `b*c`, `b`, `c` | one reverse pass: preorder (source order), each with its parent |

Nothing is built and then thrown away: the pegen spike made 3.2 nodes for every node it kept
(#276).

## Decisions

| Id | Decided | Alternatives, and why not now |
| --- | --- | --- |
| Q34, Q35 (Debith) | Python 3.14's grammar; stop at the first error | version gates for 3.8-3.13, then 3.15 (next); error recovery: the error handling is a policy type (`stop_at_first`), so a recovering policy can take its place |
| D24 *(taken)* | rows in postorder as nodes close, one reverse pass to preorder (`rows.h`, proven at compile time) | pegen's arena then a pack (#276: 3.2x the nodes); preorder insertion with reserved slots (the size is unknown when a node opens); a pointer tree (a heap box per node, as Ruff) |
| D54 *(taken)* | kinds, fields and their order generated from python3.14's `ast` module (`gen_ast_tables.py`; a test checks the header is current) | Python.asdl copied from CPython; a hand-written table |
| D55 *(taken)* | a row: kind u8, field u8, small u16, size, value, start, end (20 bytes; 24 packed, with the parent). The first identifier or literal is the row's `value`; one operator, context, flag or small integer is its `small`; a second identifier, a list of identifiers, the operators of a comparison are leaf rows (`identifier`, `cmpop`); a None inside a list (Dict keys, kw_defaults) is an `absent` row | side columns per field kind (more columns, the same information); the comparison's operators packed into its value (8 at most); a row per scalar always (about 40% more rows) |
| D56 *(taken)* | positions as byte offsets, line and column on request from each file's line starts | line and column in every row (16 more bytes a row) |
| D57 *(taken)* | a string's value as the pieces it was written in, decoded on request; escapes checked by the lexer, so a bad one is still a parse error | decoding into an arena while parsing (Ruff boxes every string) |
| D58 *(taken)* | the first error throws `syntax_error`; the two ambiguities a reader of two tokens meets — `with (` and a statement starting with `match` — take a checkpoint (the lexer's state is a fixed-size value, about 4 KB: its brackets, indentation levels and f-strings as bounded stacks, copied whole as a memento only the lexer reads) and rewind on failure | Ruff's token classification for `match`; re-reading the parsed items as an expression instead of rewinding |
| D59 *(taken)* | `f"a{x=}"`: the merged text `'ax='` is put in as a row before the field's subtree once the `=` is seen (`row_writer::insert`) | flushing the text first, then extending it (its pieces need not be contiguous) |
| D60 (Debith 2026-10-31: "Facade is ok, even preferable, but it should be split into meaningful components") | `basic_parser` is a facade over components, each its own header: `token_cursor` (`cursor.h`), `parse_output` (`output.h`), the rules in `grammar/` — `statement_rules`, `compound_rules`, `pattern_rules`, `expression_rules`, `atom_rules`, `string_rules`, each a class template on the parser deriving `part_of` (`grammar/part.h`), named helpers declared, another part's rule called as `parser().rule()` — and `make_target`, `future_imports`, `string_pieces`, which need no grammar. The parser derives the rules privately: its face is `module()` alone. In one process the split parses `/usr/lib/python3.14` in 75.0-76.1 ms, the one class it was (1,841 lines) in 76.0-77.4 (best of 15, one thread, three runs; the old header compiled from git into a nested namespace beside the new) | one class with its definitions spread over a header per grammar area (no components); parts holding references to each other (a load more on every token read); `this`-deducing members (C++23, P0847), which do what CRTP does but ask for GCC 14, Clang 18 or MSVC 19.32 — whether every compiler the wheels build with has them was not checked; CRTP asks nothing of the compiler |
| D61 (b), Debith 2026-10-31: "As you recommend (b)" | a tree's nodes are in its workspace's `chunk_arena` (`utils/chunk_arena.h`): 4 MB chunks from the process's kept blocks, handed out by a pointer bump, given back when the corpus dies; the corpus holds its workspaces' arenas. A fresh process's first parse had paid for the C library's per-thread heaps growing 128 KB at a time, each step an `mprotect` that stalls every other thread's page faults: 94 ms where a later parse took 52 (C++, 2026-10-30). `arena_allocator` moves and swaps with its container, so sorting the files moves no node. Under AddressSanitizer each allocation is unpoisoned as it is handed out, with a 32-byte redzone after it | (a) a scoped `mallopt(M_TOP_PAD, 64 MB)` for a run's length (glibc only; process-wide while it runs; a caller's own setting cannot be read back); (c) leave it. The file texts followed in D69 |
| D69, Debith 2026-10-31: "do build D69" | a corpus's files are read straight into its workspaces' arenas, and a tree's text and lines stay there with its nodes: pathlike's reads give an `arena_string` (a `std::string` whose allocator is an arena's, or the heap's without one), in the arena the caller names, so similarity's and `PathSet`'s reads, on the heap, still hand their bytes over uncopied. A tree is where its bytes are: `parse_python` takes no arena of its own. A worker leases its workspace before the read, not after. Each part is made at its exact size, as an arena gives nothing back: the lines counted before they are written, a single-byte codec's UTF-8 measured before it is decoded. Texts were 34% of a corpus's tree memory, lines and strings' pieces 11-14%, nodes the rest | a copy into the arena after the read (similarity's reads would pay it, or keep two text types); the pieces and literals in the arena too (copying them out of the scratch measured slower on the heap, output.h; not tried in an arena). On Windows the read still goes through iostreams and is copied into the arena |
| D38 *(taken)* | one task pool for the walk and the files; a worker reads and parses; workspaces from a stack under a mutex; name tables merged at the end (D30), on as many cores as there were workers: each hashes its names, each core interns the names of one hash shard, the shards appended are the table; a run of one worker keeps its table and renumbers nothing | separate stages (parallel glob, then batch read, then parse: a hand-off per file); `thread_local` workspaces (the pool's threads are not known); a shared interner with a lock (#276: slower with threads) |
| D39 *(taken)* | the Python face: `code.parse(root, pattern="**/*.py", *, threads=None)` → `Corpus`; `code.parse_source(str or bytes, path)` → `Tree`; `Node.field(name)` gives what ast holds, operators and contexts by name (`"Add"`, `"Load"`) | Arrow columns of the corpus (D23, not yet); an iterator over files as they finish |
| D40 (Q38, Debith 2026-10-27: "as dump") | `dump()` in ast.dump's 3.14 format is the completeness check, through CPython's own `repr` for constants | `to_ast()` building `ast` nodes: the C API does not reach the `ast` classes, and #248 rules out importing the module at run time |
| D41 *(taken, Q36; widened 2026-10-25, 2026-10-30)* | encodings as Python's codec registry finds them, by name or alias (`gen_codec_tables.py` generates every codec it can import and its form): UTF-8 and ASCII checked; Latin-1 and every ASCII-compatible single-byte codec — 62 (cp1252, koi8-r, iso8859-*, mac-*) — through tables from python3.14's codecs; multi-byte encodings the file's error; a codec that is no text encoding and a name the registry does not know refused in CPython's words. Widened when similarity's field test lost the stdlib's `turtledemo/clock.py` (cp1252) to it | iconv (not on Windows); multi-byte codecs as tables (large, and no source in CPython or site-packages uses one) |
| D42 *(taken, Q37)* | identifiers checked against XID_Start/XID_Continue tables generated from python3.14 (`gen_unicode_tables.py`); kept as written, without NFKC; `\N{NAME}` decoded through CPython in the adapter, not checked at parse time | NFKC and character-name tables generated into C++ (2 files of 1,846 have identifiers NFKC changes; 1 mutant of 20,000 used an unknown name) |
| D43 *(taken)* | `__future__` features checked as 3.14's `ast.parse` checks them: the leading block only, `braces` refused | the compiler's later checks (a late `__future__` import), which `ast.parse` does not make |
| D44 *(taken)* | at most 1,000 nested expressions or statements; brackets 200 deep, indentation 100, f-strings 150, format specs 2 (CPython's tokenizer limits). What a source only chains — `a.b.b...`, `x + x + ...`, `elif` after `elif` — is read in loops and dumped with a stack of the dumper's own, so a tree 200,000 deep parses and dumps (CPython parses those and its own `ast.dump` runs out of recursion) | CPython's parser stack limit (6,000 rule calls), which counts differently |
| D45 *(Q39, Debith 2026-10-27: "based on cpus for default")* | the width: `threads=None` is one thread per core from the start (`width_for<one_per_core>(…)`): parsing computes. pathlike's reads keep growing their width while more threads help | the elastic width, which a walk's tasks fool into thinking they wait: 145 ms at 20-24 workers on 16 cores, against 75 ms fixed at the cores |
| D47 (Debith: "Do fixes", 2026-10-26) | the whole-buffer source checks — is the text UTF-8, where do its lines start — scan 64 bytes a step (`utils/byte_scan.h`: SSE2 on x86-64, NEON on arm64, and two 32-byte compares where AVX2 is there, chosen at run time since 2026-10-27); the line starts come from one mask per 64 bytes, every line end taken from it (`for_each_line_end`: Lib's line index 12.6-13.5 → 3.9-4.4 ms). The scalar loop is the definition, run by a constant evaluation, the tail and `PYGIM_BYTE_SCAN_SCALAR`; `test_the_buffer_scans_agree_with_cpython_at_every_offset_around_their_steps` checks every width. The lexers stay scalar | simdjson's `validate_utf8`, which pygim vendors in pathlike's adapter: no offset for the error, no line ends, and its 1.5 MB source in another extension; SIMD in the lexers (17% for the lexer alone, 0% in a whole parse: its runs are a few bytes) |

## How it was checked

CPython is the oracle: a tree must dump as `ast.dump(ast.parse(bytes), include_attributes=True)`
does in python3.14, and a source must be refused exactly when CPython refuses it.

| Check | Result |
| --- | --- |
| CPython 3.14.3's `Lib`, 1,846 files, positions included | 1,838 the same (koi8-r read since D41 widened). 4 differ: 2 by NFKC (D42), and 2 only in `repr` itself (the oracle's `dump` runs under 3.12, whose Unicode tables are older). 4 both refuse |
| site-packages of the dnd environment and pygim's own sources, 2,297 files | 2,294 the same; the 3 left differ by `repr`'s Unicode version only |
| code strings CPython's tests compile, exec, eval or expect to fail (8,750, doctests included) | 8,748 judged as CPython judges them; the 2 left are artifacts of the extraction (a `str` source holding a BOM or a coding cookie, which compile() ignores in a str) |
| token-level mutants (a token deleted, inserted, replaced, or two swapped), each of the fixes below re-run against them | 32,000 through the C++ harness, all judged as CPython judges them; then 20,000 through `pygim.code`, 1 judged differently: an unknown `\N{NAME}` (D42) |
| re-run 2026-10-26, after the source checks (D47) and the lexer's and parser's state changed | `Lib`: the same 1,838 and 4; 5,000 of 5,000 mutants. site-packages (seed 7): the same 2,294 and 3; 5,000 of 5,000 |
| re-run 2026-10-31, after the split into components (D60) | python3.14's library as Debian ships it (624 files): 624 the same, 3,000 of 3,000 mutants, before and after alike; `tests/static/run_sanitizer_hunt.py` (ASan+UBSan) over it and its mutants for nine minutes: 714,517 inputs, no finding |

Commands: `PYTHONPATH=src python benchmarks/code_oracle.py ROOT --mutants N` (files, mutants,
time). The test suite holds 84 constructs compared with the running Python's `ast`, 5 dumps of
3.14-only syntax pinned from python3.14, 32 refusals, 12 decoding refusals pinned from
python3.14, the views, the corpus and Ctrl-C (`tests/unittests/test_code_parse.py`).

What the checks found and the parser now does as CPython does: a block's end takes in the `;`
after its last statement; a backslash in the indentation joins the next line, and the column it
stood at is the line's indentation; `*a` alone is a valid statement and `f"{*a}"` a valid field;
`except A, :` is a one-element tuple; `case (*x):` and `case {**_}:` are refused; identifiers
outside XID_Start/XID_Continue are refused; the leading `__future__` features are checked; a
comment inside `f"{x = }"` is left out of its text; a coding cookie is looked up as Python's codec
registry looks it up (`utf-`, `latin` and `us-ascii` are names it knows).

## Speed

Measured 2026-10-25 on 16 logical cores (WSL2), warm cache.

| Run | Time |
| --- | --- |
| parse alone, one thread, CPython 3.14.3's `Lib` (36 MB, 3.3 million nodes) | 355 ms, about 100 MB/s |
| of which the source checks: the encoding, UTF-8, the line starts | 33 ms; 10 ms at 64 bytes a step (D47, 2026-10-26) |
| `code.parse`: walk + read + parse + merge, 16 threads fixed | 75 ms |
| the same, `threads=None`: one per core since Q39 (2026-10-27) | 76-77 ms; 140-150 ms while it was elastic |
| the same, the merge's renumbering on every core and a node's kind read from a table (2026-10-27) | 60-62 ms: the merge 27-29 → 13-14 ms of it |
| the same, the names merged on every core (2026-10-30) | 62-63 ms: the merge 12.8 → 8.4 ms of it; one thread 403 → 395 ms (no merge) |
| a fresh process's first `code.parse`, the nodes in each workspace's chunks (D61, 2026-10-31), python3.14's library as Debian ships it (624 files) / the dnd environment's python3.12 library | first run 25.6-30.1 ms against 31.4-33.4 on the heap, second 22.5-25.4 against 29.2-31.8, third 14.0-15.7 against 14.5-16.1 / first 104.8-112.7 against 132.2-148.0, third 59.0-67.4 against 59.1-64.3 (six fresh processes each, alternated); the C library's heaps took 20.4 MB from the system instead of 33.9 / 89.3 instead of 156.3 |
| the same, each file read into its workspace's chunks and its text and lines kept there (D69, 2026-10-31) | first run 17.9-23.8 ms against 25.3-30.1 with only the nodes there, second 14.9-17.2 against 22.9-26.1, third 14.1-16.5 against 13.9-16.3 / first 73.9-103.4 against 101.0-121.3, second 62.3-68.5 against 89.3-130.9, third unchanged (six fresh processes each, alternated); the heaps took 9.2 MB from the system instead of 20.5 / 43.2 instead of 90.2 |
| python3.14's `ast.parse` of the same files, one thread | 4,200 ms |
| the pegen-generated spike (#276), one thread, CPython 3.12's `Lib` (14.5 MB) | 653 ms, about 22 MB/s |

The parser, lexer, tokens and rows are about 3,050 lines of code (Ruff's parser and lexer are
about 9,200, #280); source, literals, dump, the tree and the corpus add about 1,480.

## Known differences from CPython

- Error messages are pygim's own, but for a coding cookie's and a decoding's, which are python3.14's
  (`NOT_DECODED_314` in the tests). The refusal and its line match CPython's.
- `stats()["names"]` counts the names of every file read, a file that is not Python and a parse
  rewound at an ambiguity among them: a workspace's names are kept across its files.
- Identifiers are not NFKC-normalised (D42), and an unknown `\N{NAME}` is not refused at parse
  time.
- Multi-byte source encodings (shift_jis, gbk, euc-jp) are refused (D41).
- `barry_as_FLUFL` (`<>`) is not supported, and type comments are not parsed.
- A relative import more than 65,535 packages up (`from` and 65,536 dots) is refused: a row holds
  the level in 16 bits. CPython reads 70,000; pygim wrapped it to 4,464 until 2026-10-29.
- Nesting past 1,000 levels is refused (D44); CPython 3.14 reads 5,000 nested unary, `not` and
  conditional expressions and 2,000 lambdas.
- A decimal integer of more than 4,300 digits is refused, as CPython's default
  `int_max_str_digits` refuses it; a Python run with `sys.set_int_max_str_digits` changed differs.

The parser recurses about once a level per nesting, on whichever thread parses: 200 nested
brackets, 950 nested unary operators or lambdas and 1,000 attribute levels each parse on a thread
of 384 KB of stack, and 200 brackets overflow one of 256 KB (-O3, 2026-10-29). Linux and Windows
threads have 8 MB and 1 MB, macOS's 512 KB: enough. musl's 128 KB is not — whether the pools
should start their threads with a stack of their own is D51.

## Open questions

- **D23**: lay the corpus out as Arrow columns (kind, parent, size, field, value, start, end per
  node, a file offsets column, the names as `dictionary<uint32, large_string>`)?
- The next steps: version gates; NFKC and character-name tables; error recovery. A profile of the
  parser under similarity's frontend (2026-10-26, gprof, CPython's `Lib`) shows no part above 6%
  of a parse: the lexer's scan, the parser's `peek` and the packing share it.
