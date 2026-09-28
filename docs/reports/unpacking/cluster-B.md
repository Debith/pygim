# Unpacking pass — cluster B

[The report](../2026-09-28-unpacking-pass.generated.html) · study reports (phase-1 report, its STROBE review, the two split studies) · 47 unpacked, 4 left as written, over two rounds.

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

### B1. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Summary (1)

| | |
|---|---|
| Text before | A run&#x27;s tags were scored by the share of the parts of pygim the real session touched that they reach through retrieval. |
| Text after | A run was scored in two steps: first, list the parts of pygim the real session touched while doing that request; then, find which of those parts the run&#x27;s tags reach through retrieval. The score is the share of the touched parts the tags reach. |
| Steps made explicit | list the parts of pygim the real session touched for the request; find the parts the run&#x27;s tags reach through retrieval; the score is the share of the touched parts that the tags reach |

### B2. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Summary (1)

| | |
|---|---|
| Text before | and the five strongest and weakest on 60 held-out requests once, at the end. |
| Text after | At the end, the five strongest and weakest of them — five systems in all — were run once on 60 held-out requests. |
| Steps made explicit | &#x27;the five strongest and weakest&#x27; could read as five plus five; the held-out table has five rows, so the total of five is stated; the elided subject and verb (the candidates were run) restored |

### B3. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Summary (1)

| | |
|---|---|
| Text before | The best system reaches **0.78 of them on held-out requests (0.72–0.88 across samples) against 0.37 for today&#x27;s vocabulary**, with a third of its size, and what carries the gain is each value&#x27;s words and the permission to list every candidate. |
| Text after | The best system reaches **0.78 of them on held-out requests (0.72–0.88 across samples) against 0.37 for today&#x27;s vocabulary**, at a third of the size of today&#x27;s vocabulary. Two things carry the gain: the words each value carries, and an instruction that permits the run to list every candidate tag that could fit. |
| Steps made explicit | &#x27;its size&#x27; resolved: the comparison is against today&#x27;s vocabulary&#x27;s size; the two causes of the gain are separated from the result they explain; &#x27;the permission to list every candidate&#x27; resolved: candidate means candidate tag, and the permission comes from the instruction |

### B4. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Summary (1)

| | |
|---|---|
| Text before | The strongest limitation: the answer key is built from what sessions touched, which contains their own misses, and the owner has not yet confirmed any part of it. |
| Text after | The strongest limitation is the answer key. It is built from what the real sessions touched, so the sessions&#x27; own misses are built into it: a part a session needed but never touched is absent from the key. And the owner has not yet confirmed any part of the key. |
| Steps made explicit | the key is derived from what the real sessions touched; therefore a session&#x27;s own miss becomes the key&#x27;s miss: a part needed but never touched is absent from the key; separately, the owner has confirmed no part of the key |

### B5. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Term table under the title

| | |
|---|---|
| Text before | Of the parts of pygim (components) the real session read or changed for a task, the share the run&#x27;s tags reach through the subjects&#x27; links. |
| Text after | First, list the parts of pygim (components) the real session read or changed for the task. Then, follow each subject tag the run chose through its links to the components it reaches. Observed recall is the share of the session&#x27;s components that the tags reach. |
| Steps made explicit | list the components the session read or changed; follow each chosen subject tag through its links to components; the metric is the share of the session&#x27;s components that are reached |

### B6. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Term table under the title

| | |
|---|---|
| Text before | Of the components the tags reach, the share the session touched. |
| Text after | First, list the components the run&#x27;s tags reach. Precision is the share of those reached components that the session actually touched. |
| Steps made explicit | the denominator is the set of components the tags reach; the numerator is those of them the session touched |

### B7. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Term table under the title

| | |
|---|---|
| Text before | The spread of a metric across three runs of one unchanged candidate: 0.03 for observed recall. |
| Text after | The spread of a metric across three runs of one unchanged candidate — variation the runs alone produce, so a smaller difference between candidates cannot be told from it: 0.03 for observed recall. |
| Steps made explicit | the spread is measured with the candidate held constant, so it is variation the runs alone produce; therefore a between-candidate difference smaller than it cannot be attributed to the candidate |

### B8. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Background (2)

| | |
|---|---|
| Text before | Sections 01–04 of `docs/design/task/` found that today&#x27;s vocabulary rarely lets an agent classify a request from its words (18 of 255 tasks), that an agent given only a vocabulary does not use it unasked, and drafted a first-round vocabulary of four facets with words for every value (section 04). |
| Text after | Sections 01–04 of `docs/design/task/` found that today&#x27;s vocabulary rarely lets an agent classify a request from its words: it could in 18 of 255 tasks. They also found that an agent given only a vocabulary does not use it unasked. Section 04 then drafted a first-round vocabulary of four facets, with words for every value. |
| Steps made explicit | finding one: classification from the request&#x27;s words alone was possible in 18 of 255 tasks; finding two: a vocabulary given without an instruction goes unused; action: section 04 drafted the four-facet vocabulary with words for every value |

### B9. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Objectives (3)

| | |
|---|---|
| Text before | From `plan.md`, locked 2026-09-27T13:32:17Z before any data was drawn: which first-round vocabulary and instruction make retrieval reach the parts of pygim a task needed, reliably and cheaply; and which of its parts cause that. |
| Text after | From `plan.md`, locked 2026-09-27T13:32:17Z before any data was drawn, two questions. First, which first-round vocabulary and instruction make retrieval reach the parts of pygim a task needed, reliably and cheaply. Second, which parts of that vocabulary and instruction cause the effect. |
| Steps made explicit | the objectives are two distinct questions; &#x27;its parts&#x27; resolved: parts of that vocabulary and instruction; &#x27;cause that&#x27; resolved: cause the reliable, cheap reach |

### B10. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Participants (6) and study size (10)

| | |
|---|---|
| Text before | pygim&#x27;s 205 September tasks (section 01), stratified by the activity section 01 tagged and by whether the first message had six words or fewer, dealt to 60 development and 60 held-out tasks with seed 20260927 (`sample.py`). |
| Text after | The frame is pygim&#x27;s 205 September tasks (section 01). Each task sits in a stratum given by two things: the activity section 01 tagged it with, and whether its first message had six words or fewer. From those strata, tasks were dealt to 60 development and 60 held-out tasks with seed 20260927 (`sample.py`). |
| Steps made explicit | the sampling frame is the 205 September tasks; each task&#x27;s stratum comes from two properties: its tagged activity, and whether its first message is six words or fewer; the two sets of 60 were dealt from those strata with the stated seed |

### B11. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Participants (6) and study size (10)

| | |
|---|---|
| Text before | Sixty a set was the plan&#x27;s choice, to keep one generation of candidates near $40; with 50 scored tasks, the paired intervals are about ±0.05 wide, so smaller differences in recall cannot be told from noise. |
| Text after | Sixty a set was the plan&#x27;s choice: it keeps one generation of candidates near $40. With 50 scored tasks, a paired interval is about ±0.05 wide, so a difference in recall smaller than that cannot be told from noise. |
| Steps made explicit | the set size was chosen for cost: one generation near $40; the size fixes the resolution: a paired interval is about ±0.05 wide with 50 scored tasks; therefore a difference smaller than ±0.05 is indistinguishable from noise |

### B12. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Variables (7), sources and measurement (8)

| | |
|---|---|
| Text before | **Components reached**: the tags&#x27; subjects through their `resolves` links (section 04), or `component=` tags directly for today&#x27;s vocabulary. |
| Text after | **Components reached**: for the draft vocabularies, take each subject tag the run chose and follow its `resolves` links (section 04) to components; for today&#x27;s vocabulary, the run&#x27;s `component=` tags name components directly. |
| Steps made explicit | for the draft vocabularies: take the subject tags and follow their resolves links to components; for today&#x27;s vocabulary: the component= tags already name components |

### B13. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Variables (7), sources and measurement (8)

| | |
|---|---|
| Text before | **The answer key** (`answer-key.jsonl`): *observed* — the files each real session read or changed between the task&#x27;s first message and the next task&#x27;s, from its transcript, mapped to components by the plan&#x27;s path table (`key_observed.py`); *expected* — for the 60 development tasks, what the problem space should have held, judged by six subagents from the request, the session&#x27;s files, the inventory, the documents and the memory titles (`expected/`), 549 additions in all. |
| Text after | **The answer key** (`answer-key.jsonl`) has two parts. The *observed* part: from each real session&#x27;s transcript, take the files the session read or changed between the task&#x27;s first message and the next task&#x27;s first message; then map each file to a component by the plan&#x27;s path table (`key_observed.py`). The *expected* part, for the 60 development tasks: six subagents judged what the problem space should have held, working from the request, the session&#x27;s files, the inventory, the documents and the memory titles (`expected/`); 549 additions in all. |
| Steps made explicit | the key has two parts, observed and expected; observed, step one: from the transcript, the files read or changed between one task&#x27;s first message and the next task&#x27;s first message; observed, step two: each file mapped to a component by the path table; expected, for the development tasks: six subagents judged what the problem space should have held, from the listed sources |

### B14. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Bias (9)

| | |
|---|---|
| Text before | The expected key was judged by agents of the same model family that tags &#124; may flatter candidates that think alike; the owner&#x27;s 20 confirmations are the check, still pending |
| Text after | The expected key was judged by agents from the same model family as the model that tags &#124; may flatter candidates that think like the judges; the check is the owner confirming 20 keys, still pending |
| Steps made explicit | the judges share a model family with the model whose tags are scored; so a candidate that thinks like the judges may score too well; the guard is the owner confirming 20 keys, which has not happened yet |

### B15. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Bias (9)

| | |
|---|---|
| Text before | The path table maps pre-rename paths (`docs/design/memory/`) to `docs`, not `enact` (named `memory` until the owner&#x27;s review) |
| Text after | The component `enact` was named `memory` until the owner&#x27;s review, and the path table maps its pre-rename paths (`docs/design/memory/`) to `docs`, not `enact` |
| Steps made explicit | the parenthetical&#x27;s referent made plain: it is enact that was named memory until the owner&#x27;s review; given that rename, the pre-rename paths belong to enact, yet the path table sends them to docs |

### B16. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Other analyses (17)

| | |
|---|---|
| Text before | **Links** (offline, on stored runs): removing `web_ui → cli` and `data_structures → utils` raised precision by 0.09–0.12 for −0.02 to −0.03 recall, on development and again on held-out, for all three drafts. |
| Text after | **Links** (scored offline on the stored runs, no new runs): removing `web_ui → cli` and `data_structures → utils` raised precision by 0.09–0.12 and lowered recall by 0.02 to 0.03, on development and again on held-out, for all three drafts. |
| Steps made explicit | &#x27;offline, on stored runs&#x27; means the stored runs were re-scored, with no new runs; the trade written as &#x27;raised X for −Y&#x27; made explicit: precision up 0.09–0.12, recall down 0.02–0.03 |

### B17. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Other analyses (17)

| | |
|---|---|
| Text before | two rebuilds that changed only the headings each lost ~0.03 (below significance, above the floor); later candidates were built on the original rendering. |
| Text after | two rebuilds that changed only the headings each lost ~0.03 — below what the paired intervals can call significant, but above the noise floor; later candidates were built on the original rendering. |
| Steps made explicit | &#x27;below significance&#x27; expanded: smaller than what the paired intervals can call significant; &#x27;above the floor&#x27; expanded: larger than the noise floor |

### B18. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Key results (18)

| | |
|---|---|
| Text before | B4&#x27;s confidence levels raised recall by 0.10; the same instruction without the levels but &quot;when unsure, give them all&quot; kept all of it. |
| Text after | B4&#x27;s confidence levels raised recall by 0.10. The same instruction with the levels removed and &quot;when unsure, give them all&quot; in their place kept all of that gain. |
| Steps made explicit | the levels raised recall by 0.10; the variant swaps the levels for the give-them-all sentence; &#x27;kept all of it&#x27; resolved: the variant kept the whole 0.10 gain |

### B19. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Key results (18)

| | |
|---|---|
| Text before | It is why the held-out set exists. |
| Text after | Catching exactly this kind of failure is why the held-out set exists. |
| Steps made explicit | &#x27;It&#x27; resolved: the failure of a development finding to replicate; the held-out set exists to catch such failures |

### B20. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Limitations (19)

| | |
|---|---|
| Text before | retrieval is simulated by component links only, so the activity, object and quality dimensions are measured only by *procedure hit* (exploratory) and not at all for memories |
| Text after | Retrieval is simulated by component links only, and only subject tags carry such links; so recall tests the subject dimension alone, the activity, object and quality dimensions are measured only by *procedure hit* (exploratory), and retrieval of memories is not measured at all. |
| Steps made explicit | the simulation reaches components only through links, and only subject tags carry links; therefore recall exercises the subject dimension alone; the other three dimensions are covered only by the exploratory procedure-hit measure; retrieval of memories is measured by nothing |

### B21. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Interpretation (20)

| | |
|---|---|
| Text before | The system to carry forward is small; whether to keep the briefs and confidence levels is a precision-for-size choice the owner makes. |
| Text after | The system to carry forward is small. Whether to keep the briefs and confidence levels is the owner&#x27;s choice, and it is a trade: keeping them buys precision and costs size. |
| Steps made explicit | &#x27;precision-for-size choice&#x27; unpacked into the trade it names: keeping briefs and levels buys precision and costs size; the decision belongs to the owner |

### B22. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Convergence

| | |
|---|---|
| Text before | **Not converged.** Phase 1 ends here because the plan uses the held-out set once. |
| Text after | **Not converged.** Phase 1 ends here anyway: the plan allows the held-out set one use, and that use is now spent. |
| Steps made explicit | the plan&#x27;s rule: the held-out set may be used once; that single use has happened; therefore the phase ends even though convergence was not reached |

### B23. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — For the owner

| | |
|---|---|
| Text before | **confirm**: every number here rests on it, and phase 2 re-scores against it for free |
| Text after | **confirm**: every number here rests on the key, and once it is confirmed, phase 2 re-scores the stored runs against it — no new runs, so no new cost |
| Steps made explicit | &#x27;it&#x27; resolved: the answer key; &#x27;for free&#x27; unpacked: re-scoring runs over the stored runs, so no new runs and no new cost |

### B24. `docs/design/task/studies/vocabulary-evolution/phase-1-strobe-review.md` — Phase 1 report, reviewed by STROBE

| | |
|---|---|
| Text before | **In one line:** 19 of 22 items reported, 3 partly; two of the partly were fixed in the report after this review (study size, descriptive data); the gap that could overturn the result — an answer key no one outside the study has confirmed — is named, not fixed. |
| Text after | **In one line:** at review, 19 of the 22 items were reported and 3 partly. Two of the three partly items — study size and descriptive data — were then fixed in the report, after this review; one remains partly. The gap that could overturn the result is named but not fixed: an answer key no one outside the study has confirmed. |
| Steps made explicit | at review time the verdicts were 19 reported and 3 partly, of 22; two of the three partly items were fixed after the review, so one remains partly; the decisive gap, the unconfirmed answer key, is named in the report but not fixed by it |

### B25. `docs/design/task/studies/2026-09-27-blind-split/results.md` — Summary (1)

| | |
|---|---|
| Text before | Unasked, 0 of 8 runs used the vocabulary. Asked, two independent runs chose the same domain in 8 of 8 requests and the same task in 5 of 8, and named the same gaps the September coders had named with the whole conversation in view. |
| Text after | Given the vocabulary with nothing asking for it, 0 of 8 runs used it. Asked to break the request down, two independent runs chose the same domain in 8 of 8 requests and the same task in 5 of 8. The gaps those runs named — what the vocabulary cannot express — were the same gaps the September coders had named, and the coders had the whole conversation in view where the runs had only the request. |
| Steps made explicit | &#x27;Unasked&#x27; resolved: the vocabulary was present but nothing asked for its use; &#x27;Asked&#x27; resolved: the arm whose instruction says to break the request down; &#x27;gaps&#x27; glossed: what the vocabulary cannot express; the comparison made explicit: the coders had the whole conversation, the runs only the request, and they named the same gaps |

### B26. `docs/design/task/studies/2026-09-27-blind-split/results.md` — Agreement (Q3)

| | |
|---|---|
| Text before | Per hard dimension, out of the requests where the dimension exists (component: the five pygim ones): |
| Text after | Agreement is counted per hard dimension, and only over the requests whose vocabulary has that dimension; for component those are the five pygim requests: |
| Steps made explicit | the counts are per dimension; a request enters a dimension&#x27;s count only if its store&#x27;s vocabulary has that dimension; for component that leaves the five pygim requests |

### B27. `docs/design/task/studies/2026-09-27-blind-split/results.md` — Key results (18)

| | |
|---|---|
| Text before | It found two tasks in E22, where the September coder had recorded one with an `also`. |
| Text after | It found two tasks in E22, where the September coder had recorded a single task and noted the second only as an `also`. |
| Steps made explicit | &#x27;one with an also&#x27; unpacked: the coder&#x27;s record holds one task; the second task appears in that record only as an also annotation |

### B28. `docs/design/task/studies/2026-09-27-blind-split/results.md` — Interpretation (20)

| | |
|---|---|
| Text before | Read with the facet proposal (activity, object, subject, quality): the blind runs filled the facets the vocabulary has — activity (`task`), object (`artifact`) — and, unasked, wrote down the one it lacks. The runs&#x27; summaries said what each task was about in plain words (&quot;the banner&quot;, &quot;memory usage&quot; on the path set, &quot;the ink on the document&quot;, &quot;particle effects&quot;), except F17&#x27;s, which could only name the file: its subject — configuration — is inside the file, which no run saw. The missing column is mostly subjects the vocabulary cannot name. |
| Text after | Read with the facet proposal (activity, object, subject, quality): the vocabulary already has dimensions for two of the facets — `task` is its activity, `artifact` its object — and the blind runs filled those. The facet the vocabulary lacks is the subject, and the runs wrote it down unasked: their summaries said what each task was about in plain words (&quot;the banner&quot;, &quot;memory usage&quot; on the path set, &quot;the ink on the document&quot;, &quot;particle effects&quot;). The exception is F17, whose summary could only name the file: the subject — configuration — is inside the file, which no run saw. The &quot;Missing, in the runs&#x27; words&quot; column above is mostly subjects the vocabulary cannot name. |
| Steps made explicit | the mapping made explicit: the vocabulary&#x27;s task dimension is the activity facet, its artifact dimension the object facet; &#x27;the one it lacks&#x27; resolved: the subject facet; how the runs wrote the subject down: their plain-word summaries; &#x27;the missing column&#x27; resolved: the table column titled Missing, in the runs&#x27; words |

### B29. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Summary (1)

| | |
|---|---|
| Text before | With the context, every run named what its task was about, and two runs agreed on the artifact in 5 of 8 requests against 1 of 8 without it. Every run still said it needed something, but the context changed what: without it, 7 of 8 requests could not be told apart from their subject; with it, 1 could not, and one more stayed split between two activities. |
| Text after | With the context, every run named what its task was about, and the two runs agreed on the artifact in 5 of 8 requests; without the context, they agreed in 1 of 8. Every run still said it needed something, but the context changed what kind of thing was missing: without it, in 7 of 8 requests the missing thing was the subject itself — what the task is about; with it, that was left in 1 request, and one more stayed split between two activities. |
| Steps made explicit | the two agreement figures attached to their conditions: 5 of 8 with the context, 1 of 8 without; &#x27;could not be told apart from their subject&#x27; unpacked by the study&#x27;s blocking definition: what the run lacked was the very thing the task is about, its subject; the with-context tail mapped out: one request still lacked its subject, and one more stayed split between two activities |

### B30. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Term table under the title

| | |
|---|---|
| Text before | what a run says it needs is what the task is about: which file, which server, what &quot;it&quot; is, where the comments are |
| Text after | what a run says it needs is the very thing the task is about — which file, which server, what &quot;it&quot; is, where the comments are — so the split cannot name its subject |
| Steps made explicit | the need and the task&#x27;s aboutness are the same thing; therefore the subject facet cannot be filled, which is what blocks the split |

### B31. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Q1 — does every run name a subject?

| | |
|---|---|
| Text before | Arm C named a subject in 14 of 16 runs (D22 twice &quot;unknown&quot;), 4 of those only as a guess from a name; arm D in 16 of 16. |
| Text after | Arm C named a subject in 14 of 16 runs; the two that did not are D22&#x27;s, both &quot;unknown&quot;. Of the 14, 4 named it only as a guess from a name. Arm D named a subject in 16 of 16 runs. |
| Steps made explicit | the parenthetical resolved: the two runs without a subject are D22&#x27;s, which both answered unknown; &#x27;4 of those&#x27; resolved: four of the fourteen were guesses from a name; the elided &#x27;arm D in 16 of 16&#x27; completed into a sentence |

### B32. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Q2 — what is still needed?

| | |
|---|---|
| Text before | Blocking: 7 of 8 requests in C; in D, 1 (D22), and F17&#x27;s activity. |
| Text after | The need blocks the split in 7 of 8 requests in arm C. In arm D it blocks in 1 request (D22), and in F17 it blocks only the choice of activity. |
| Steps made explicit | &#x27;Blocking:&#x27; expanded into what is being counted: requests where the need blocks the split; the arm D tail separated: one request fully blocked, and in F17 only the choice of activity |

### B33. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Key results (18)

| | |
|---|---|
| Text before | Work detail, which discovery finds once the task is known. And two things the context did not hold: |
| Text after | The first kind is work detail: it is needed only for doing the work, and discovery finds it once the task is known. The second kind is what the context did not hold, and it appeared twice: |
| Steps made explicit | kind one named as such: work detail, needed only for doing the work, found by discovery once the task is known; kind two named as such: things the given context did not hold; &#x27;two things&#x27; clarified as the two instances of the second kind, not a third kind |

### B34. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Limitations (19)

| | |
|---|---|
| Text before | Without the context, the facet instruction made the artifact less consistent than blind-split&#x27;s vocabulary-only instruction (1 against 4 of 8 the same between runs): naming the object in plain words first may pull the tag in different directions. Not in the protocol; not explained. |
| Text after | Without the context, the facet instruction made the artifact less consistent than blind-split&#x27;s vocabulary-only instruction had made it: the two runs chose the same artifact in 1 of 8 requests here, against 4 of 8 there. A possible cause: the instruction asks for the object in plain words first, and the plain words may pull the tag in different directions. This comparison was not in the protocol, and the effect is not explained. |
| Steps made explicit | the parenthetical numbers attached to their studies: same artifact in 1 of 8 here, 4 of 8 in blind-split; the causal clause marked as the possible cause it is; &#x27;Not in the protocol; not explained&#x27; expanded: the comparison was unplanned, and the effect remains unexplained |

### B35. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Interpretation (20)

| | |
|---|---|
| Text before | It is a classification of the request in its context, and the context has three sources, each covered by something that exists or is proposed: the conversation and open files (the host has them), the named file (read its head), and the project&#x27;s own knowledge (the procedures and the project map). The vocabulary alone supplies none of them |
| Text after | It is a classification of the request in its context, and the context has three sources. The conversation and the open files: the host already has them. The named file: reading its head supplies it. The project&#x27;s own knowledge: the procedures and the project map hold it. So each source is covered by something that exists or is proposed. The vocabulary alone supplies none of the three |
| Steps made explicit | each source paired explicitly with what covers it: the host holds the conversation and open files; reading its head supplies the named file; the procedures and the project map hold the project&#x27;s knowledge; the each-covered claim restated after the pairs it summarises; &#x27;none of them&#x27; resolved: none of the three sources |

## Every unpack of round 2, in full

### B36. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Summary (1)

| | |
|---|---|
| Text before | Two things carry the gain: the words each value carries, and an instruction that permits the run to list every candidate tag that could fit. |
| Text after | Two things carry the gain: the plain words the vocabulary lists for each value, which every run is shown, and an instruction that permits the run to list every candidate tag that could fit. |
| Steps made explicit | the words belong to the vocabulary, listed under each value; the run is shown them as part of the candidate system, which is how they can act on anything |

### B37. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Term table under the title (paired)

| | |
|---|---|
| Text before | Compared task by task between two candidates, with a 95% bootstrap interval over tasks. |
| Text after | For each task, take one candidate&#x27;s value and subtract the other candidate&#x27;s value on that same task; the interval is a 95% bootstrap interval over those per-task differences. |
| Steps made explicit | pair the two candidates&#x27; values on the same task; subtract to get a per-task difference; the bootstrap interval is over those differences, not over raw scores |

### B38. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Term table under the title (+ two weak links removed)

| | |
|---|---|
| Text before | The same runs scored without the links web_ui → cli and data_structures → utils. |
| Text after | The same stored runs, re-scored with the links web_ui → cli and data_structures → utils deleted: a web_ui tag then no longer identifies the component cli, and a data_structures tag no longer identifies utils. |
| Steps made explicit | no new runs: the stored runs are re-scored; removal means deleting the link from the vocabulary before following links; the mechanism: without the link, the tag no longer identifies that component |

### B39. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Bias (9)

| | |
|---|---|
| Text before | The observed key holds the sessions&#x27; own misses: a session that never opened `pathlike` makes a link to it look wrong |
| Text after | The observed key holds the sessions&#x27; own misses: a session that never opened `pathlike` leaves `pathlike` out of the key, so a tag whose link identifies `pathlike` is scored as identifying a component the session did not use — even when the link is right |
| Steps made explicit | the session never opened the component, so the key omits it; a tag&#x27;s link identifies that component; scoring compares identified components to the key, so the identification counts as wrong; hence a right link is penalised |

### B40. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Other analyses (17)

| | |
|---|---|
| Text before | two rebuilds that changed only the headings each lost ~0.03 |
| Text after | two rebuilds that changed only the headings each lowered observed recall by ~0.03 |
| Steps made explicit | named the metric the 0.03 is measured in (observed recall) before comparing it to the noise floor, which is stated in recall |

### B41. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Key results (18)

| | |
|---|---|
| Text before | **Much of the draft can go without loss** |
| Text after | **Much of the draft can be removed without losing recall** |
| Steps made explicit | said what “loss” is measured in — recall — since precision does move (key result 4 treats precision as a separate axis) |

### B42. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Interpretation (20)

| | |
|---|---|
| Text before | The draft&#x27;s structure — words for every value, and subjects linked to the components where they live — doubles what a first round reaches over today&#x27;s vocabulary, and most of its other parts are optional. |
| Text after | The draft&#x27;s structure — words for every value, and subject tags that carry links to the components that hold each subject — doubles the share of a session&#x27;s components that the first round&#x27;s tags identify, compared with today&#x27;s vocabulary, and most of its other parts are optional. |
| Steps made explicit | replaced the metaphor “components where they live” with the mechanism: subject tags carry links, and the links identify components; replaced “what a first round reaches” with the measurement: the share of the session&#x27;s components the tags identify |

### B43. `docs/design/task/studies/vocabulary-evolution/phase-1-strobe-review.md` — What the study needs, most important first

| | |
|---|---|
| Text before | 3. A test of real retrieval, where the activity, object and quality dimensions can be measured, not only subjects. |
| Text after | 3. A test of real retrieval. In this study only subject tags carry component links, so the scoring measures the subject dimension alone; real retrieval would let the activity, object and quality dimensions be measured too. |
| Steps made explicit | stated the mechanism the item leans on: only subject tags carry component links; hence the simulated scoring measures the subject dimension alone; hence real retrieval is what would let the other dimensions be measured |

### B44. `docs/design/task/studies/2026-09-27-blind-split/results.md` — Arm A — does it use the vocabulary unasked? (Q1)

| | |
|---|---|
| Text before | asked for the file: no tools, and `/` is not the checkout |
| Text after | asked for the file: with tools off and the session run from `/`, not the checkout, it could not read the file |
| Steps made explicit | the run had tools off (Methods) and its working directory was /, not the repository; so it could not read the named file; which is why it asked for it |

### B45. `docs/design/task/studies/2026-09-27-blind-split/results.md` — Key results (18)

| | |
|---|---|
| Text before | **The gaps are not an effect of hindsight.** Every gap the runs named was one section 01 had found with the conversation in view: no artifact for a source module, no version control, no task for operating a process, no software domain in the D&amp;D vocabulary, no graphics domain. |
| Text after | (same, plus:) The runs saw only the request, so finding these gaps does not depend on having the conversation. |
| Steps made explicit | made the inference explicit: the runs lacked the conversation, yet named the same gaps; so the gaps are findable from the request alone, not products of the coders&#x27; hindsight |

### B46. `docs/design/task/studies/2026-09-27-facet-split/results.md` — Q3, Q4 — agreement

| | |
|---|---|
| Text before | (table only, no prose; classes and denominators defined in blind-split, not here) |
| Text after | Agreement is counted as in blind-split: for each request and each dimension in the table, the set of values one side chose is compared with the set the other side chose — the same set, an overlap of at least one value, disjoint sets, or no value on one or both sides. Each arm has two runs, and each run is compared with the September coder separately, so the columns against Sep count 16 comparisons over the eight requests. Only the pygim vocabulary has a component dimension, so its row counts over the five pygim requests — 10 comparisons against Sep. |
| Steps made explicit | what is compared: the set of values each side chose, per request and dimension; how the comparison classes work: same, overlap, disjoint, empty; where 16 comes from: two runs per arm, each compared with Sep, over eight requests; where 5 and 10 come from: only the pygim vocabulary has a component dimension |

### B47. `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Background (2) — from the owner's comment "What does this mean?", after the pass

| | |
|---|---|
| Text before | Section 04 then drafted a first-round vocabulary of four facets, with words for every value. |
| Text after | Section 04 then drafted a vocabulary for the first round — the split an agent makes from the request and the vocabulary alone, before anything is retrieved. The draft answers four questions about a request, each question a facet with a fixed list of values: the activity asked for (task), the object it concerns (artifact), what it is about (subject), and the quality that matters (concern). And because a first round has only the request&#x27;s phrasing to go on, every value also lists the words a request uses when that value applies — &quot;fix, bug, broken&quot; for troubleshoot — so an agent chooses values from the phrasing instead of guessing from the value&#x27;s name. |
| Steps made explicit | the first round is the split an agent makes from the request and the vocabulary alone, before anything is retrieved; the four facets are named: task, artifact, subject, concern — each a question with a fixed list of values; the words are per value: what a request says when that value applies, checked against section 04&#x27;s Words column; why the words exist: the first round has only the request&#x27;s phrasing, so the words are what lets a value be chosen from it |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Bias (9) | Two batches of the expected key kept `docs` more strictly than the rest | stricter about admitting docs as an expected component, or stricter about retaining docs rows already added |
| 1 | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Key results (18) | that the object dimension disciplines subject choice | disciplines could mean it makes the chosen subjects more accurate, or that it makes runs choose fewer, tighter subjects |
| 2 | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Bias (9) | Two batches of the expected key kept `docs` more strictly than the rest | either the judging batches added the docs component less readily (a stricter bar for including it), or they held docs additions to a stricter definition; the mechanism of “kept more strictly” is not stated |
| 2 | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Key results (18) | that the object dimension disciplines subject choice | “disciplines” is a metaphor for an unstated mechanism: either naming an object constrains which subject tags are chosen, or its presence merely correlated with better subject precision on development |
