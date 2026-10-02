# Unpacking pass — cluster D

[The report](../2026-09-28-unpacking-pass.generated.html) · ENACT design, sections 00 and 00a · 36 unpacked, 4 left as written, over two rounds.

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

### D1. `docs/design/enact/00_overview.md` — Section 00: Overview — opening preamble

| | |
|---|---|
| Text before | the C++ implementation reproduces them — held to it by parity tests for as long as both existed. |
| Text after | the C++ implementation reproduces them. While the prototype and the implementation both existed, parity tests held the implementation to the prototype&#x27;s behaviour. |
| Steps made explicit | the prototype&#x27;s demo and evaluation defined the retrieval behaviour; the C++ implementation reproduces it; while both existed, parity tests held the implementation to the prototype; &#x27;held to it&#x27; and &#x27;both&#x27; now have explicit referents |

### D2. `docs/design/enact/00_overview.md` — The system and its parts

| | |
|---|---|
| Text before | That name merged three parts this design keeps apart, and spent a word the end state needs (§4.13): episodic memory is one part of ENACT, not the name of it. |
| Text after | That name merged three parts this design keeps apart. It also spent a word the end state needs (§4.13): the end state needs &quot;memory&quot; for episodic memory, which is one part of ENACT, not the name of the whole. |
| Steps made explicit | the old name merged three parts; separately, it used up the word &#x27;memory&#x27;; the end state needs that word for the episodic part; so episodic memory is a part of ENACT, not the whole&#x27;s name |

### D3. `docs/design/enact/00_overview.md` — The system and its parts

| | |
|---|---|
| Text before | **Retrieval Policy belongs to Adaptive Context**, not beside it: it is the state the selector applies, and modelling a component&#x27;s parameters as its sibling hides that the two change together. |
| Text after | **Retrieval Policy belongs to Adaptive Context**, not beside it: the policy is the state the selector applies. Modelled as the selector&#x27;s sibling it reads as an independent component, which hides that the two change together. |
| Steps made explicit | the policy is the state the Adaptive Context selector applies; drawn as a sibling it reads as an independent component; that reading hides that selector and policy change together |

### D4. `docs/design/enact/00_overview.md` — The system and its parts

| | |
|---|---|
| Text before | episodic evidence stays separate from generalised knowledge and reusable procedures (§4.4), which is what lets a receipt be rerun long after the knowledge above it has changed. |
| Text after | episodic evidence stays separate from generalised knowledge and reusable procedures (§4.4) — kept apart, the evidence is untouched when the knowledge changes, which is what lets a receipt be rerun long after the knowledge above it has changed. |
| Steps made explicit | evidence and generalised knowledge are kept apart; so a change to the knowledge never touches the evidence; so a receipt can still be rerun after the knowledge changed |

### D5. `docs/design/enact/00_overview.md` — 4.1 The seed vocabulary

| | |
|---|---|
| Text before | few enough that a memory&#x27;s whole tag set is one 64-bit word (01, §2.3). |
| Text after | few enough that, at one bit per tag, a memory&#x27;s whole tag set fits in one 64-bit word (01, §2.3). |
| Steps made explicit | each tag is one bit; fifty-three possible tags need at most 53 bits; so the whole set fits a 64-bit word |

### D6. `docs/design/enact/00_overview.md` — 4.1 The seed vocabulary

| | |
|---|---|
| Text before | `quality` divided by *two* characteristics at once — epistemic type and valence — which the one-characteristic-of-division rule forbids (§4.8). |
| Text after | `quality` divided by *two* characteristics at once — epistemic type (principle versus example) and valence (good versus bad) — and the one-characteristic-of-division rule forbids that: one facet asks one question (§4.8). |
| Steps made explicit | quality&#x27;s values answered two questions at once; epistemic type separates principle from example; valence separates good from bad; the facet rule allows one question per facet, so quality had to split |

### D7. `docs/design/enact/00_overview.md` — 4.4 History, consolidation, and counters

| | |
|---|---|
| Text before | A merge of two heads that were written separately is also a *recollection failure* on record (§4.7): the merge row points at the later write&#x27;s evidence, so the miss can be named. |
| Text after | A merge of two heads that were written separately is also a *recollection failure* on record (§4.7): two heads saying one thing means the later write did not recognise what the earlier one already said. The merge row points at the later write&#x27;s evidence — what it searched and what it saw — so the kind of miss can be named. |
| Steps made explicit | two separately written heads that merge said one thing; so the later write failed to recognise the earlier one; the later write&#x27;s audit row holds what was searched and what was seen; from that evidence the kind of miss is named |

### D8. `docs/design/enact/00_overview.md` — 4.5 Writing during work

| | |
|---|---|
| Text before | Updating and creating are the same operation seen from two sides, which is why the corpus sharpens instead of accumulating near-duplicates. |
| Text after | Updating and creating are the same operation seen from two sides: every write passes through this one decision, so what improves an existing chain lands as its next version, what is already said lands as a LEARN, and only the genuinely new starts a chain. That is why the corpus sharpens instead of accumulating near-duplicates. |
| Steps made explicit | every write faces the same decide step; an improvement becomes the chain&#x27;s next version; an exact repeat becomes a LEARN; only genuinely new text starts a chain; hence near-duplicates cannot accumulate and the corpus sharpens |

### D9. `docs/design/enact/00_overview.md` — 4.5 Writing during work

| | |
|---|---|
| Text before | And from the service&#x27;s side, four checks — the first of them two-sided — and a commit — every check a lookup or a set operation, and every refusal an answer in facts rather than an opinion: |
| Text after | And from the service&#x27;s side, four checks and a commit. The first check is two-sided — the tag values must be in the closed list, and the tags must leave the memory findable — so it fills the first two rows below. Every check is a lookup or a set operation, and every refusal an answer in facts rather than an opinion: |
| Steps made explicit | there are four checks but the table below has five rows; the first check&#x27;s two sides are the closed vocabulary and findability; so rows one and two of the table are one check (matching §7&#x27;s list of the four) |

### D10. `docs/design/enact/00_overview.md` — 4.5 Writing during work

| | |
|---|---|
| Text before | and reconciles by content hash, so an edited entry becomes a new version rather than a duplicate and a `git pull` is picked up on the next load. |
| Text after | and reconciles by content hash: an entry whose hash the store already holds has already landed and lands nothing new, while an edited entry hashes differently and becomes a new version of its chain rather than a duplicate. A `git pull` is therefore picked up on the next load. |
| Steps made explicit | ingestion compares each entry&#x27;s content hash with what the store holds; a known hash means the entry already landed and lands nothing new; a changed entry hashes differently and extends its chain as a new version; so edits arriving through git are absorbed at the next load |

### D11. `docs/design/enact/00_overview.md` — 4.6 Building and growing the vocabulary — What "described" means

| | |
|---|---|
| Text before | adequacy is what the study measures (agreement between two passes) and what the human reviews, and what a recollection failure tests afterwards: a classification mismatch (§4.7) traced to two sessions reading one description two ways is the study&#x27;s κ, met in the field, and the signal to rewrite. |
| Text after | Whether the description is *adequate* is judged three times: the study measures it, as agreement between two passes; the human reviews it; and a recollection failure tests it afterwards. When a classification mismatch (§4.7) is traced to two sessions reading one description two ways, that is the same disagreement the study&#x27;s κ measures, met now in the field — and it is the signal to rewrite the description. |
| Steps made explicit | adequacy is judged at three moments: study, review, field; the study&#x27;s measure of it is two-pass agreement (κ); a classification mismatch traced to two readings of one description is that same disagreement occurring in real work; such a mismatch is the signal to rewrite that description |

### D12. `docs/design/enact/00_overview.md` — 4.8 Foundations of the vocabulary work

| | |
|---|---|
| Text before | The first the human reviews; the second the service can measure. |
| Text after | The first rule — one characteristic per facet — the human reviews; the second — independence — the service can measure. |
| Steps made explicit | &#x27;the first&#x27; is the one-characteristic rule; &#x27;the second&#x27; is independence; review versus measurement now attach to named rules |

### D13. `docs/design/enact/00_overview.md` — 4.8 Foundations of the vocabulary work

| | |
|---|---|
| Text before | which two values the passes swapped, on which entries. That points at one boundary sentence. |
| Text after | which two values the passes swapped, on which entries. Each pair points at one boundary sentence — the *when not to use it* line that draws the border between the two swapped values — and that sentence is where the fix goes. |
| Steps made explicit | a confused pair names two swapped values and the entries they were swapped on; the border between two values lives in a codebook entry&#x27;s when-not-to-use line; so the pair points at that one sentence; rewriting that sentence is the fix |

### D14. `docs/design/enact/00_overview.md` — 4.9 Sources and locators

| | |
|---|---|
| Text before | stored as an artefact bound to the document&#x27;s hash, so that a changed document invalidates the structure derived from it and not the other way round. |
| Text after | stored as an artefact bound to the document&#x27;s hash. The binding runs one way: when the document changes, its hash changes and the structure derived from it is invalidated; nothing derived from a document ever invalidates the document itself. |
| Steps made explicit | the structure is bound to the hash of the document it came from; a changed document changes its hash, invalidating the structure derived from it; the dependency never runs the other way: nothing derived can invalidate the document |

### D15. `docs/design/enact/00_overview.md` — 4.11 Consolidating a session — The rhythm

| | |
|---|---|
| Text before | the agent asks the service for what the session has written so far — every audit row carries its session (§4.4) — read them together with the spaces they landed in, and look for a point several of them make. |
| Text after | the agent asks the service for what the session has written so far, which the service can list because every audit row carries its session (§4.4). The agent reads those memories together with the spaces they landed in, and looks for a point several of them make. |
| Steps made explicit | the service can list a session&#x27;s writes because each audit row names its session; the agent then reads those memories and the spaces they landed in; and looks for a shared point |

### D16. `docs/design/enact/00_overview.md` — 4.11 Consolidating a session — What the service checks

| | |
|---|---|
| Text before | Naming an instance counts as having read it, so the look-before-writing check does not ask for it twice. |
| Text after | Naming an instance in `generalises` counts as having read it, so the look-before-writing check does not ask for it to be named again in `seen`. |
| Steps made explicit | &#x27;naming&#x27; happens in the generalises list; the look-before-writing check treats a named instance as seen; so the writer need not repeat it in seen |

### D17. `docs/design/enact/00_overview.md` — 4.11 Consolidating a session — How retrieval treats it

| | |
|---|---|
| Text before | The fold happens before ranking and the budget, so it depends on neither: the evidence travels with its generalisation, and a retired generalisation is no candidate, so its instances unfold. |
| Text after | The fold happens before ranking and the budget, so whether a case folds depends on neither its rank nor the room left in the budget: the evidence travels with its generalisation wherever it is placed. And a case folds only under a generalisation that is itself a candidate, so a retired generalisation — no longer a candidate — releases its instances to be placed on their own again. |
| Steps made explicit | folding is applied to the candidate set before ranking orders it and before the budget cuts it; so a case&#x27;s fold never depends on rank or remaining budget; a case folds only under a generalisation that is a candidate; a retired generalisation leaves the candidate set, so its instances are placed on their own again |

### D18. `docs/design/enact/00_overview.md` — 4.12 Where memories live

| | |
|---|---|
| Text before | it reaches only that project&#x27;s sessions, never the ones that do not exist yet. |
| Text after | it reaches only that project&#x27;s sessions, never the sessions of projects that do not yet exist. |
| Steps made explicit | &#x27;the ones&#x27; meant sessions of future projects; knowledge about the person filed in one project can never reach a project created later |

### D19. `docs/design/enact/00_overview.md` — 4.12 Where memories live

| | |
|---|---|
| Text before | the standing knowledge a session starts with merges both: the global first, the project&#x27;s last, because where they disagree the nearer rule wins. `domain=any` is the test of what belongs there. |
| Text after | the standing knowledge a session starts with merges both — the global first, the project&#x27;s last, so that where they disagree the rule merged last stands, and that is the project&#x27;s: the nearer rule wins. `domain=any` is the test of what belongs there: knowledge that holds for every value of `domain` is about no one project, and that is what the global store holds. |
| Steps made explicit | merge order is global then project; on a disagreement the rule merged last stands; the project&#x27;s rule is the nearer one, so the order makes it win; domain=any claims every domain, i.e. no one project, which is the global store&#x27;s content — hence the test |

### D20. `docs/design/enact/00_overview.md` — 4.12 Where memories live

| | |
|---|---|
| Text before | a read names under `standing` the preferences in its space it did not place |
| Text after | a read names under `standing` the preferences that fall in its space but were not placed in the context |
| Steps made explicit | some preferences fall inside the read&#x27;s problem space; not all of them are placed in the returned context; the unplaced ones are named under standing |

### D21. `docs/design/enact/00_overview.md` — 4.12.1 Delivering at the moment

| | |
|---|---|
| Text before | A trigger may carry `term=&lt;word&gt;`, which is what the term filter was for: the subject tags cannot name. With it, the test-isolation rule arrives; without it, two generic preferences do. |
| Text after | A trigger may carry `term=&lt;word&gt;`; the term filter exists for exactly this, narrowing by a word where the subject tags cannot name the space. With the term, the trigger delivers the test-isolation rule; without it, two generic preferences arrive instead. |
| Steps made explicit | the global store&#x27;s knowledge has no subject tags a path could match; the term filter narrows by a word instead, which is the case it exists for; with the term the wanted rule (test isolation) is delivered; without it only generic preferences arrive |

### D22. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 0 — Starting up, Panel 3

| | |
|---|---|
| Text before | A file whose content no longer matches its digest was edited in place, which content never is; it is left out of the snapshot and named, and the other memories load. |
| Text after | A file whose content no longer matches its recorded digest was edited in place — and content is never edited in place, so the mismatch marks an edit that should never have happened. The file is left out of the snapshot and named, and the other memories load. |
| Steps made explicit | each memory file records the digest its content had when written; content is never legitimately edited in place; so a digest mismatch proves an illegitimate in-place edit; the file is excluded and named; the rest load |

### D23. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 1.2 — The agent prepares the D&D domain, Panel 7

| | |
|---|---|
| Text before | The evidence sheet has one row per entry, per dimension, per pass, and a coding dictionary that *is* the proposal. |
| Text after | The evidence sheet has one row per entry, per dimension, per pass, and a coding dictionary — the candidate dimensions and values with their descriptions. That dictionary is what the study proposes: accepted, it becomes the pack. |
| Steps made explicit | the coding dictionary is the candidate dimensions and values with their descriptions; that same artefact is what the study submits as its proposal; on acceptance it becomes the domain pack |

### D24. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 1.3 — A word the vocabulary lacks turns up during work, Panel 1

| | |
|---|---|
| Text before | one is genuinely new to the vocabulary, one is already covered by another tag — the agent cannot always tell which, and does not have to. |
| Text after | one is genuinely new to the vocabulary, one is already covered by another tag. The agent cannot always tell which is which, and does not have to: both go in as proposals, and the human&#x27;s review tells them apart — one accepted, one rejected with the tag that covers it. |
| Steps made explicit | the agent cannot reliably distinguish new from covered; it need not, because both become proposals; the human&#x27;s review distinguishes them: accept the new, reject the covered naming the covering tag |

### D25. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 3.1 — The right note is in a neighbouring space, Panel 2

| | |
|---|---|
| Text before | The agent softens `task` — a query property, not a taxonomy one — and reads again. |
| Text after | The agent softens `task` and reads again — hard versus soft is a property of the query, not of the taxonomy, so only this one read is changed and the dimension&#x27;s stored default is not. |
| Steps made explicit | hard versus soft is decided per query; softening therefore changes only this read; the taxonomy&#x27;s stored default role is untouched |

### D26. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 3.1 — The right note is in a neighbouring space, Panel 4

| | |
|---|---|
| Text before | Its content hash never moved. |
| Text after | Its content hash never moved: the promotion added an association, which is index, and the content was never touched. |
| Steps made explicit | a promotion adds an association; associations are index, not content; so the content, and hence its hash, is unchanged |

### D27. `docs/design/enact/00a_how_a_memory_is_made.md` — Feature 6 — A note turns out to be wrong

| | |
|---|---|
| Text before | A correction is a birth plus a retirement, whichever door the memory came through — and it is the same `remember` call as Feature 2, with the same checks. |
| Text after | A correction is a birth plus a retirement: the corrected text is written as a new memory that supersedes the wrong one, and the wrong one leaves retrieval — whichever door the memory came through. It is the same `remember` call as Feature 2, with the same checks. |
| Steps made explicit | the birth is a new memory holding the corrected text, superseding the wrong head; the retirement is the wrong head leaving retrieval; this holds for every door a memory entered through; the call and checks are the ordinary write&#x27;s |

### D28. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 2.3 — A variant of a spell, Panel 5

| | |
|---|---|
| Text before | Note `seen` now lists #6 as well — the agent wrote it a moment ago, and the service would hand it back otherwise. |
| Text after | Note `seen` now lists #6 as well: #6 became a head in this space a moment ago, when the agent wrote it, so it is in the candidate set — and had it been missing from `seen`, the service would hand it back. |
| Steps made explicit | #6 is a head carrying this space&#x27;s hard tags, so it is a candidate; the agent has read it, having just written it; the no-unread-write check subtracts seen from candidates; leaving #6 out of seen would make the check fail and the service hand the candidate back |

## Every unpack of round 2, in full

### D29. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 2.3, Panel 1

| | |
|---|---|
| Text before | The read. Everything under the hard tags comes back, ordered by the soft ones — including the corruption note, at the bottom, because a hard match is a hard match. |
| Text after | The read. Everything under the hard tags comes back, ordered by the soft ones — including the corruption note, at the bottom: it carries every hard tag, so it is admitted, and hard tags only admit; it matches almost none of the soft tags, and soft tags only rank, so it ranks last. |
| Steps made explicit | hard tags only admit or exclude, they never rank; the corruption note carries every hard tag, so it is admitted; soft tags only rank, they never exclude; the note matches almost none of the soft tags, so it ranks last instead of being left out |

### D30. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 0 — Starting up (intro)

| | |
|---|---|
| Text before | Startup builds both from files — and the same files always build the same snapshot, down to its version number, so a retrieval from before the restart can still be rerun after it. |
| Text after | Startup builds both from files, and the same files always build the same snapshot, down to its version number. A retrieval&#x27;s receipt names the snapshot version it was answered from; because a restart rebuilds that same version from the same files, the name on the receipt still points at the same snapshot, so a retrieval from before the restart can still be rerun after it. |
| Steps made explicit | a retrieval&#x27;s receipt names the snapshot version it was answered from; the same files rebuild the same snapshot under the same version number; so after a restart the version named on the receipt still points at the same snapshot; which is why the retrieval can be rerun |

### D31. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 0, Panel 2

| | |
|---|---|
| Text before | Files are canonical, so every fact comes from the repository; the SQLite cache is only trusted where its content digests agree with the files, and rebuilt where they do not. |
| Text after | Files are canonical: every fact comes from the repository&#x27;s files, and the SQLite cache only repeats them for speed. The cache is trusted where its content digests agree with the files&#x27; — agreement shows the cache still repeats what the file says — and rebuilt from the files where they do not. |
| Steps made explicit | files are the canonical record; the cache only repeats them for speed; a digest agreeing between cache and file shows the cache still repeats what the file says; where the digests disagree the cache is rebuilt from the files, never the other way |

### D32. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 0, Panel 5

| | |
|---|---|
| Text before | Sources are cited, not loaded, so all startup does is rehash each document whole. |
| Text after | Sources are cited, not loaded: the store keeps a locator into each document and the document&#x27;s digest, never the document&#x27;s text. So the only check startup can make is the digest: it rehashes each document whole and compares the result with the digest on record. |
| Steps made explicit | the store keeps a locator and a digest per source, never the source&#x27;s text; with no text held, the only possible startup check is the digest; so startup rehashes each document whole and compares against the digest on record |

### D33. `docs/design/enact/00a_how_a_memory_is_made.md` — Scenario 7.1, Panel 6

| | |
|---|---|
| Text before | The cases fold under the pattern: named, one `show` away, and not paid for twice. |
| Text after | The cases fold under the pattern: the read returns the principle&#x27;s text, lists the four cases by number and title under it, and spends none of its budget on their texts — each is one `show` away. |
| Steps made explicit | the read returns the accepted principle&#x27;s text in full; the cases appear only as number and title under it; so the token budget is not spent on their texts; a case&#x27;s full text is one show call away |

### D34. `docs/design/enact/00_overview.md` — §4.5 Writing during work — the no-unread-write paragraph

| | |
|---|---|
| Text before | it makes *never looked* impossible, and it leaves behind exactly the evidence needed to name any miss that slips through (§4.7). |
| Text after | it makes *never looked* impossible: a `new` write is refused while any candidate in its space is missing from `seen`, so by the time a write lands, every candidate has been named as read. And the `seen` list kept in the audit row is exactly the evidence needed to name any miss that slips through (§4.7). |
| Steps made explicit | a new write is refused while any candidate in its space is missing from seen; so a write that lands proves every candidate was named as read — never looked cannot have happened; the seen list is kept in the audit row, which is the evidence later misses are judged by |

### D35. `docs/design/enact/00_overview.md` — §4.8 Foundations — facet analysis

| | |
|---|---|
| Text before | The first rule — one characteristic per facet — the human reviews; the second — independence — the service can measure. |
| Text after | The first rule — one characteristic per facet — asks what a facet *means*, so only the human can review it; the second — independence — is a number, the association between two dimensions over the coded sample (Cramér&#x27;s V, below), so the service can measure it. |
| Steps made explicit | the one-characteristic rule is a question about meaning, which the service never judges; so the human reviews it; independence is a computable number: the association between two dimensions over the coded sample; so the service measures it |

### D36. `docs/design/enact/00_overview.md` — §4.12 — delivery

| | |
|---|---|
| Text before | `oo enact status --standing` prints the lot for a host&#x27;s session-start hook, which is the only delivery that depends on nobody&#x27;s compliance. |
| Text after | `oo enact status --standing` prints the lot for a host&#x27;s session-start hook, which is the only delivery that depends on nobody&#x27;s compliance: the host runs the hook itself at every session start, where the instructions rely on the host keeping them whole and `session` relies on the agent choosing to call it. |
| Steps made explicit | the server&#x27;s instructions reach the agent only if the host keeps them whole, which it does not (the 2,048-character cap); the session tool reaches the agent only if the agent chooses to call it; the hook runs by the host&#x27;s own configuration at every session start, needing no one&#x27;s cooperation; that is why it is the only delivery that depends on nobody&#x27;s compliance |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/design/enact/00_overview.md` — 4.10 Procedures — How retrieval treats it | The context builder then places the head procedure for the query&#x27;s (artifact, task) pair *first*, outside the ranked list and outside the token budget&#x27;s ordering, and at most one. | either the procedure is exempt only from budget-driven ordering and cutoff but still consumes tokens, or it does not count against the token budget at all |
| 1 | `docs/design/enact/00_overview.md` — 4.12 Where memories live | So the instructions now carry the loop and an index of titles, newest first, with the count of what the index leaves out | &#x27;the loop&#x27; could be §4.5&#x27;s writing loop (file, look, decide, write, review) or the whole session use-loop the server&#x27;s startup instructions describe |
| 1 | `docs/design/enact/00a_how_a_memory_is_made.md` — The whole thing in two frames | Three doors in for a memory, one door out, and the way out is not a delete. | the state diagram above shows four entry arrows; three doors reads as WRITE (mid-task and consolidation counted as one door), ingestion, merge — or the count simply omits the generalising write |
| 2 | `docs/design/enact/00a_how_a_memory_is_made.md` — The whole thing in two frames — closing paragraph | Every arrow in both frames leaves an audit row, which is what makes the history goal (G10) hold: the state of the index at any past moment is the log | the file&#x27;s last sentence ends mid-clause with no period — the intended completion (probably &#x27;replayed to that moment&#x27;) is missing, and supplying it would be adding content: a candidate defect, not a wording choice |
