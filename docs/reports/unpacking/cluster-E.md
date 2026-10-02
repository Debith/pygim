# Unpacking pass — cluster E

[The report](../2026-09-28-unpacking-pass.generated.html) · ENACT design, sections 01, 01a and 02 · 31 unpacked, 4 left as written, over two rounds.

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

### E1. `docs/design/enact/01_domain_model.md` — ## 2. What a dimension is, in practice

| | |
|---|---|
| Text before | That independence is what the vocabulary study measures (overview §4.8) and what keeps the answer set a flat product rather than a tree. |
| Text after | That independence is what the vocabulary study measures (overview §4.8). It is also what keeps the answer set flat: because no answer narrows any other, every combination of answers can occur, so the set is a product of the dimensions rather than a tree in which one answer restricts the next. |
| Steps made explicit | independence means no answer constrains any other; therefore every combination of answers can occur; a space of free combinations is a product of the dimensions; a hierarchy, where one answer restricts the next, would be a tree |

### E2. `docs/design/enact/01_domain_model.md` — ### 2.2 The content digest, and a memory's key

| | |
|---|---|
| Text before | Content can repeat: a correction reverted to the old text has the old digest and is still a different memory, with different lineage and a different place in its chain (section 03 §2.2). |
| Text after | The digest cannot be the identity, because content can repeat: a correction that reverts to the old text carries the old content, so it has the old digest — and it is still a different memory, created by a different row, with different lineage and a different place in its chain (section 03 §2.2). |
| Steps made explicit | the sentence is the argument for key over digest, now stated as such; the scenario: a correction reverts a memory to its earlier text; same content, so the digest repeats the old one; yet the memory is distinct: a different row created it, so key, lineage and chain position differ |

### E3. `docs/design/enact/01_domain_model.md` — ### 2.2 The content digest, and a memory's key (Decision 2.2)

| | |
|---|---|
| Text before | Sixty-four bits gives a birthday collision near 1e-10 for a corpus of 100 000 — small, but the failure mode is a *silently dropped write*, so the second lane is cheap insurance. SHA-256 would add a dependency for a boundary this is not: a repository&#x27;s trust model is git&#x27;s, and an adversary who can rewrite the associations file does not need a hash collision. |
| Text after | Sixty-four bits gives a birthday collision near 1e-10 for a corpus of 100 000. That chance is small, but follow what a collision would do: two different contents share one digest, the identical-content check takes the new write for content already stored, and the new text is never written — a *silently dropped write*. Against that failure mode, the second lane is cheap insurance. SHA-256 would add a dependency, and what the dependency buys — protection against a collision constructed on purpose — matters only at a trust boundary, which this digest is not: a repository&#x27;s trust model is git&#x27;s, and an adversary who can rewrite the associations file does not need a hash collision. |
| Steps made explicit | a collision means two different contents share one digest; the identical-content check compares digests, so it takes the new write for a duplicate; the new text is therefore never written — that is the silently dropped write; what SHA-256 buys is protection against a deliberately constructed collision; that protection matters only at a trust boundary, and this digest is not one: git is the trust model, and a file-rewriting adversary needs no collision |

### E4. `docs/design/enact/01_domain_model.md` — ## 3. Weights and scores are integers

| | |
|---|---|
| Text before | The ranking key is a total order over distinct memories — negated score, then negated count of soft hits, then the id, which always terminates the comparison. |
| Text after | The ranking key is negated score, then negated count of soft hits, then the id. Distinct memories never share an id, so when score and hit count both tie, the id still decides: the comparison always terminates, and the order over distinct memories is total. |
| Steps made explicit | the key&#x27;s three components, in order; ids are unique across distinct memories; so a tie on score and hit count is still decided by the id; which is why the comparison terminates and the order is total |

### E5. `docs/design/enact/01_domain_model.md` — ## 4. Knowledge (Decision 4.1)

| | |
|---|---|
| Text before | And *only the head is findable* stays a single comparison at read time rather than a walk, without that convenience costing content its immutability. |
| Text after | And *only the head is findable* stays cheap at read time: `memory_state` carries `superseded_by`, so headness is one comparison — is it empty — rather than a walk of the chain. Because that marker lives in the state and not in the content record, marking an old head superseded touches the index and never the written memory: the convenience does not cost content its immutability. |
| Steps made explicit | the single comparison is: is memory_state.superseded_by empty; the alternative is a walk of the chain&#x27;s supersede edges; the marker sits in the state, not the content record; so superseding writes to the index and never rewrites the stored memory — that is how the convenience leaves immutability intact |

### E6. `docs/design/enact/01_domain_model.md` — ## 5. Vocabulary

| | |
|---|---|
| Text before | That is a `dimension_proposal`, reviewed by the same human in the same way, and a `tag_proposal` may name a dimension that is itself still pending, so a pack is accepted as one reviewed set rather than dimension by dimension in the right order. |
| Text after | That is a `dimension_proposal`, reviewed by the same human in the same way. A `tag_proposal` may name a dimension that is itself still pending. Without that, a value could be accepted only after its dimension, and a pack would have to be taken dimension by dimension in the right order; with it, a pack is accepted as one reviewed set. |
| Steps made explicit | a value proposal may reference a dimension that is only proposed; were that not allowed, a value could enter only after its dimension existed; which would force accepting a pack dimension by dimension, in dependency order; the allowance is what lets the whole pack be accepted as one reviewed set |

### E7. `docs/design/enact/01_domain_model.md` — ## 6. Provenance

| | |
|---|---|
| Text before | A `locator` is trivially copyable and holds no path of its own — the inventory owns the path once, so moving a document is one edit rather than thousands. |
| Text after | A `locator` is trivially copyable and holds no path of its own: it names its source by id, and the inventory owns the path, once. Every locator into a document therefore points at the same path record, so when the document moves, the one inventory record is edited and every locator still resolves — one edit rather than thousands. |
| Steps made explicit | a locator carries a source id, not a path; the path exists once, in the inventory; all locators into a document therefore share that one path record; moving the document is one edit to the inventory, after which every locator still resolves — the thousands are the per-locator edits avoided |

### E8. `docs/design/enact/01a_model_by_scenario.md` — ### Scenario 5.1 — An ingested note duplicates a chain

| | |
|---|---|
| Text before | The verdict is read from the lineage of the two sources, not judged. |
| Text after | The verdict is read from the lineage of the two memories merged, not judged: lineage says how each entered the store, and so whether a look step was ever expected. |
| Steps made explicit | &quot;sources&quot; here meant the two merged memories, not inventoried source documents — `source` is a defined term, so the referent was made explicit; lineage records how a memory entered the store (written vs seed); an ingested memory owed no look step, which is the fact the verdict states — read off records, not judged |

### E9. `docs/design/enact/02_taxonomy_and_sources.md` — ### 1.1 One file for the base, one per pack

| | |
|---|---|
| Text before | One file per pack keeps two people growing two domains out of each other&#x27;s diffs. |
| Text after | One file per pack keeps two people growing two domains out of each other&#x27;s diffs: each domain grows in its own file, so neither person&#x27;s changes turn up in the diffs the other reviews. |
| Steps made explicit | a pack is a file, and a domain is a pack; two people growing two domains therefore edit two different files; so their changes never appear in the diffs the other reviews |

### E10. `docs/design/enact/02_taxonomy_and_sources.md` — ### 1.3 Names on disk, ids in memory

| | |
|---|---|
| Text before | Two repositories that grew their packs in a different order can exchange memories. |
| Text after | Two repositories that grew their packs in a different order assign different ids to the same tag — and can still exchange memories, because the files exchanged carry names, not ids. |
| Steps made explicit | ids are assigned in load order, so a different pack order gives the same tag different ids in each repository; exchange works anyway, because exchanged files carry qualified names; each repository resolves those names to its own ids on load |

### E11. `docs/design/enact/02_taxonomy_and_sources.md` — ### 2.3 What "complete" means, mechanically

| | |
|---|---|
| Text before | A drafted pack is checked before a person accepts it (`check_pack`, and again by `oo enact accept --pack`), and what the loader cannot see comes back beside what it can. Every locator the pack adds is resolved against its document, and a warning names each that does not hold: the document is not in the store&#x27;s inventory or the draft&#x27;s; it is not found; the lines there are not the cited passage; or the cited text also occurs on other lines. A path is tried against the project&#x27;s root, the store&#x27;s own root — a store whose subject is a body of knowledge keeps its sources inside itself and belongs to no checkout — and the inventory file&#x27;s directory; a &quot;not found&quot; says which of those it looked in, since a check that cannot say where it looked is one people learn to ignore. The last is the field report&#x27;s case — a locator found by matching the text &quot;Attack&quot; took the Action entry&#x27;s list item (`L61`), not the Attack Roll entry (`L131`), and nothing flagged it. They are warnings, not refusals: a repeated line can be the right one. |
| Text after | A drafted pack is checked before a person accepts it (`check_pack`, and again by `oo enact accept --pack`), and the check&#x27;s result holds what the loader cannot see beside what it can: every locator the pack adds is resolved against its document, and a warning names each that does not hold. Four things can fail to hold: the document is in neither the store&#x27;s inventory nor the draft&#x27;s; the document is not found; the lines there are not the cited passage; or the cited text also occurs on other lines. A document is looked for by its path, tried against three places: the project&#x27;s root; the store&#x27;s own root, because a store whose subject is a body of knowledge keeps its sources inside itself and belongs to no checkout; and the inventory file&#x27;s directory. A &quot;not found&quot; says which of those it looked in, since a check that cannot say where it looked is one people learn to ignore. The fourth warning — the cited text also occurring on other lines — is the field report&#x27;s case: a locator found by matching the text &quot;Attack&quot; took the Action entry&#x27;s list item (`L61`), not the Attack Roll entry (`L131`), and nothing flagged it. They are warnings, not refusals: a repeated line can be the right one. |
| Steps made explicit | the warning causes are counted — four — so they can be referred to later; the three roots a path is tried against become their own sentence, and the aside about the store&#x27;s root becomes its explicit justification (because) instead of an interruption inside the list; &quot;the last&quot; had to be resolved backwards across the intervening roots sentence; it is now named: the fourth warning, cited text also on other lines; the field report anecdote is tied to that fourth warning explicitly |

### E12. `docs/design/enact/02_taxonomy_and_sources.md` — ### 2.4 Identity, and hand edits

| | |
|---|---|
| Text before | a merge of two branches that grew different packs gets a version of its own without either side having to count. |
| Text after | a merge of two branches that grew different packs gets a version of its own — the merged content simply digests to a new value — without either side having to count. |
| Steps made explicit | the version is a digest of content, so the merged vocabulary&#x27;s version falls out of the merged content itself; that is why neither branch has to produce or reconcile a number |

### E13. `docs/design/enact/02_taxonomy_and_sources.md` — ### 3.1 The life of a proposal

| | |
|---|---|
| Text before | after the replacement it would carry a tag the vocabulary no longer has, and on a hard dimension no read could find it. |
| Text after | after the replacement it would carry a tag the vocabulary no longer has, no query could name the dropped value any more, and on a hard dimension that means no read could find it. |
| Steps made explicit | a query is built from vocabulary values, so a dropped value can no longer be asked for; a memory whose value on a hard dimension can no longer be named therefore cannot be reached by a read on that dimension |

### E14. `docs/design/enact/02_taxonomy_and_sources.md` — ### 3.3 Retiring a value

| | |
|---|---|
| Text before | Those rows are history: a review is raised only for a head that still carries the tag at the end of replay, one per memory and tag, so a value that was unlinked before it went raises nothing. |
| Text after | Those rows are history: replay keeps them and does not fail on them. A review is raised only for a head that still carries the tag once replay has finished — one per memory and tag — so a value that was unlinked before it went, leaving no head that carries it, raises nothing. |
| Steps made explicit | rows naming a vanished tag are replayed as history, not treated as errors; reviews come from the end state of replay, not from meeting such a row; one review per memory and tag, only for heads still carrying the tag; so a value unlinked before it disappeared leaves no head carrying it, and raises nothing |

### E15. `docs/design/enact/02_taxonomy_and_sources.md` — ### 5.2 The inventory record

| | |
|---|---|
| Text before | `cite` gives a document the id its inventory entry already has, found by path, and makes one from the path only for a document the inventory lacks — so a vocabulary value and a memory never name one document two ways. |
| Text after | `cite` looks a document up in the inventory by its path: a document already inventoried gets the id its entry already has, and only a document the inventory lacks gets a new id made from the path — so one document has one id, and a vocabulary value and a memory never name it two ways. |
| Steps made explicit | cite&#x27;s first move is a lookup by path; an inventoried document keeps its existing id; only an absent document gets a new id, derived from the path; hence one id per document, and no two citers can name it differently |

### E16. `docs/design/enact/02_taxonomy_and_sources.md` — ### 5.2 The inventory record

| | |
|---|---|
| Text before | Not relative to the inventory file: a store may live outside the checkout — on its own branch, or in a user directory (03 §9.1) — and every worktree of the project must resolve the same document from the same path. |
| Text after | There is a second reason not to anchor a path to the inventory file: a store may live outside the checkout — on its own branch, or in a user directory (03 §9.1) — and every worktree of the project must resolve the same document from the same path. |
| Steps made explicit | the fragment repeated the decision after the D-D-2024 anecdote had already argued it, and read as a stray restatement; made explicit that it is a second, independent reason for the same choice: the anecdote covers the store moving, this covers a store outside the checkout with several worktrees |

## Every unpack of round 2, in full

### E17. `docs/design/enact/01_domain_model.md` — 2.2 The content digest, and a memory's key

| | |
|---|---|
| Text before | The hash of a memory&#x27;s content does two jobs: it is what &quot;identical content is already a head&quot; compares (overview §4.5), and it proves a stored text is the text that was written. |
| Text after | The hash of a memory&#x27;s content does two jobs. First, it is what the &quot;identical content is already a head&quot; check compares (overview §4.5): the service digests the incoming content and looks for a head whose recorded digest equals it — equal digests mean the content is already stored. Second, it proves a stored text is the text that was written: re-digest the stored text and compare with the recorded digest; a mismatch means the text is not the one the write carried. |
| Steps made explicit | the check digests the incoming content; it compares that digest against heads&#x27; recorded digests; equality means the content is already stored; integrity is proven by re-digesting the stored text and comparing with the recorded digest |

### E18. `docs/design/enact/01_domain_model.md` — 2.4 Why the index is kept in both directions

| | |
|---|---|
| Text before | Written with `id_set`, it is a handful of word-at-a-time operations, and the counting variants answer *how many* candidates there would be without building the set at all. |
| Text after | Written with `id_set`, it is a handful of word-at-a-time operations. The counting variants combine the same words and count the set bits of each combined word as they go — a popcount — so they answer *how many* candidates there would be without ever allocating the candidate set. |
| Steps made explicit | the counting variants perform the same word-level combination; they count set bits per combined word (popcount); so the size is known without allocating the result set |

### E19. `docs/design/enact/01_domain_model.md` — 3.1 Reproducing a retrieval later

| | |
|---|---|
| Text before | Counters rise on *reads*, which are asynchronous observations that no snapshot version pins. |
| Text after | Counters rise on *reads*: each observation lands asynchronously as a usage record, and a usage record is not an audit row — of LEARN, only a promotion writes one (§3.1) — so counters move without moving the version. No snapshot version therefore names the counters&#x27; state at any moment. |
| Steps made explicit | a read&#x27;s observation lands asynchronously as a usage record; a usage record is not an audit row; only a LEARN promotion writes one; versions are moved by rows, so counters move without moving the version; hence no snapshot version names a counter state |

### E20. `docs/design/enact/01_domain_model.md` — 5. Vocabulary

| | |
|---|---|
| Text before | The taxonomy is append-only, like the interner beneath it: accepting a proposal adds a tag and nothing ever removes one, so an id in a memory written last year still resolves. |
| Text after | The taxonomy is append-only, like the interner beneath it: accepting a proposal appends a tag at the end, and nothing ever removes or reorders one. An id is a position in that insertion order, so no later append can change what an existing id points at — which is why an id in a memory written last year still resolves. |
| Steps made explicit | accepting appends at the end; nothing removes or reorders; an id is a position in insertion order; appends cannot change existing positions; so old ids keep resolving |

### E21. `docs/design/enact/01a_model_by_scenario.md` — Scenario 1.3

| | |
|---|---|
| Text before | The write lands at once under the tags that exist; the proposal rides along, pending, and points into a document the inventory does not hold yet. |
| Text after | The write lands at once, under only the tags that already exist. The proposal travels in the same `write_request`, in its `proposals` field; the service stores it as pending rather than holding the write back for it. The proposal&#x27;s justifying locator points into a document the inventory does not hold yet. |
| Steps made explicit | replaced the &#x27;rides along&#x27; metaphor with the carrier: the write_request&#x27;s proposals field; the service stores the proposal as pending; the write is not blocked on the proposal; the locator is the proposal&#x27;s justification |

### E22. `docs/design/enact/01a_model_by_scenario.md` — Scenario 1.4

| | |
|---|---|
| Text before | A rejection is data too: it is published with the vocabulary so it is not proposed again. |
| Text after | A rejection is data too: it is kept and handed to the agent with the vocabulary at the next session, so the agent sees that the concept was already rejected, and the reason, and does not propose it again. |
| Steps made explicit | the rejection is stored; it is handed to the agent with the vocabulary at the next session; the agent reads the rejection and its reason; which is what stops the re-proposal |

### E23. `docs/design/enact/01a_model_by_scenario.md` — Scenario 2.1

| | |
|---|---|
| Text before | The first is the procedure, found through the locator the study left on `task=balance`. |
| Text after | The first is the procedure. The vocabulary study left a locator on the value `task=balance`; the agent has the service resolve that locator, reaches the passage it names, and writes the procedure citing that passage. |
| Steps made explicit | the study attached a locator to the tag value; the agent asks the service to resolve it; resolution yields the source passage; the procedure memory is written citing that passage |

### E24. `docs/design/enact/01a_model_by_scenario.md` — Scenario 3.1

| | |
|---|---|
| Text before | Nothing is written. Three usage records in three sessions become one learned `association`. |
| Text after | Nothing is written. In each of three sessions the agent reports the same observation with a `learn` call, and each call lands as one usage record. The records are folded into the memory&#x27;s counters; when the count reaches the threshold, the service promotes the observation into one learned `association`. |
| Steps made explicit | each session reports via a learn call; each call lands as a usage record; records fold into counters; at the threshold the service promotes to a learned association |

### E25. `docs/design/enact/01a_model_by_scenario.md` — Scenario 3.2

| | |
|---|---|
| Text before | The same `association` as a promotion, reached in one step, with a reason in the human&#x27;s words. |
| Text after | The `association` that results has the same shape a promotion produces; the difference is the path to it. The human&#x27;s `link` call creates it in one step, with source `curated` and a reason in the human&#x27;s own words, instead of three usage records reaching a threshold. |
| Steps made explicit | the resulting association has the promotion&#x27;s shape; the human&#x27;s link call creates it directly; source is curated, reason is the human&#x27;s; contrasted with the threshold path of 3.1 |

### E26. `docs/design/enact/01a_model_by_scenario.md` — Scenario 5.1

| | |
|---|---|
| Text before | The verdict is read from the lineage of the two memories merged, not judged: lineage says how each entered the store, and so whether a look step was ever expected. |
| Text after | The verdict is read from the lineage of the two memories merged, not judged. Lineage records how each entered the store: a memory whose lineage is `seed` was ingested from a corpus file, so no session read candidates before writing it, and no look step was ever expected of it; one whose lineage is `written` came from a session that had read first. |
| Steps made explicit | lineage records the entry path; seed lineage means ingestion from a corpus file; ingestion has no prior read, so no look step was expected; written lineage means a session that had read first |

### E27. `docs/design/enact/01a_model_by_scenario.md` — Scenario 5.2

| | |
|---|---|
| Text before | The verdict comes from two records: what the later write saw, and what the earlier head carried at that moment. |
| Text after | The verdict comes from two records. The later write&#x27;s request records what its writer saw; the earlier head&#x27;s `memory_state` records the tags it carried at that moment. Comparing them shows whether those tags could have admitted the earlier head into the later write&#x27;s candidate set at all; here they could not — #5 carried `task=design` only, and the write&#x27;s read filtered on `task=critique` — so the duplicate is a classification mismatch. |
| Steps made explicit | named the two records: the write_request&#x27;s seen and the head&#x27;s memory_state tags; the comparison asks whether the old head could have been a candidate of the new write&#x27;s read; #5&#x27;s tags could not pass the task=critique hard filter; hence the classification-mismatch verdict |

### E28. `docs/design/enact/01a_model_by_scenario.md` — Feature 7 — Consolidate what a session wrote

| | |
|---|---|
| Text before | nothing on the instances changes except the edge that points back. |
| Text after | nothing on the instances changes except that each instance&#x27;s `memory_state` gains a `generalised_by` edge pointing back at the generalisation. |
| Steps made explicit | named where the edge lives: each instance&#x27;s memory_state; named the edge: generalised_by; stated its direction: back at the generalisation |

### E29. `docs/design/enact/02_taxonomy_and_sources.md` — 3.1 The life of a proposal

| | |
|---|---|
| Text before | after the replacement it would carry a tag the vocabulary no longer has, no query could name the dropped value any more, and on a hard dimension that means no read could find it. |
| Text after | after the replacement it would carry a tag the vocabulary no longer has, and no query could name the dropped value any more. On a hard dimension that is fatal: a memory is admitted only when the query names one of its values there, so a read that filters on that dimension could never find the memory again. |
| Steps made explicit | the memory would carry a tag the vocabulary lacks; no query can name the dropped value; hard admission requires the query to name one of the memory&#x27;s values on that dimension; so reads filtering on that dimension can never admit it |

### E30. `docs/design/enact/02_taxonomy_and_sources.md` — 4.1 The four measures, exactly

| | |
|---|---|
| Text before | the largest share any single value of *d* takes among the entries that have one |
| Text after | for each value of *d*, the share of pass-A entries carrying it, out of the entries that carry any value of *d*; discrimination is the largest of those shares |
| Steps made explicit | per value, count pass-A entries carrying it; the denominator is the entries carrying any value of d; the measure is the largest of those shares — measurement stated before the reader must lean on the metric name |

### E31. `docs/design/enact/02_taxonomy_and_sources.md` — 5.5 Derived structure

| | |
|---|---|
| Text before | every item carrying a line, and bound to the rendition&#x27;s digest. When the digest changes the structure is marked stale rather than trusted |
| Text after | every item carrying a line, and the file records the digest of the rendition it was read from. When the rendition&#x27;s current digest no longer equals the recorded one, the structure is marked stale rather than trusted |
| Steps made explicit | &#x27;bound to&#x27; made mechanical: the structure file records the source rendition&#x27;s digest; staleness is a comparison of current digest against recorded digest; mismatch marks the structure stale |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/design/enact/01_domain_model.md` — ## 7. Retrieval | Every retrieval also leaves a `retrieval_receipt` — query id, when, `snapshot_version`, `taxonomy_version`, the classification, and the ids returned. | §3.1&#x27;s table and diagram pin `snapshot_id` (with `snapshot_version` as the human-readable count); this list says `snapshot_version` — whether the receipt holds the id, the version, or both would be an assumption |
| 1 | `docs/design/enact/01a_model_by_scenario.md` — Section 01a intro (before Scenario 0) | The actors are always the same four: the **Agent** thinks, the **Service** checks and keeps the books, the **Index** is the snapshot the service reads, the **Store** is where commits land, and the **Human** governs the vocabulary and reviews in git. | says four but lists five; either the count is stale, or one of the five (the Index as a thing the Service reads, or the Human as outside the system) is deliberately not counted as an actor |
| 1 | `docs/design/enact/02_taxonomy_and_sources.md` — Appendix — taxonomy v0, as shipped | **question** — an open question, not a choice already made | the appendix gives `kind` seven values, while 01 §2 lists six (no question) and 01a Scenario 1.1 counts 11 values / 16 entries, which matches six; either question is a later addition the other sections have not caught up with, or it does not belong in v0 — a term-table cell, so logged rather than touched |
| 2 | `docs/design/enact/01_domain_model.md` — 4. Knowledge — Decision 4.1 | a derived store can rebuild every byte of it from the canonical file by content hash | &#x27;by content hash&#x27; could mean the hash locates/keys the canonical file, or that the hash verifies the rebuilt copy is byte-identical — unpacking would pick one |
