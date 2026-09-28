# Unpacking pass — cluster G

[The report](../2026-09-28-unpacking-pass.generated.html) · user-facing docs (enact guide, definition of done, releasing, examples, defects report) · 41 unpacked, 6 left as written, over two rounds.

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

### G1. `docs/enact.md` — ENACT: setting up and using it

| | |
|---|---|
| Text before | It gives an agent knowledge found by the *kind of problem* it is working on rather than by similarity to the prompt, remembers what happened, and learns from whether what it offered helped. |
| Text after | It gives an agent knowledge, remembers what happened, and learns from whether what it offered helped. The knowledge is not found by similarity to the prompt: it is filed by the *kind of problem* it belongs to, and found by asking what kind of problem the agent is working on. |
| Steps made explicit | ENACT does three things: gives knowledge, remembers, learns; retrieval is not similarity to the prompt&#x27;s text; knowledge is filed by the kind of problem it belongs to; retrieval asks what kind of problem the agent is working on and returns what is filed there |

### G2. `docs/enact.md` — Set up a project

| | |
|---|---|
| Text before | `--local` keeps `.enact` inside the project, on the branch that carries it — simplest for a project with one checkout, but each branch then has its own. |
| Text after | `--local` keeps `.enact` inside the project, on the branch that carries it — simplest for a project with one checkout. But the store is then committed with the code, so checking out another branch means working against that branch&#x27;s copy of the store: each branch has its own. |
| Steps made explicit | with --local the store is committed with the code; so the store exists per branch; checking out another branch puts you on that branch&#x27;s copy; hence each branch has its own store |

### G3. `docs/enact.md` — One store for what is not about a project

| | |
|---|---|
| Text before | A store shared with other people is the other case: its `policy.yaml` says `push: manual`, so writes stay in your own clone and reach the others as a pull request its owners merge. |
| Text after | A store shared with other people is the other case. Its `policy.yaml` says `push: manual`: a write is still committed, but only in your own clone — nothing is pushed for you. To reach the others, the writes go out as a pull request, and they arrive when the store&#x27;s owners merge it. |
| Steps made explicit | push: manual disables automatic pushing; a write is still committed, but only locally; sharing happens by opening a pull request; the other people receive the writes when the store&#x27;s owners merge it |

### G4. `docs/enact.md` — Several stores, one repository

| | |
|---|---|
| Text before | Two rules of thumb: a store&#x27;s remote need not be its project&#x27;s, and for a project whose repository is public it must not be; and a store can live anywhere, as long as its inventory paths are relative to the project root (`oo enact status` opens it and reports anything it cannot resolve). |
| Text after | Two rules of thumb. First, a store&#x27;s remote need not be its project&#x27;s — and for a project whose repository is public it must not be, or pushing the store would publish the memory with the code. Second, a store can live anywhere, as long as its inventory paths are relative to the project root, so they resolve no matter where the store sits (`oo enact status` opens it and reports anything it cannot resolve). |
| Steps made explicit | the store may push to a different repository than the code; if the code repository is public, pushing the store there would publish the memory — that is why &#x27;must not&#x27;; inventory paths are relative to the project root, not to the store; therefore the store directory&#x27;s location does not affect whether they resolve |

### G5. `docs/enact.md` — Start a new project's memory

| | |
|---|---|
| Text before | **`prepare-vocabulary`** drafts the project&#x27;s vocabulary from its own documents: the questions knowledge is filed by, named in the project&#x27;s own words, each value citing where the word comes from. |
| Text after | **`prepare-vocabulary`** drafts the project&#x27;s vocabulary from its own documents. The vocabulary is the set of questions knowledge is filed by; the draft names those questions in the project&#x27;s own words, and each value cites the place in the documents its word comes from. |
| Steps made explicit | the vocabulary is defined: the set of questions knowledge is filed by; the draft names those questions using the project&#x27;s own words; every value carries a citation to where in the documents its word appears |

### G6. `docs/enact.md` — Start a new project's memory

| | |
|---|---|
| Text before | Replacing a live pack (`--replace`) is refused while memories carry a value it removes; the message names them, so they can be retagged first. |
| Text after | A replacement pack (`--replace`) can drop values the live pack defines. While any memory still carries a dropped value, the replacement is refused, and the message names those memories — retag them first, then replace. |
| Steps made explicit | a replacement pack may drop values the live pack defined; some memories may still be tagged with a dropped value; while any is, accepting the replacement is refused; the refusal names those memories; retag them, then the replacement goes through |

### G7. `docs/enact.md` — During work, and after

| | |
|---|---|
| Text before | A read can narrow by a word (`term`) when tags cannot name the subject, and says which documents its candidates cite and which inventoried ones none does, so a question the memory cannot answer is sent to the right document. |
| Text after | A read can narrow by a word (`term`) when tags cannot name the subject. It also reports where its candidates stand on the project&#x27;s documents: which documents the candidate memories cite, and which inventoried documents none of them cites. So when the memory cannot answer a question, the read has already named the document to send it to. |
| Steps made explicit | term narrows when tags cannot name the subject; the read reports coverage: documents its candidate memories cite; and inventoried documents no candidate cites; an uncited inventoried document is where an unanswered question should be taken |

### G8. `docs/enact.md` — During work, and after

| | |
|---|---|
| Text before | The server&#x27;s startup instructions carry only an index of titles, newest first — Claude Code keeps the first 2,048 characters of a server&#x27;s instructions and drops the rest without saying so, which is how a preference that would have changed a recommendation once never arrived. |
| Text after | The server&#x27;s startup instructions carry only an index of titles, newest first, because Claude Code keeps the first 2,048 characters of a server&#x27;s instructions and drops the rest without saying so. That silent cut has already cost something once: a preference sat past it, never reached the agent, and a recommendation went out that the preference would have changed. |
| Steps made explicit | Claude Code truncates server instructions at 2,048 characters, silently; that limit is why the instructions carry only a title index, newest first; once, a preference sat past the cut and was dropped; so it never reached the agent; and the recommendation it would have changed went out unchanged |

### G9. `docs/enact.md` — During work, and after

| | |
|---|---|
| Text before | Only then do that pattern&#x27;s cases fold under it in reads. |
| Text after | Only then do that pattern&#x27;s cases fold under it in reads: instead of each case standing in the context on its own, a read places the accepted generalisation and lists its cases under it as evidence. |
| Steps made explicit | before acceptance, each case stands in a read&#x27;s context on its own; after acceptance, the read places the generalisation instead; the cases are listed under it as evidence — that is what &#x27;fold&#x27; means |

### G10. `docs/enact.md` — When something is off

| | |
|---|---|
| Text before | The agent still holds the tool descriptions it was given when it connected, so if a *new parameter* seems ignored, reconnect (`/mcp`) as well. |
| Text after | A reload replaces the server&#x27;s code, but not what the agent knows about it: the agent still holds the tool descriptions it was given when it connected, and a parameter added since then is missing from those descriptions. So if a *new parameter* seems ignored, reconnect (`/mcp`) as well. |
| Steps made explicit | reload replaces the server&#x27;s code only; the agent&#x27;s tool descriptions were cached at connect time and are not refreshed by a reload; a parameter added since connecting is absent from those descriptions; that absence is why the parameter seems ignored; reconnecting refetches the descriptions |

### G11. `docs/enact.md` — When something is off

| | |
|---|---|
| Text before | The file is a view: to make the edit the memory, have the agent write it superseding that memory. |
| Text after | The file is a view: it is generated from the memory and shows it, so editing the file changes what is shown, not the memory itself. To make the edit the memory, have the agent write the edited text superseding that memory. |
| Steps made explicit | a view is generated from the memory and shows it; editing the view therefore does not change the memory; to make the edit the memory, the agent writes the edited text as a superseding memory |

### G12. `docs/enact.md` — Driving it from a shell

| | |
|---|---|
| Text before | Each call is a process, and a process is a session — right for the MCP server, which is one process per conversation, wrong for a script, where it would put every memory in a session of its own and leave `review` nothing to gather. |
| Text after | Each call is a process, and a process is a session. For the MCP server that is right: the server is one process per conversation, so the conversation is one session. For a script it is wrong: each call would be a new process, and so a new session, putting every memory in a session of its own and leaving `review` nothing to gather. |
| Steps made explicit | each oo enact call runs as its own process; the system equates a process with a session; the MCP server is one process for a whole conversation, so conversation = session, which is right; a script makes a new process per call, so a new session per call; every memory then lands in a session of its own; review, which works over a session, is left nothing to gather |

### G13. `docs/definition_of_done.md` — What proves it

| | |
|---|---|
| Text before | A law stated in a comment is a `static_assert` in `tests/static/*_proofs.cpp`, wired into an extension&#x27;s sources so a build that breaks it does not link. |
| Text after | A law stated in a comment is a `static_assert` in `tests/static/*_proofs.cpp`. The proof files are wired into an extension&#x27;s sources, so they compile whenever the extension does — a change that breaks a law fails that compile, and the extension does not link. |
| Steps made explicit | the proof files are listed among an extension&#x27;s sources; so they are compiled whenever the extension is built; a change that breaks a law fails that compilation; therefore the extension does not link |

### G14. `docs/definition_of_done.md` — What proves it

| | |
|---|---|
| Text before | Nothing a test reads may come from the machine it runs on — not the user&#x27;s configuration, a shared directory, a running service, the clock or the working directory — or a green run says only that this machine happens to be in the right state. |
| Text after | Nothing a test reads may come from the machine it runs on — not the user&#x27;s configuration, a shared directory, a running service, the clock or the working directory. If a test does read the machine, its passing depends on the machine&#x27;s state, and a green run says only that this machine happens to be in the right state — not that the code is right. |
| Steps made explicit | if a test reads the machine, whether it passes depends on the machine&#x27;s state; a green run then attests to the machine&#x27;s state; which means it does not attest that the code is right |

### G15. `docs/definition_of_done.md` — What proves it

| | |
|---|---|
| Text before | A memory or timing measurement runs on a fresh heap (a subprocess), never against a heap that remembers what an earlier test freed. |
| Text after | A memory or timing measurement runs on a fresh heap, which means in a subprocess: the test process&#x27;s own heap still holds what earlier tests allocated and freed, and a measurement taken on it measures that history too. |
| Steps made explicit | the test process&#x27;s heap retains what earlier tests allocated and freed; a measurement taken on that heap includes that history; a subprocess starts with a fresh heap, which is why the measurement runs there |

### G16. `docs/definition_of_done.md` — How it is wired

| | |
|---|---|
| Text before | What differs is the `Environment` passed in, which is configuration, which is exactly what is meant to differ. |
| Text after | What differs is the `Environment` passed in. The `Environment` is configuration, and configuration is exactly what is meant to differ between a test and production. |
| Steps made explicit | test and production differ only in the Environment argument; the Environment is the configuration; configuration is the one thing meant to differ between test and production |

### G17. `docs/definition_of_done.md` — How it is wired

| | |
|---|---|
| Text before | Environment variables are legitimate at a process boundary — a reloading server cannot pass an argument to its own successor — and the boundary is the wiring, never one layer in. |
| Text after | Environment variables are legitimate at a process boundary: a reloading server cannot pass an argument to its own successor, so what the next process must know travels in the environment. And the boundary is the wiring, never one layer in. |
| Steps made explicit | a reloading server cannot hand an argument to the process that replaces it; so the environment is the channel that carries what the successor must know; that is why environment variables are legitimate exactly at the process boundary; and the boundary is read in the wiring, never a layer below it |

### G18. `docs/definition_of_done.md` — What it leaves behind

| | |
|---|---|
| Text before | Prefer to check this from outside the process — `oo enact call` makes every operation reachable from a shell, and the defects that survive in-process tests live in what a process does on its way in and out (design 03 §3.5.2, §3.5.3). |
| Text after | Prefer to check this from outside the process. `oo enact call` makes every operation reachable from a shell, and that reach matters because of where defects hide: a test that runs in-process never exercises what a process does on its way in and on its way out, and that is exactly where the defects that survive such tests live (design 03 §3.5.2, §3.5.3). |
| Steps made explicit | an in-process test never runs a process&#x27;s way in or way out; the defects that survive such tests live exactly there; oo enact call reaches every operation through a real process, which is why checking from outside is preferred |

### G19. `docs/releasing.md` — What decides the version

| | |
|---|---|
| Text before | A patch or minor bump ignores pre-release tags. |
| Text after | A patch or minor bump ignores pre-release tags: the version it bumps is the latest final release&#x27;s. |
| Steps made explicit | a bump computes the next version from an existing tag; pre-release tags are skipped in that computation; so the tag that gets bumped is the latest final release&#x27;s |

### G20. `docs/releasing.md` — What a run does

| | |
|---|---|
| Text before | The extensions link Arrow&#x27;s versioned shared libraries, so no other pyarrow can load them. Arrow&#x27;s libraries are excluded when the wheel is repaired and are loaded from the pinned pyarrow instead. |
| Text after | The pin follows from linking: the extensions link Arrow&#x27;s shared libraries by their versioned names, a different pyarrow release ships them under different versioned names, and with those the extensions cannot load. Repairing the wheel would normally copy Arrow&#x27;s libraries into it; they are excluded instead, so at runtime the extensions load them from the pinned pyarrow. |
| Steps made explicit | the extensions link Arrow&#x27;s shared libraries by versioned names; a different pyarrow release ships differently versioned libraries; with those the extensions cannot load — which is why the pin exists; wheel repair would normally copy Arrow&#x27;s libraries into the wheel; they are excluded from the repair; so at runtime they come from the pinned pyarrow |

### G21. `docs/releasing.md` — What a run does

| | |
|---|---|
| Text before | **macOS builds its own unixODBC** (`.github/scripts/build_unixodbc_macos.sh`) for the wheels&#x27; deployment target, 13.3. Homebrew&#x27;s bottle targets a newer macOS, which delocate rejects. |
| Text after | **macOS builds its own unixODBC** (`.github/scripts/build_unixodbc_macos.sh`) for the wheels&#x27; deployment target, 13.3. Every library bundled into a wheel must support that target, and delocate rejects one built for a newer macOS. Homebrew&#x27;s bottle is built for a newer macOS, so the workflow compiles unixODBC against 13.3 itself. |
| Steps made explicit | the wheels declare deployment target macOS 13.3; every library bundled into a wheel must support the target; delocate enforces this by rejecting newer ones; Homebrew&#x27;s prebuilt unixODBC is built for a newer macOS, so it is rejected; therefore the workflow compiles unixODBC against 13.3 itself |

### G22. `docs/releasing.md` — One-time setup

| | |
|---|---|
| Text before | Every field must match what the workflow sends; a failed upload prints those claims. |
| Text after | The workflow sends these values as claims with the upload, and PyPI accepts the upload only when every field matches; a failed upload prints those claims, so the mismatched field can be read off. |
| Steps made explicit | the workflow sends the publisher fields as claims with the upload; PyPI compares each claim against the configured publisher and accepts only on a full match; a failed upload prints the claims it sent; comparing them to the publisher shows which field mismatched |

### G23. `docs/examples/README.md` — Conventions

| | |
|---|---|
| Text before | Each file starts with `# type: ignore`: the compiled extension modules ship no type stubs yet, and the examples favour runtime-verified behaviour over static typing. |
| Text after | Each file starts with `# type: ignore`. The compiled extension modules ship no type stubs yet, so a type checker would flag every call into them; the marker silences that, and the examples favour runtime-verified behaviour over static typing. |
| Steps made explicit | the compiled extensions ship no type stubs; so a type checker would flag every call into them; # type: ignore silences those flags; the examples rely on runtime verification instead of static typing |

### G24. `docs/reports/2026-09-16-memory-defects-root-cause.md` — Root cause analysis — the memory defects of 2026-09-15/16 (Term table)

| | |
|---|---|
| Text before | A memory the hard tags admit for a read, before ranking, the budget, and any term. |
| Text after | A memory that a read&#x27;s hard tags admit — counted before ranking orders the candidates, before the budget cuts them, and before any term narrows them. |
| Steps made explicit | the read&#x27;s hard tags filter the store; what passes is a candidate; ranking then orders the candidates; the budget then cuts them; a term then narrows them; &#x27;candidate&#x27; names the stage before all three |

### G25. `docs/reports/2026-09-16-memory-defects-root-cause.md` — Root cause analysis — the memory defects of 2026-09-15/16 (Term table)

| | |
|---|---|
| Text before | Listing an accepted generalisation&#x27;s instances under it as evidence rather than placing each in the context. |
| Text after | What a read does with an accepted generalisation: rather than placing each of its instances in the context separately, the read lists the instances under the generalisation as evidence. |
| Steps made explicit | the fold happens in reads, once a generalisation is accepted; without it, each instance would be placed in the context separately; with it, the read places the generalisation and lists the instances under it as evidence |

### G26. `docs/reports/2026-09-16-memory-defects-root-cause.md` — 1. The defects, and what each was really an instance of

| | |
|---|---|
| Text before | and each is a fresh instance of the same root cause as the defect it came from. That is the strongest evidence in this report that these are causes and not coincidences. |
| Text after | and each is a fresh instance of the same root cause as the defect it came from. That is the strongest evidence in this report that these are causes and not coincidences: a coincidence has no reason to reappear inside its own fix, while a cause that is still operating does, because it shapes the fix as it shaped the defect. |
| Steps made explicit | a coincidence has no mechanism to recur inside the commit that fixes it; a still-operating cause shapes the fix the same way it shaped the defect; so recurrence inside the fixes is evidence for causes over coincidence |

### G27. `docs/reports/2026-09-16-memory-defects-root-cause.md` — A. Derived state written at each call site

| | |
|---|---|
| Text before | it is a silent overwrite of their edit — a loss the design had already argued about (03 §3.4) and which the fix&#x27;s author had to re-derive from the document rather than from the code. |
| Text after | it is a silent overwrite of their edit — a loss the design had already argued about (03 §3.4). The code carried no trace of that argument, so the fix&#x27;s author had to re-derive it from the document rather than read it off the code. |
| Steps made explicit | the design document had already argued about this loss; the code carried no trace of that argument; so the fix&#x27;s author could not learn it from the code and had to re-derive it from the document |

### G28. `docs/reports/2026-09-16-memory-defects-root-cause.md` — B. Absence was never a result

| | |
|---|---|
| Text before | With stores of three to five memories in tests, an unanswerable read is indistinguishable from a small one. |
| Text after | With stores of three to five memories in tests, every response is small, whether the store could answer or not — so an unanswerable read was indistinguishable from an ordinary read of a small store. |
| Steps made explicit | test stores hold three to five memories; so every read&#x27;s response is small regardless of whether the store could answer; an unanswerable read&#x27;s near-empty response therefore looks like any ordinary small response; which is why the missing &#x27;not here&#x27; was never noticed |

### G29. `docs/reports/2026-09-16-memory-defects-root-cause.md` — C. The caller was never closed with

| | |
|---|---|
| Text before | For an agent, though, a result is not a receipt; it is the next input. |
| Text after | For an agent, though, a result is not a receipt; it is the next input — whatever the result leaves unsaid, the agent&#x27;s next call has to go and fetch. |
| Steps made explicit | the agent feeds each result into its next decision; so anything the result omits, the agent must obtain; obtaining it costs another tool call — the cost the next sentence counts |

### G30. `docs/reports/2026-09-16-memory-defects-root-cause.md` — D. Checks and data that stop at the file boundary

| | |
|---|---|
| Text before | Each component was built with a clear, narrow input, which is good design and exactly why the cross-file obligations fell between components. Nobody owned &quot;the store is consistent with the project&#x27;s documents&quot;. |
| Text after | Each component was built with a clear, narrow input. That is good design, and it is exactly why the cross-file obligations fell between components: a check that needs a second file belongs to no component whose input is one file, so none of them performed it. Nobody owned &quot;the store is consistent with the project&#x27;s documents&quot;. |
| Steps made explicit | each component takes one narrow input, which is good design; a check that needs a second file fits inside no such component; so no component performed those checks — they fell between; and no owner existed for the cross-file property itself |

### G31. `docs/reports/2026-09-16-memory-defects-root-cause.md` — F. A long-lived runtime over changing code

| | |
|---|---|
| Text before | Passing the new argument happened to work because the host forwarded it, which is luck. |
| Text after | Passing the new argument happened to work, but only because the host forwarded an argument its cached schema did not list — behaviour nothing guarantees, which is why it is luck. |
| Steps made explicit | the host&#x27;s cached schema predated the new argument, so the schema did not list it; the host nevertheless forwarded the unknown argument; nothing guarantees a host does that; which is why the working call was luck, not correctness |

### G32. `docs/reports/2026-09-16-memory-defects-root-cause.md` — F. A long-lived runtime over changing code

| | |
|---|---|
| Text before | `oo memory reload` asks each server to re-exec itself between messages — exec keeps the host&#x27;s pipes, so the session survives, and the session number travels with it. |
| Text after | `oo memory reload` asks each server to re-exec itself between messages — an exec replaces the code the process runs while keeping its open pipes to the host, so the session survives, and the session number travels with it. |
| Steps made explicit | an exec replaces the code the process runs; but keeps the process&#x27;s open pipes to the host; the host keeps talking over the same pipes, so the session survives; and the session number travels into the new code |

## Every unpack of round 2, in full

### G33. `docs/enact.md` — During work, and after

| | |
|---|---|
| Text before | It also reports where its candidates stand on the project&#x27;s documents: which documents the candidate memories cite, and which inventoried documents none of them cites. |
| Text after | It also reports how its candidates rest on the project&#x27;s documents. A memory carries citations, and each citation names a document; the store&#x27;s inventory lists the project&#x27;s documents. The read compares the two and reports both sides: which documents the candidate memories cite, and which inventoried documents none of them cites. |
| Steps made explicit | a memory carries citations; each citation names a document; the inventory lists the project&#x27;s documents; the read compares cited documents against the inventoried ones; both sides of the comparison are reported |

### G34. `docs/enact.md` — Start a new project's memory

| | |
|---|---|
| Text before | `accept` also prints a warning for any citation that does not hold in its document. |
| Text after | `accept` also checks each citation against the document it names, and prints a warning for any whose cited text the document does not carry where the citation points. |
| Steps made explicit | a citation names a document and a place in it; accept opens that document and checks the cited text there; a citation whose text the document does not carry at that place gets a warning |

### G35. `docs/definition_of_done.md` — What proves it

| | |
|---|---|
| Text before | An optimisation is justified by a same-process A/B (old and new compiled side by side), never by comparing runs on a machine whose load drifts. |
| Text after | An optimisation is justified by an A/B in one process: the old and the new code are compiled side by side and measured in the same run, so whatever the machine is doing weighs on both alike. It is never justified by comparing separate runs, because the machine&#x27;s load drifts between them, and the difference measured may be the drift, not the code. |
| Steps made explicit | old and new are compiled side by side into one process; both are measured in the same run; so machine conditions affect both equally; separate runs differ by whatever the load did between them; so their difference may measure the drift, not the code |

### G36. `docs/definition_of_done.md` — What it leaves behind

| | |
|---|---|
| Text before | For anything that refuses, a test says so explicitly: the audit log, the head views, the content objects and the vocabulary are byte-identical afterwards. |
| Text after | For anything that refuses, a test says so explicitly: after the refused call, the audit log, the head views, the content objects and the vocabulary are byte-identical to what they were before it. |
| Steps made explicit | the comparison baseline was unstated; identical afterwards means identical to the state before the refused call; stated the two points being compared |

### G37. `docs/definition_of_done.md` — How it is delivered

| | |
|---|---|
| Text before | process-wide memory (`pygim.utils.rss_bytes`) is a benchmark probe, never a per-object size. |
| Text after | process-wide memory (`pygim.utils.rss_bytes`) is a benchmark probe, never a per-object size: it measures the whole process, so its number includes everything else the process holds and cannot be read as the size of one object. |
| Steps made explicit | rss_bytes measures the whole process; that number includes everything else the process holds; therefore it cannot be attributed to one object |

### G38. `docs/releasing.md` — Cutting a release

| | |
|---|---|
| Text before | Run on `main`, the workflow only cuts: it picks the version (the latest `v*` tag bumped, or the one given), creates `release/&lt;version&gt;` from `main`, and starts the release on that branch. That second run is the release. |
| Text after | Run on `main`, the workflow only cuts: it picks the version (the latest `v*` tag bumped, or the one given) and creates `release/&lt;version&gt;` from `main`. Creating that branch is a push, and a push to a `release/*` branch starts the workflow on it — so the cut sets off a second run, on the new branch, and that second run is the release. |
| Steps made explicit | the cut run creates the release branch; creating the branch is a push; a push to a release/* branch triggers the workflow on that branch (the mechanism the &#x27;cut a branch yourself&#x27; section already relies on); hence the second run, which is the release |

### G39. `docs/releasing.md` — One-time setup

| | |
|---|---|
| Text before | *Deployment branches and tags*: *Selected branches and tags*, with the single rule `release/*`. PyPI uploads can then only come from a release branch. |
| Text after | *Deployment branches and tags*: *Selected branches and tags*, with the single rule `release/*`. The job that uploads to PyPI runs inside this environment, and GitHub admits a run into the environment only from a branch the rule matches — so a PyPI upload can only come from a release branch. |
| Steps made explicit | the publish job runs in the pypi environment (the diagram states this); the branch rule controls which branches may enter the environment; a run from another branch is refused the environment; therefore uploads can only come from release branches |

### G40. `docs/examples/README.md` — pygim examples

| | |
|---|---|
| Text before | so the examples double as living documentation that can be executed to verify the library. |
| Text after | so each example is documentation that is also a check: running the file executes its asserts, and a claim the library no longer honours fails instead of reading as if it were still true. |
| Steps made explicit | &#x27;living documentation&#x27; replaced with its mechanism; running the file executes the asserts behind each claim; a changed behaviour fails the assert; so the text cannot silently go stale (the Conventions section already states this; the intro now carries the mechanism too) |

### G41. `docs/reports/2026-09-16-memory-defects-root-cause.md` — A. Derived state written at each call site

| | |
|---|---|
| Text before | described the view as &quot;rewritten when the head changes&quot; — true, but narrower than the file&#x27;s actual dependency. |
| Text after | described the view as &quot;rewritten when the head changes&quot; — true, but narrower than the file&#x27;s actual dependency: the file shows the memory&#x27;s tags as well as its head, and `link` changes the tags without changing the head, so a view can go stale through changes that sentence never names. |
| Steps made explicit | the view&#x27;s front matter shows tags as well as the head (per section 6.1); link changes the tags without changing the head (defect #1); so &#x27;when the head changes&#x27; misses tag changes; which is exactly how the design sentence was narrower than the dependency |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/enact.md` — During work, and after | A read also names, under `standing`, any preference in its space that it did not place. | &#x27;its space&#x27; could mean the memories the read&#x27;s hard tags admit, or the stores/scope the read consulted; unpacking &#x27;did not place&#x27; would commit to one |
| 1 | `docs/enact.md` — During work, and after | A wrong yes is not a trap: retiring an entry procedure unfolds its instances again. | &#x27;an entry procedure&#x27; reads either as an accepted entry that is a procedure, or as a slip for &#x27;an entry&#x27;/&#x27;a procedure&#x27;; what exactly is retired decides the unpacking |
| 1 | `docs/examples/README.md` — Conventions | the compiled extension modules ship no type stubs yet | looks possibly stale rather than compressed: definition_of_done.md says &#x27;The stub is current&#x27; with src/pygim/*.pyi regenerated by `pygim stubs` — either the extensions&#x27; own stubs are still genuinely missing, or the sentence predates the stub work; left as written |
| 2 | `docs/enact.md` — During work, and after | A read also names, under `standing`, any preference in its space that it did not place. | &#x27;its space&#x27; may mean the stores the read covers or the tag space the read matched, and unpacking &#x27;did not place&#x27; requires choosing one |
| 2 | `docs/enact.md` — One store for what is not about a project | Each write is committed at once, and pushed if you give the store a git remote — that is what carries a preference to your other projects and machines. | the push may be what reaches other machines only (projects on this machine read the store directly), or the route to all other projects too; the sentence supports both |
| 2 | `docs/reports/2026-09-16-memory-defects-root-cause.md` — B. Absence was never a result | `facets` (what the candidates carry, including a soft tag at 0) | a queried soft tag that matched no candidate, or any soft tag no candidate carries — the parenthesis supports both |
