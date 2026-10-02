# Unpacking pass — cluster F

[The report](../2026-09-28-unpacking-pass.generated.html) · ENACT design, sections 03, 04 and 05 · 36 unpacked, 7 left as written, over two rounds.

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

### F1. `docs/design/enact/03_store.md` — 1. What is written down (Decision 1)

| | |
|---|---|
| Text before | The overview&#x27;s G10 asked that the index as it stood after any audit row can be rebuilt; making the log the only record makes that a definition rather than a feature. |
| Text after | The overview&#x27;s G10 asked that the index as it stood after any audit row can be rebuilt. With the log as the only canonical record, the index after any row *is* the replay of the rows up to it — so what G10 asked for stops being a feature to implement and becomes the definition of the index. |
| Steps made explicit | G10 asks that the index after any audit row can be rebuilt; the log is made the only canonical record; the index after any row is therefore defined as the replay of the rows up to it; so rebuildability is no longer a feature to implement — it is the definition of the index |

### F2. `docs/design/enact/03_store.md` — 3.1 Layout (content objects)

| | |
|---|---|
| Text before | **Content objects** are the memory text, named by the digest of the text and written by temp-file-and-rename, so writing one twice is a no-op and a torn write is never visible. |
| Text after | **Content objects** are the memory text. An object is named by the digest of its text, so the same text always lands at the same name, and writing it twice is a no-op. It is written by temp-file-and-rename: the bytes go to a temporary file first, and the rename is what makes the object appear, so a torn write is never visible. |
| Steps made explicit | the name is the digest of the text; the same text therefore always lands at the same name, so a second write is a no-op; separately, the bytes go to a temporary file first; the rename is the moment the object appears; so a reader never sees a half-written object |

### F3. `docs/design/enact/03_store.md` — 3.3 Head views

| | |
|---|---|
| Text before | The service compares each published snapshot with the one before it, so every change reaches the views through one place; opening a store checks every view, and a view already right is not rewritten, so git sees no churn. |
| Text after | The service compares each published snapshot with the one before it; whatever operation changed what a view shows, the change reaches the views through that one comparison. Opening a store checks every view, and a view already right is not rewritten, so git sees no churn. |
| Steps made explicit | the service diffs each published snapshot against the one before it; any operation that changes what a view shows changes that diff; so every kind of change reaches the views through the one comparison, with no per-operation path |

### F4. `docs/design/enact/03_store.md` — 3.5.1 A citation moves like a tag

| | |
|---|---|
| Text before | Replay copies the record before changing it, because earlier snapshots share it and a reader holding one must keep seeing the citations it was given. |
| Text after | Replay copies the record before changing it: earlier snapshots share the same record, so changing it in place would change what a reader holding an old snapshot sees — and that reader must keep seeing the citations it was given. |
| Steps made explicit | earlier snapshots share the same record object; changing it in place would therefore change what an old snapshot shows; a reader holding that snapshot must keep seeing the citations it was given; so replay copies the record before changing it |

### F5. `docs/design/enact/03_store.md` — 3.5.2 A timestamp is not an order

| | |
|---|---|
| Text before | twice now the answer was &quot;the greatest time, ties broken by row id&quot; — which inside one second is a coin toss between unrelated digests. |
| Text after | twice now the answer was &quot;the greatest time, ties broken by row id&quot;. Rows written inside the same second carry the same time, so the tie falls to their row ids — digests, whose order says nothing about which row was written first. Inside one second, &quot;latest&quot; is a coin toss. |
| Steps made explicit | time has second resolution, so rows written inside one second carry the same time; equal times push the decision to the tie-break, the row id; a row id is a digest, whose order has no relation to write order; so which row counts as latest is effectively random |

### F6. `docs/design/enact/03_store.md` — 3.5.3 Tested from outside, on what is left behind

| | |
|---|---|
| Text before | The isolation is the one a person gets from `$PYGIM_ENACT_GLOBAL`, `$XDG_DATA_HOME` and `$GIT_CONFIG_GLOBAL` — a process cannot be monkeypatched, which is part of what makes this suite worth its cost. |
| Text after | The isolation is the one a person gets from `$PYGIM_ENACT_GLOBAL`, `$XDG_DATA_HOME` and `$GIT_CONFIG_GLOBAL`, and it is the only kind on offer: a separate process cannot be monkeypatched. That impossibility is part of what makes this suite worth its cost. |
| Steps made explicit | the tests isolate themselves only through environment variables a person could also set; a separate process offers no other seam, because it cannot be monkeypatched; that impossibility is itself part of the suite&#x27;s value |

### F7. `docs/design/enact/03_store.md` — 3.6 The mailbox

| | |
|---|---|
| Text before | `mailbox` returns what is open, oldest first by time then id — one order whoever merged the clones — and `all` returns everything, since nothing is deleted. |
| Text after | `mailbox` returns what is open, oldest first by time then id — a key each message carries itself, so the order is the same whoever merged the clones — and `all` returns everything, since nothing is deleted. |
| Steps made explicit | the sort key is time then id; both are properties each message carries itself, not properties of file order; so however the clones&#x27; files were merged, every copy lists messages in the same order |

### F8. `docs/design/enact/03_store.md` — 4.1 The row is the point of no return (found 2026-09-23)

| | |
|---|---|
| Text before | The same helper names its temporary file `&lt;file&gt;.tmp`, which is safe only while every writer of that file holds the commit lock, and the store&#x27;s constructor writes `local/clone` without taking it. |
| Text after | The same helper names its temporary file `&lt;file&gt;.tmp` — one fixed name per file, so two writers of one file would be writing the same temporary file. That is safe only while every writer of that file holds the commit lock, and the store&#x27;s constructor writes `local/clone` without taking it. |
| Steps made explicit | the temporary name is fixed per target file; two concurrent writers of one file would therefore write the same temporary file; so the scheme is safe only when the commit lock serialises every writer; and the constructor writes local/clone without taking that lock |

### F9. `docs/design/enact/03_store.md` — 4.3 What is synchronous and what is not

| | |
|---|---|
| Text before | Replay applies the row; it never recounts usage, so a lost usage record can delay a promotion but never undo one. |
| Text after | Replay applies the row; it never recounts usage. So a usage record lost before the threshold is reached can delay the promotion; one lost after the row is committed changes nothing, because the row, not the count, is what replay applies. |
| Steps made explicit | before the threshold, the promotion exists only as counters, which may be lost — a lost record can delay it; once promoted, the association is an audit row, which is canonical; replay applies the row and never recounts usage; so no lost record can undo a committed promotion |

### F10. `docs/design/enact/03_store.md` — 5. When git merges two histories

| | |
|---|---|
| Text before | That is inherent — nobody can read what does not exist yet — and it is exactly the duplicate Feature 5 finds and names. |
| Text after | That is inherent — nobody can read what does not exist yet — so two people can write the same thing with neither having seen the other&#x27;s note. That duplicate is exactly the one Feature 5 finds and names. |
| Steps made explicit | the no-unread-write check cannot span rows the writers could not have seen; so two people can write the same thing, each unaware of the other&#x27;s note; the duplicate that results is the one Feature 5 exists to find and name |

### F11. `docs/design/enact/03_store.md` — 6. Loading (the checkpoint)

| | |
|---|---|
| Text before | It is trusted only when its row is an ancestor of the current head, and then only the rows after it are replayed. |
| Text after | It is trusted only when its row is an ancestor of the current head — everything it was built from is then part of the head&#x27;s own history — and then only the rows after it are replayed. |
| Steps made explicit | the checkpoint is the index as it stood at one row; if that row is an ancestor of the current head, every row the checkpoint was built from is also in the head&#x27;s history; so its state can be taken as given, and only the rows after it need replaying |

### F12. `docs/design/enact/04_index_and_retrieval.md` — header (intro paragraph)

| | |
|---|---|
| Text before | This section is the part the prototype&#x27;s demo and evaluation test, and it must reproduce them. |
| Text after | This section is the part of the design that the prototype&#x27;s demo and its evaluation put to the test, and it must reproduce them. |
| Steps made explicit | &#x27;test&#x27; is a verb here: the demo and the evaluation are what exercise this part; rewritten so &#x27;evaluation test&#x27; cannot be parsed as a noun; the obligation stays: this section must reproduce both |

### F13. `docs/design/enact/04_index_and_retrieval.md` — 1. The snapshot (counters)

| | |
|---|---|
| Text before | They rise on every read, which no snapshot version pins, and a ranking that depended on them could not be rerun (01 §3.1). |
| Text after | They rise on every read, and a read publishes no snapshot, so no snapshot version pins what a counter held at any moment; a ranking that depended on them could not be rerun (01 §3.1). |
| Steps made explicit | counters change on every read; a read publishes no snapshot — only a commit does; so no snapshot version records what a counter held at any moment; a ranking that used counters therefore could not be rerun from a receipt |

### F14. `docs/design/enact/04_index_and_retrieval.md` — 3.2 Candidates, and `any`

| | |
|---|---|
| Text before | So `any` is nameable exactly when it is the only live value its dimension has, and the procedure slot reads it the same way: `artifact=any` anchors a slot there and nowhere else. The two rules have to agree, or a query that is accepted would return no procedure. |
| Text after | So `any` is nameable exactly when it is the only live value its dimension has. The procedure slot reads it the same way: where `any` is the only live value, `artifact=any` counts as the single artifact value a slot needs; anywhere else it anchors no slot. The two rules have to agree: if a query could name `any` where the slot did not count it, a query that is accepted would return no procedure. |
| Steps made explicit | a query may name any exactly when it is the dimension&#x27;s only live value; the slot needs exactly one artifact value and one task value (§3.6); in that same only-live-value case, artifact=any counts as that single artifact value; anywhere else it anchors no slot; if the two rules disagreed, a query the read accepts could never fill its procedure slot |

### F15. `docs/design/enact/04_index_and_retrieval.md` — 3.8 Explanation, receipt, usage (`standing` row)

| | |
|---|---|
| Text before | reads them with `show` before advising. With no soft tags a rank is age (§3.5), so the newest preference is last and `max` cuts it: the one a reader is least likely to know already |
| Text after | reads them with `show` before advising. With no soft tags a rank is age, oldest first (§3.5); the newest preference is therefore ranked last, and `max` cuts the end of the list — so the preference cut is the newest, the one a reader is least likely to know already |
| Steps made explicit | with no soft tags, rank falls to §3.5&#x27;s tie-break — creation order, oldest first; the newest preference therefore sits last in the ranking; max cuts from the end of the list; so the preference that gets cut is the newest — exactly the one a reader is least likely to know, which is why standing lists it |

### F16. `docs/design/enact/04_index_and_retrieval.md` — 3.9 Folding a generalisation's instances

| | |
|---|---|
| Text before | After step 5, every candidate one of whose `generalised_by` is also a candidate *and accepted by a person* (overview §4.11) is taken out of the list that will be scored and named under that generalisation as `evidence` — its key and title, not its text. |
| Text after | After step 5, each candidate&#x27;s `generalised_by` is checked: when a generalisation in it is itself a candidate *and accepted by a person* (overview §4.11), the instance is taken out of the list that will be scored and is named under that generalisation as `evidence` — its key and title, not its text. |
| Steps made explicit | for each candidate, look at its generalised_by list; the fold triggers when a generalisation in that list is itself a candidate and is accepted by a person; then the instance leaves the list that will be scored; and is named under that generalisation as evidence — key and title, not text |

### F17. `docs/design/enact/05_scope_index.md` — header (the G5 paragraph)

| | |
|---|---|
| Text before | Read literally, that forbids a catalogue of every title, which is what #101 decided. |
| Text after | Read literally, that forbids a catalogue of every title — and a catalogue of every title is what #101 decided. |
| Steps made explicit | G5 read literally forbids a catalogue of every title; #101 decided exactly such a catalogue; the rewrite pins the relative clause to the catalogue, so the sentence cannot be read as #101 having decided the forbidding |

### F18. `docs/design/enact/05_scope_index.md` — 5. Where the catalogue arrives

| | |
|---|---|
| Text before | The session-start output measured 8,936 bytes against `SESSION_START_LIMIT = 8900` (`src/_pygim/_mcp/enact.py:46`). The standing cards are already trimmed to fit, and above about 13 KB the host moves the whole output to a file with a 2 KB preview (global #21). So the catalogue cannot be added at the session&#x27;s start. |
| Text after | The session-start output measured 8,936 bytes against `SESSION_START_LIMIT = 8900` (`src/_pygim/_mcp/enact.py:46`): the start is already full, and the standing cards are already trimmed to fit it. Nor can the output simply grow, because above about 13 KB the host moves the whole output to a file with a 2 KB preview (global #21). So the catalogue cannot be added at the session&#x27;s start. |
| Steps made explicit | the session-start output already exceeds its own limit, and the standing cards are already trimmed to fit it; growing the output is no way out either: above about 13 KB the host moves the whole output to a file with a 2 KB preview; the two ceilings together are why the catalogue cannot arrive at the session&#x27;s start |

## Every unpack of round 2, in full

### F19. `docs/design/enact/03_store.md` — 2.1 A row's id is the hash of the row

| | |
|---|---|
| Text before | Rows therefore form a chain, and a chain of hashes is tamper-evident and, more usefully here, **mergeable**: two copies of a repository that each grew their own rows can be joined, and the join has an identity of its own (§5). |
| Text after | Rows therefore form a chain. A chain of hashes is tamper-evident: a row&#x27;s id is the digest of its content, its parents&#x27; ids included, so editing any row changes its id, and the rows that name the old id no longer match anything. More usefully here, the chain is **mergeable**: because each row names its parents, two copies of a repository that each grew their own rows still form one graph when joined, and the join has an identity of its own (§5). |
| Steps made explicit | a row&#x27;s id is the digest of its content, parents&#x27; ids included; editing a row therefore changes its id; the children that named the old id then match nothing, which is what tamper-evident means; separately, each row names its parents, so two grown copies still form one graph when joined — the mechanism behind mergeable |

### F20. `docs/design/enact/03_store.md` — 3.3 Head views

| | |
|---|---|
| Text before | The reason it is committed is the diff. |
| Text after | The reason it is committed is the diff: a committed file&#x27;s changes show up where people already review, so a change to a memory is reviewed as a change to its page. |
| Steps made explicit | the views are committed to git; a committed file&#x27;s changes appear in the diff, which is where review already happens; so a change to a memory is reviewed as a change to its page — the mechanism &#x27;the diff&#x27; stood in for |

### F21. `docs/design/enact/03_store.md` — 3.4.1 Stores: what this machine holds

| | |
|---|---|
| Text before | It is built from an `Environment` where the program is wired, and it lives as long as the session does, which is exactly as long as its answer stays true. |
| Text after | It is built from an `Environment` where the program is wired, and it lives as long as the session does. The lifetime is chosen to match the answer: which stores this machine holds stays true for the length of a session, so a value found once may be held, and trusted, for exactly that long. |
| Steps made explicit | the object&#x27;s lifetime is the session&#x27;s; its answer — which stores this machine holds — stays true for the length of a session; so a value found once may be held and trusted for exactly the object&#x27;s lifetime, which is why the lifetime justifies the cache |

### F22. `docs/design/enact/03_store.md` — 5. When git merges two histories

| | |
|---|---|
| Text before | Its id is the digest of the sorted parents, so whoever merges first — and in whichever direction — produces the same row: merging A into B and B into A gives the same snapshot. |
| Text after | Its id is the digest of the sorted parents: the parents are put in one fixed order before hashing, so the direction of the merge never enters the digest. Whoever merges first — and in whichever direction — therefore produces the same row, and merging A into B and B into A gives the same snapshot. |
| Steps made explicit | the parents are put in one fixed order before hashing; the direction of the merge therefore never enters the digest; so either person, merging in either direction, produces the same row and the same snapshot |

### F23. `docs/design/enact/03_store.md` — 5. When git merges two histories (add wins)

| | |
|---|---|
| Text before | **add wins**: an unlink removes only the links it had seen; a link it had not seen survives |
| Text after | **add wins**: an unlink row removes only the links that exist in the history behind its own parents — the rows its writer had replayed; a link made concurrently in the other clone is not in that history, so it survives |
| Steps made explicit | what an unlink &#x27;had seen&#x27; is the history behind its row&#x27;s parents — the rows its writer had replayed; a concurrent link in the other clone is not in that history; so the unlink cannot name it, and it survives — the mechanism behind &#x27;add wins&#x27; |

### F24. `docs/design/enact/03_store.md` — 7. The service, and its strategies (leak table)

| | |
|---|---|
| Text before | fine for prose; a short, guessable secret can still be confirmed by hashing a guess, so the rule is that secrets do not belong in memories at all |
| Text after | fine for prose; a short, guessable secret can still be confirmed by hashing the guess and looking for that digest among the object names, so the rule is that secrets do not belong in memories at all |
| Steps made explicit | content objects are named by the digest of their plaintext; an attacker hashes a guessed secret; and looks for that digest among the object names — a match confirms the guess, which is the attack &#x27;confirmed by hashing a guess&#x27; compressed |

### F25. `docs/design/enact/03_store.md` — 9.1 Where the repository lives

| | |
|---|---|
| Text before | `git config` is what makes a store global to a project: git keeps it in the clone&#x27;s shared configuration, so one `oo enact setup` points every worktree at the same store. |
| Text after | `git config` is what makes a store global to a project: the setting lives in the clone&#x27;s configuration, which every worktree of that clone shares, so the path one `oo enact setup` writes is the path every worktree reads — one store for all of them. |
| Steps made explicit | the setting lives in the clone&#x27;s configuration; every worktree of that clone shares that one configuration; so the path setup writes once is the path every worktree reads — the mechanism behind &#x27;points every worktree at the same store&#x27; |

### F26. `docs/design/enact/03_store.md` — 7. The service, and its strategies (arrow legend)

| | |
|---|---|
| Text before | &#124; Arrow &#124; Relationship &#124; Read it as &#124; … four rows: realization, composition, aggregation, dependency … The same four, drawn: &#91;a demonstration classDiagram] … In the Mermaid source these are, in order, `&lt;&#124;..`, `*--`, `o--` and `..&gt;`; an interface is always written first in a realization, so it is drawn above what implements it. |
| Text after | The arrows follow &#91;the project&#x27;s relationship pattern](../plantuml_relationship_pattern.md). (§3.1.1&#x27;s cross-reference to the legend removed with it.) |
| Steps made explicit | removed a notation explainer the owner ruled assumed knowledge; the legend, the drawn example and the Mermaid-spelling sentence explained standard UML class-diagram arrows; the link to the project&#x27;s relationship-pattern doc stays |

### F27. `docs/design/enact/04_index_and_retrieval.md` — 1.1 Postings over every memory, masked by heads

| | |
|---|---|
| Text before | The masked form is also what makes a rerun cheap: the snapshot at an older head differs from today&#x27;s mostly in `heads`. |
| Text after | The masked form is also what makes a rerun cheap: a supersede or a retirement changes the `heads` set and leaves every posting list as it was, so the snapshot at an older head differs from today&#x27;s mostly in `heads`. |
| Steps made explicit | under masking, a supersede or retirement changes only the heads set; the posting lists stay as they were; so the difference between an older snapshot and today&#x27;s is mostly in heads, which is why a rerun is cheap |

### F28. `docs/design/enact/04_index_and_retrieval.md` — 3.2 Candidates, and `any`

| | |
|---|---|
| Text before | The two rules have to agree: if a query could name `any` where the slot did not count it, a query that is accepted would return no procedure. |
| Text after | The two rules have to agree. Suppose a query could name `any` in a dimension where the slot did not count it: the query would be accepted, the slot would find no single artifact value, and a query that is accepted would return no procedure. |
| Steps made explicit | suppose the query rule accepted any but the slot rule did not count it; the query is accepted; the slot finds no single artifact value, so it stays empty; an accepted query then returns no procedure — the contradiction that forces the rules to agree |

### F29. `docs/design/enact/04_index_and_retrieval.md` — 3.3 Scores — step 6

| | |
|---|---|
| Text before | For each candidate, the soft score is the sum of the weights of the dimensions its matched soft tags answer, in milli-units (01 §3). |
| Text after | For each candidate, first take the query&#x27;s soft tags that the candidate also carries — its matched soft tags. Each of those tags answers one dimension, and each dimension has a weight (01 §3). The candidate&#x27;s soft score is the sum of those dimensions&#x27; weights, in milli-units. |
| Steps made explicit | first identify the matched soft tags: the query&#x27;s soft tags the candidate also carries; each tag answers one dimension, and each dimension has a weight; the soft score is the sum of those weights — the measurement stated before the metric, per the owner&#x27;s model |

### F30. `docs/design/enact/04_index_and_retrieval.md` — 5. Rerunning a receipt

| | |
|---|---|
| Text before | the vocabulary version is frozen, and every number is an integer. |
| Text after | the vocabulary version is frozen, and every number is an integer, so the same arithmetic gives the same score on every machine. |
| Steps made explicit | every number in scoring is an integer; integer arithmetic gives the same result on every machine; so the rerun&#x27;s order cannot drift — the reason &#x27;every number is an integer&#x27; belonged in this list |

### F31. `docs/design/enact/05_scope_index.md` — Intro (G5)

| | |
|---|---|
| Text before | Read literally, that forbids a catalogue of every title — and a catalogue of every title is what #101 decided. |
| Text after | Read literally, that forbids a catalogue of every title: the catalogue is delivered into the context, and most of its titles lie outside any one request&#x27;s hard problem space — and a catalogue of every title is what #101 decided. |
| Steps made explicit | the catalogue is delivered into the context; most of its titles lie outside any one request&#x27;s hard problem space; G5 read literally therefore forbids it — the reconstruction the dash hid |

### F32. `docs/design/enact/05_scope_index.md` — Why it exists

| | |
|---|---|
| Text before | The discovery step lived inside each procedure, so it arrived only when the request&#x27;s words matched one. &quot;How does oo inventory work now?&quot; matched none. |
| Text after | The discovery step lived inside each procedure, and a procedure reaches a session only when the request&#x27;s words match its `asked` words. &quot;How does oo inventory work now?&quot; matched no procedure&#x27;s words, so no procedure — and with it, no discovery step — arrived. |
| Steps made explicit | the delivery mechanism made explicit: a procedure arrives when the request&#x27;s words match its asked words; this request matched no procedure&#x27;s words; so no procedure arrived, and the discovery step living inside them arrived nowhere |

### F33. `docs/design/enact/05_scope_index.md` — Scenario 1 (measured note)

| | |
|---|---|
| Text before | Titles rather than texts is measured too. That read, with a 1,500-token budget, returned four texts and dropped five. The titles of all nine take a few hundred bytes. |
| Text after | Titles rather than texts is measured too: the same read, with a 1,500-token budget, returned four texts in full and dropped the other five, while the titles of all nine together take a few hundred bytes. |
| Steps made explicit | what is measured: the same read delivered as texts against a 1,500-token budget; the result: four texts fit, five dropped; the comparison: all nine titles together take a few hundred bytes — the measurement behind the titles-over-texts choice, joined into one comparison |

### F34. `docs/design/enact/05_scope_index.md` — 3. How it is built (term keys)

| | |
|---|---|
| Text before | That is decided by count, so it is deterministic. |
| Text after | That is decided by counting the files each word hits and comparing the count to the fixed share, so two builds over the same file list drop the same words: it is deterministic. |
| Steps made explicit | the cut is made by counting the files each word hits; the count is compared to the fixed share; the same file list therefore always drops the same words, which is what deterministic meant |

### F35. `docs/design/enact/05_scope_index.md` — 5. Where the catalogue arrives

| | |
|---|---|
| Text before | Nor can the output simply grow, because above about 13 KB the host moves the whole output to a file with a 2 KB preview (global #21). So the catalogue cannot be added at the session&#x27;s start. |
| Text after | Nor can the output simply grow, because above about 13 KB the host moves the whole output to a file with a 2 KB preview (global #21): the session then sees the preview, and the catalogue itself sits unread in the file. So the catalogue cannot be added at the session&#x27;s start. |
| Steps made explicit | above about 13 KB the host moves the output to a file and shows a 2 KB preview; the session then sees only the preview; the catalogue itself sits unread in the file — the reason growing the output does not deliver it |

### F36. `docs/design/enact/04_index_and_retrieval.md` — §3.2.1 — resolved centrally by the session

| | |
|---|---|
| Text before | This is the seam §4.13 needs for anything learned |
| Text after | This is the seam the overview&#x27;s §4.13 needs for anything learned |
| Steps made explicit | the round-2 ambiguous row found no §4.13 in this file; the referent is the overview&#x27;s §4.13, Direction: the end state — verified against 00_overview.md; the cross-reference now names its document |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/design/enact/03_store.md` — 3.4 When a human edits a view (option table) | the no-unread-write check does not apply — but a supersede of the current head never needed it | either the check is inapplicable because a file edit carries no seen list, or it is waived because superseding the current head is inherently a response to what is there; the text gives no reason to make explicit |
| 1 | `docs/design/enact/03_store.md` — 3.5.2 A timestamp is not an order | A store created, given a pack and written to inside the same second could have its *first* taxonomy row win the tie for good — and then every open, for ever, appended another row claiming a change that had not happened. | why rows appended at later opens, carrying later times, never displace the first row as &#x27;latest&#x27; is not derivable from the text — either they keep losing the tie somehow, or the scan reads something other than their time; without that mechanism the &#x27;for good / for ever&#x27; chain cannot be unpacked |
| 1 | `docs/design/enact/03_store.md` — 6. Loading (ingestion) | The same no-look rule is open to `remember` with origin `seed`, for an agent seeding a store from documents it has not yet written anything from. | &#x27;the no-look rule&#x27; is named nowhere else in the design set (checked by grep): it may mean ingestion&#x27;s exemption from reading existing memories before writing, or the slug-and-digest reconciliation itself |
| 1 | `docs/design/enact/03_store.md` — 9.1 Where the repository lives | A consequence for sources (02 §5.2): a store outside the checkout cannot hold paths relative to itself, so an inventory path is relative to the project&#x27;s root. | the reason is unstated: either several worktrees mean no single checkout sits at a fixed offset from the store, or the store simply cannot know where the checkout is — the two readings unpack differently |
| 1 | `docs/design/enact/04_index_and_retrieval.md` — 3.4 A soft tag the memory does not carry | The miss is still recorded in the explanation (`soft_missed`), so the day an evaluation shows contradiction ties near the top, miss scoring is a change to step 6 alone. | &#x27;contradiction ties&#x27; could mean contradicting memories tying with silent ones (both score zero) or contradictions merely appearing tied near the top; and the &#x27;so&#x27; may claim the recording is what confines the change to step 6, or only that the data is already kept |
| 1 | `docs/design/enact/04_index_and_retrieval.md` — 3.7 The budget — step 9 | If the procedure alone exceeds the budget it is still the context, alone, with `over_budget` set — a procedure is never cut, and the caller learns that its budget is smaller than the domain&#x27;s way of working. | looks stale rather than ambiguous in itself: §3.2.1 (rules version 2, settled 2026-09-22) says an oversized procedure is now named, not placed, and the budget goes to the matches; the Laws row &#x27;Budget kept&#x27; repeats the old rule too. Which behaviour is current cannot be decided from this file, so the text is left as written |
| 2 | `docs/design/enact/04_index_and_retrieval.md` — 3.2.1 Which procedure is placed, and what the rules version is for | This is the seam §4.13 needs for anything learned: what a policy learns is state, and state that orders answers has to be versioned like the vocabulary it sits beside. | no §4.13 exists in this file (§4 is the worked example); either an overview-§4.13 reference missing its document name, or a stale number — unpacking would require choosing |
