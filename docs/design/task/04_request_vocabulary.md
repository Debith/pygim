# Starting a task — The request vocabulary, a draft

**Section 04: The vocabulary an agent splits a pygim request with, drafted for a vocabulary study**
Status: draft, not accepted · Owner: Debith · Last updated: 2026-09-27

Sections 01 and 02 measured how today's vocabulary fits a month of requests; the two studies in
`studies/` measured what an agent does with only a request and a vocabulary. This section drafts the
vocabulary those results call for, for pygim only. It is a draft for a study, not a pack: the next
step is a locked sample of pygim's requests, two blind tagging passes, and the four measures ENACT's
overview sets (00 §4.8), before the owner accepts anything.

The draft's one source is `request-vocabulary.yaml`; the tables in §3 and the first-round vocabulary
(`request-vocabulary.first-round.md`) are rendered from it by `render_request_vocabulary.py`. Values
that exist today keep their live entries — the renderer reads them from the stores — and add only
their words.

| Term | Meaning |
|---|---|
| first round | The agent's first split of a request, from the request and the vocabulary alone, before anything is retrieved. |
| retag | The one second split, after retrieval by the first round's tags has returned files, documents and knowledge. |
| resolved | Set by retrieval from what the first round chose, and confirmed by the retag; never asked of a request. |
| Value | A tag of the dimension, as it would be written after the `=`. |
| Status | exists — live in a store today, with its live entry; new — proposed here, with a full entry. |
| Brief | The value's one-line meaning; for existing values, the live one. |
| Words | Words a request uses for the value — what the first round matches a request against. |
| Not | The value's boundary: what it is not, and which value that is instead. |
| Evidence | September tasks (section 01) whose request the value would have tagged, each checked by reading its summary. |
| Resolves to | Where in pygim retrieval would look for the value, as a component. Only for subjects. |

---

## 1. What is decided

From the conversation of 2026-09-27, after the two studies:

| Step | Who | Does |
|---|---|---|
| 1. tag | the agent, from the request and the first-round vocabulary only | activity, object, subject(s), quality — never where in the project |
| 2. retrieve | mechanical: the inventory, by tags | files, documents, what the project ships for each subject, the activity's procedure |
| 3. retag, once | the agent, from what came back and the conversation | confirms or corrects object and subject; the activity stays as the request said |
| 4. read and work | the agent and the service | knowledge by the final tags, then the task |

The first round is **broad and shallow**: every value of the dimensions a request can decide, each
as its brief and its words. The retag is **narrow and deep**: full entries, only for the values the
first round chose and their confusable neighbours. Both tag sets are kept; the difference is the
evidence the words need.

---

## 2. Three kinds of dimension

| Dimension | Facet | Round | Why |
|---|---|---|---|
| `domain` | field | first | pygim itself or software in general decides which store answers |
| `task` | activity | first | the request's verb; the studies' runs filled it in every case |
| `artifact` | object | first | what is acted on; the runs named it when the request did |
| `subject` (new) | subject | first | what it is about; the runs wrote it into their summaries unasked, and it is what retrieval resolves: each subject value's `resolves` entry links it to components, and retrieval follows those links to the components where it looks |
| `concern` | quality | first | which property matters, when the request says |
| `component`, `layer`, `language` | place | resolved | the agent cannot know them from a request: without context two runs agreed on the component in 1 of 5 requests |
| `kind`, `tier` | memory | memory | they describe stored knowledge, not a request; `kind` was put on 45 of 255 September tasks, wrongly |

For pygim, the first round drops about 45% of today's vocabulary text; what is left is rendered in
`request-vocabulary.first-round.md`.

---

## 3. The values

<!-- values:start -->

### Field — `domain` (hard): 2 values, 0 new

| Value | Status | Brief | Words | Not | Evidence | Resolves to |
|---|---|---|---|---|---|---|
| `software` | exists | Building software, in any project. | code, program, library, repository, build, test, command | Not what holds in every field — that is domain=any; not one project's own knowledge — that belongs in its store. | — | — |
| `pygim` | exists | pygim itself — a C++26 and pybind11 library with a Python face. | pygim, oo, enact, pathlike, pathset, extension | Not about a project that merely uses pygim, and not about a subject domain such as dnd. | — | — |

### Activity — `task` (hard): 12 values, 6 new

| Value | Status | Brief | Words | Not | Evidence | Resolves to |
|---|---|---|---|---|---|---|
| `design` | exists | Making something new. | design, propose, plan out, sketch, approach, how should, new | Not judging or improving a thing that exists — that is critique or evaluate. | — | — |
| `critique` | exists | Judging a thing that exists. | review, analyze, critique, assess, what do you think, check this | Not measuring against a fixed standard — that is evaluate. | — | — |
| `evaluate` | exists | Measuring against a standard. | measure, benchmark, compare, how much, how fast, verify against | Not an opinion about quality — that is critique. | — | — |
| `troubleshoot` | exists | Finding why a thing fails. | fix, bug, broken, fails, error, why does, not working | Not improving a thing that works — that is design or critique. | — | — |
| `explain` | changed | Making a thing understood now, in the answer. | explain, how does, what is, why, show me, summarize | Not the reasoning an answer gives while doing something else — a critique or a design explains itself, and that is not explain; not a file for a reader — that is document. | A33, E09, F01 | — |
| `implement` | changed | Carrying out a decided change to code so that it works. | implement, add, write, build, change, wire up, refactor | Not deciding what to build — that is design; not writing a document — that is document, and a change that brings its design document along is both. | — | — |
| `document` | new | Writing a thing down for a reader to open later. | document, write up, write it into, markdown, report, readme, docs | Not an answer in the conversation — that is explain; not knowledge stored for agents — that is record; not a change to code — that is implement, and a change that brings its design document along is both. | A37, A43, D27 | — |
| `discover` | new | Finding out what the project already has before acting. | discover, discovery, best way, what do we have, already have, what exists, what is next, go through | Not what is known outside the project — that is research; not measuring what exists against a standard — that is evaluate; not judging it — that is critique. | A10, A19, C04, C16, E15, E16, E22, F26 | — |
| `research` | new | Finding out what is known outside the project before deciding. | research, study, options, alternatives, prior art, how do others, look into, state of the art, paper | Not what the project already has — that is discover; not making the design — that is design, which research comes before; not measuring against a standard — that is evaluate. | A10, C04, C16, C18, E22, F26 | — |
| `operate` | new | Running, stopping or changing the state of something that runs. | kill, stop, start, restart, reload, run, serve, deploy, shut down | Not changing the program's code — that is implement; not finding why it misbehaves — that is troubleshoot. | B22, B30, D03, D29 | — |
| `record` | new | Capturing knowledge so that it outlives the task. | remember, memorize, record, store, lesson, write down, procedure, preference | Not writing a document for people to read — that is document; not explaining something now — that is explain. | B13, B19, B32, C22, F09 | — |
| `plan` | new | Ordering work that is already decided — what next, in which order. | next steps, order, outstanding, what now, roadmap, merge order, priority | Not deciding what to build — that is design; not doing the work — that is implement. | A09, A19 | — |

### Object — `artifact` (hard): 17 values, 5 new

| Value | Status | Brief | Words | Not | Evidence | Resolves to |
|---|---|---|---|---|---|---|
| `extension` | exists | A compiled extension module. | extension, module, compiled, manifest, pybind | Not one layer inside it — that is layer. | — | — |
| `api` | exists | A public interface. | interface, API, method, function, signature, stub, public | Not the implementation behind it. | — | — |
| `service` | exists | A long-lived object that owns state. | service, server object, lifecycle, state, thread | Not a plain value type. | — | — |
| `store` | exists | An on-disk format or layout. | store, on-disk, layout, format on disk, files it writes | Not the objects held in memory. | — | — |
| `protocol` | exists | A wire protocol. | protocol, JSON-RPC, messages, wire | Not the library that answers them. | — | — |
| `design_doc` | exists | A design document. | design, document, section, overview, spec, doc | Not the code the document describes. | — | — |
| `test` | exists | A test or a proof. | test, fixture, suite, assertion, proof | Not the behaviour being tested. | — | — |
| `benchmark` | exists | A benchmark. | benchmark, timing, measurement, profile | Not a correctness test. | — | — |
| `cli_command` | exists | A command a user runs. | command, oo, flag, option, output, banner | Not the library behind the verb. | — | — |
| `vocabulary` | exists | A taxonomy pack or its codebook. | vocabulary, tag, dimension, value, pack, taxonomy | Not a memory tagged with the vocabulary. | — | — |
| `release` | exists | A published version of the library. | release, version, wheel, publish, PyPI | Not one extension's build or its manifest — that is extension. | — | — |
| `report` | exists | A document that presents findings for someone to read and act on. | report, findings, study, analysis, summary | Not a design document, which settles how something is built — that is design_doc; a document can be both. | — | — |
| `source_module` | new | One source file or module, as code — not its public surface. | this file, module, source, .py, .h, .cpp, class, function body | Not the surface callers see — that is api; not a compiled module as a whole — that is extension. | C23, F11, F12, F13, F14, F15, F16, F17 | — |
| `web_page` | new | A page a program serves to a browser, and what the reader does on it. | page, button, browser, highlight, colour, scroll, click, view, served | Not the document the page renders — that is design_doc; not the command that serves it — that is cli_command. | A29, A30, B01, D01, D02, D08, D09, D10, D11, D12, D14 | — |
| `change` | new | A recorded change to the code — a commit, a branch, a pull request. | commit, branch, pull request, PR, merge, rebase, squash, push, stash, history | Not the code the change contains — name that artifact too; not a published version — that is release. | A07, A09, A10, A26, A31, E22, E24 | — |
| `process` | new | A program as it runs — a server, a session, a background job. | server, process, running, port, pid, session, background, job | Not the program's code — that is source_module or extension; not a long-lived object inside a program — that is service. | B06, B22, B30, D03 | — |
| `agent_context` | new | What an agent is given without asking — session-start text, hooks, instructions, prompts. | session start, hook, prompt, instructions, context, delivered, standing, mailbox | Not the knowledge itself — that is a memory, tagged by its own subject; not the service that stores it — that is service or store. | B17, C05, D25, D27, D28, D29, D30 | — |

### Subject — `subject` (soft, new): 18 values, 18 new

| Value | Status | Brief | Words | Not | Evidence | Resolves to |
|---|---|---|---|---|---|---|
| `configuration` | new | Settings a program reads from outside itself. | configuration, settings, environment variables, config, defaults, home directory, working directory | Not how the read values are handed on — that is dependency_injection. | F11, F12, F13, F14, F15, F16, F17 | `component=wiring` |
| `dependency_injection` | new | How the parts of a program are built and handed to each other. | composition root, wiring, dependency injection, container, IoC, factory, hidden dependency | Not the settings themselves — that is configuration. | A08, C18, E17, F17 | `component=wiring` |
| `software_design` | new | The shape of code — objects, responsibilities, patterns. | design pattern, strategy, domain model, domain-driven design, layering, adapter, responsibility, template | Not one mechanism named here — use dependency_injection or data_structures when they fit. | A16, B39, C11, C33, F10 | — |
| `file_system` | new | Paths, directories and files on disk. | path, directory, folder, file system, walk, glob, relative path, uri | Not what a file's content means — that is file_format. | B14, B15, C19, F01, F02 | `component=pathlike` |
| `file_format` | new | How data is written into a file and read back. | yaml, json, toml, html, format, file type, parse, serialise, reader, writer, engine | Not where the file lives — that is file_system. | C31, D31, F20, F21 | `component=pathlike` |
| `command_line` | new | How a program is used from a terminal. | command line, CLI, command, option, flag, help, output, banner, terminal | Not the library behind a command — name its own subject. | B12, C09, C10, E21, E22 | `component=cli` |
| `web_ui` | new | What a served page shows and how a reader works with it. | page, browser, button, tint, scroll, served, localhost, comment on page | Not the text a page renders — that is documentation. | D01, D02, D08, D10 | `component=docs`, `component=cli` |
| `documentation` | new | What documents say, and how they are written and kept. | document, docs, section, scenario, overview, code comments, writing, style | Not how a served page behaves — that is web_ui; not drawing a diagram — that is diagrams. | A17, A42, A43, C34 | `component=docs` |
| `diagrams` | new | Drawing structure or sequence as a picture. | diagram, sequence diagram, class diagram, mermaid, flowchart, arrows | Not the document around them — that is documentation. | A02, A34, A45, D27 | `component=docs` |
| `version_control` | new | Recording, sharing and combining changes to code. | git, commit, branch, merge, rebase, pull request, push, stash, squash, history | Not building or publishing the result — that is continuous_integration or packaging. | A07, A09, A10, A26, A31, E01, E22, E24 | — |
| `continuous_integration` | new | What runs automatically on every change, and publishing its result. | CI, workflow, GitHub Actions, pipeline, release, publish, PyPI, matrix | Not the local build — that is packaging. | A07, B18, E05, E20, E23 | `component=build` |
| `packaging` | new | Building and installing the program on a machine. | build, install, conda, pip, wheel, pyproject, environment, compiler, editable | Not what runs in CI — that is continuous_integration. | A04, E04, E05 | `component=build` |
| `testing` | new | Tests as a subject — how a thing is proven. | test, fixture, isolation, pytest, suite, end to end, regression | Not the property of being provable — that is concern=testing; not measuring speed — that is performance work under task=evaluate. | B32, C16, C18, D28, F04, F05 | — |
| `data_structures` | new | How data is held in memory for fast use. | table, bit set, interning, index, id set, container, memory footprint | Not how data is stored on disk — that is file_format or persistence. | A06, A13, A14, A15, A16, A21 | `component=mapping`, `component=pathlike`, `component=utils` |
| `persistence` | new | Storing data in a database and reading it back. | database, SQL, ODBC, Arrow, bulk load, table, persistence | Not the files a program keeps for itself — that is file_format or store. | A03, A05, E10 | `component=persistence` |
| `knowledge_retrieval` | new | Filing knowledge by tags and finding it again. | tags, vocabulary, taxonomy, classification, retrieval, citation, index, memory | Not how an agent receives it without asking — that is agent_tooling. | A48, B20, D31, E11, E18 | `component=enact`, `component=mcp` |
| `agent_tooling` | new | How an AI agent is set up, fed and coordinated. | hook, session start, agent, subagent, MCP, prompt, instructions, mailbox | Not the knowledge being fed — that is knowledge_retrieval or the knowledge's own subject. | B06, B17, C06, D25, D30, F03 | `component=mcp`, `component=enact`, `component=cli` |
| `running_services` | new | Programs that keep running — servers, daemons, sessions — and their state. | server, process, restart, reload, port, running, background | Not the code of the program — name its own subject. | B06, B22, B30, D03, D13 | `component=mcp`, `component=docs` |

### Quality — `concern` (soft): 13 values, 4 new

| Value | Status | Brief | Words | Not | Evidence | Resolves to |
|---|---|---|---|---|---|---|
| `correctness` | exists | Getting the right answer. | correct, wrong, right answer, bug | Not about speed — that is performance. | — | — |
| `performance` | exists | Time and space. | fast, slow, memory, speed, cost in time | Not about whether the result is right. | — | — |
| `portability` | exists | Working the same everywhere. | Windows, macOS, platform, Python version | Not performance differences between platforms. | — | — |
| `testing` | exists | How a thing is proven. | testable, provable, isolation | Not the behaviour under test. | — | — |
| `documentation` | exists | How a thing is explained. | documented, explained, readable docs | Not the design being documented. | — | — |
| `toolchain` | exists | Compiling and linking. | compiler, linker, flags, toolchain | Not the runtime behaviour of what was built, and not the build subsystem itself — that is component=build. | — | — |
| `lifetime` | exists | Who owns a thing, and for how long. | ownership, lifetime, dangling, reference | Not logical correctness of the algorithm. | — | — |
| `threading` | exists | Threads, locks and the GIL. | thread, lock, GIL, concurrent | Not single-threaded ordering. | — | — |
| `public_api` | exists | The shape of what a caller sees. | public, caller sees, signature, breaking | Not how it is implemented behind the interface. | — | — |
| `usability` | new | How easily a person reads, finds or acts on something. | readable, easy, confusing, glance, find, review, understand | Not how it looks — that is appearance; not whether it is right — that is correctness. | B28, C09, C10 | — |
| `appearance` | new | How something looks. | look, cool, cooler, pretty, colour, style, visual | Not how easily it is used — that is usability. | D02, E22 | — |
| `security` | new | Keeping data and access safe. | secure, encrypt, private, secret, leak, credentials | Not correctness in general. | B34 | — |
| `cost` | new | What a thing spends — tokens, money, a person's time. | tokens, cost, wasted, budget, expensive, money | Not a program's speed — that is performance. | F26 | — |

<!-- values:end -->

---

## 4. Confidence, carried with each tag

Each tag the first round gives carries one of four levels, tied to where it came from — not a number.
Section 01 tagged each September task twice: once at the request, and once in hindsight, from all of
its messages; a tag *changed* when the hindsight tagging gave a different value than the tagging at
the request. Tags where *several values fit* changed in 101 of 151 cases; tags decided *from the
request's words* changed in 114 of 312. Most of those 114 were `task` tags:
the request's words had decided the activity, and the activity itself then moved during the work.

| Level | Meaning | How retrieval uses it |
|---|---|---|
| stated | the request says it | as a hard filter |
| inferred | the context points to it | soft until the retag confirms it |
| candidates | several values fit | all of them, soft; the retag chooses |
| guessed | only knowledge of the project would decide it | soft only |

---

## 5. Open decisions

| Decision | Options | Recommendation |
|---|---|---|
| `subject`: hard or soft | hard — a read must match it · soft — it orders | **soft** until memories carry it: a hard tag filters what a read returns, and no memory written before this dimension carries a `subject` tag, so as hard it would exclude every memory written before it |
| Where words and resolutions live | new entry fields `words:` and `resolves:`, which the loader must learn · in the prose of `when` | **new fields**: retrieval uses them mechanically, and prose cannot be matched reliably |
| Where the new values live | all in pygim's pack · generic ones in the global `software` pack, pygim's own in pygim's | **split**: version control, testing or configuration hold in any software project |
| `concern=testing` beside `subject=testing`, `concern=documentation` beside `subject=documentation` | keep both · rename one side | **keep both, and let the independence measure decide**: the study will show whether they move together — whether a task's value on one side predicts its value on the other. If they move together, one side is redundant, which decides for renaming it; if not, both stay |
| `component=memory`, a name from before the rename to ENACT (found by a blind run, F17-D1) | keep · add `component=enact`, retag, retire `memory` | **rename by retag and retire**: a memory persists the tag's name itself, never an id that a rename could redirect (02 §1.3), so the new name must be written onto each memory — the retag — and the old value retired. Owner, 2026-09-27: *retire it once the new vocabulary finds its sweet spot*; until then the store keeps `memory`, and the study already says `enact` |
| `explain`, `document`, `implement`: what separates an answer from a written file (the owner's review of C32) | split by channel: explain an answer in chat, document a file · split by purpose and lifetime · a dimension of its own for the output | **Decided, owner 2026-09-27: purpose and lifetime.** explain makes a thing understood now, in the answer; `task=document` (new) makes it understood later, in a file a reader opens; implement is a change to code; record stays knowledge for agents. Channel alone would put explain on every request. The study's key was re-keyed by translating its old spelling, implement on a design_doc or report (`studies/vocabulary-evolution/rekey_document.py`) |
| An activity for finding out before acting (the owner's review, 9 of 20 tasks) | one value, `research` · two, `discover` (inside the project) and `research` (outside it) | **Decided 2026-09-27: two**, on the agent's recommendation, which the owner let stand: pygim's procedures already call the in-project survey "discover" (global #45 and #59, step 1), and a search of the project and of the world need different sources |
| What a session is handed (02 §7.1, open) | everything · the base and a pack index | **a third option**: the request-facing dimensions, every value, brief and words; full entries only at the retag |

---

## 6. What comes next

1. **Lock a sample** of pygim's September tasks — about 60 of its 205, stratified by activity and by
   how their first message reads — before anything is tagged.
2. **Two blind passes** over it with `request-vocabulary.first-round.md`, by the isolated headless
   runs of the studies (about $0.06 a run).
3. **The four measures**, as 02 §4.1 computes them. Coverage: how many of the sample's tasks
   receive at least one value of a dimension. Discrimination: the largest share any single value of
   the dimension takes among the tasks that have one. Agreement: whether the two blind passes give
   the same values to the same task, per value as Cohen's κ (κ ≥ 0.70), with the confused pairs
   beneath each κ. Independence: whether two dimensions' values move together across the sample, as
   Cramér's V (V < 0.60).
4. **The owner decides** each value: accept, change, reject.

ENACT's own study machinery (02 §4 — a lock on an audit row, evidence rows, measures computed by the
service) is specified but not built; the studies' scripts stand in for it until it is.

---

## 7. What this leaves out

D&D and other fields, by the owner's decision on 2026-09-27; how the first round is delivered — the
request hook or the session start — which the study's size measure informs; the `resolves` links for
objects, drafted only for subjects; and every existing value's entry, which is used as it stands.
