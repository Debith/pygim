# Unpacking pass — cluster C

[The report](../2026-09-28-unpacking-pass.generated.html) · page generators (owner-review, progress, gaps, progress-2, evidence) · 38 unpacked, 5 left as written, over two rounds.

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

### C1. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; observed &#124; The tags the September session itself used when it read ENACT during the task, in the store&#x27;s vocabulary; — when it made no read. &#124; |
| Text after | &#124; observed &#124; The tags the September session itself used when it read ENACT during the task, in the store&#x27;s vocabulary. When the session made no read, the cell shows —. &#124; |
| Steps made explicit | the session may or may not have read ENACT during the task; when it read, the tags it used are shown, in the store&#x27;s vocabulary; when it made no read, the cell shows a dash |

### C2. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; proposed, request &#124; The key&#x27;s tags the request itself states: what the first round (step 1) can know. This, with the next column, is what you confirm. &#124; |
| Text after | &#124; proposed, request &#124; Of the key&#x27;s tags, those the request itself states — so the first round (step 1) can already know them. This column and the next are what you confirm. &#124; |
| Steps made explicit | the key gives the task a set of tags; this column shows the subset the request itself states; stated by the request means step 1 can already know them; the owner confirms this column and the next |

### C3. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; proposed, whole task &#124; Every tag the key gives the task in the draft vocabulary, knowing everything that happened: what the retag (step 2) and the work reveal too. (you) marks a tag you added. &#124; |
| Text after | &#124; proposed, whole task &#124; Every tag the key gives the task in the draft vocabulary. The key is written knowing everything that happened, so this also holds what the retag (step 2) and the work reveal. (you) marks a tag you added. &#124; |
| Steps made explicit | the key is written knowing everything that happened in the task; so beside what the request states, this column holds what the retag (step 2) and the work reveal; (you) marks a tag the owner added |

### C4. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; *italics* &#124; In any other column: a value the whole-task key does not have, where it gives the dimension at all. Some of the store&#x27;s names differ from the draft&#x27;s, so an italic there may be only a rename. &#124; |
| Text after | &#124; *italics* &#124; In any other column, italics mark a value the whole-task key does not have. They are used only where the key gives the dimension at least one value; where it gives none, nothing is marked. Some of the store&#x27;s names differ from the draft&#x27;s, so in a store-vocabulary column an italic may be only a rename. &#124; |
| Steps made explicit | italics mark a value the whole-task key does not have; the mark is applied only where the key gives that dimension at least one value; where it gives none, nothing is marked; the store columns use the store&#x27;s names, which sometimes differ from the draft&#x27;s, so an italic there may be only a rename |

### C5. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; Why &#124; The reason given: by the agent that proposed the tag, or by the key for a part of pygim or a reading. *re-keyed* marks a change by rule for your decision on explain, document and implement (`rekey_document.py`). &#124; |
| Text after | &#124; Why &#124; The reason given: by the agent that proposed the tag, or by the key for a part of pygim or a reading. *re-keyed* marks a tag changed by rule: your decision on explain, document and implement, applied across the key by `rekey_document.py`. &#124; |
| Steps made explicit | the owner made a decision on explain, document and implement; rekey_document.py applied that decision across the key as a rule; *re-keyed* marks a tag that rule changed |

### C6. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; request recall &#124; The share of the tags the request states (proposed, request) that a run gave: how much of what step 1 can know it found. Over both runs of the 60 development tasks. &#124; |
| Text after | &#124; request recall &#124; First take the tags the request states — the proposed, request column. Then count the share of them the run gave: that is how much of what step 1 can know it found. Over both runs of the 60 development tasks. &#124; |
| Steps made explicit | first identify the tags the request states — the proposed, request column; then count the share of those tags the run gave; that share says how much of what step 1 can know the run found; computed over both runs of the 60 development tasks |

### C7. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; whole-task recall &#124; The share of all the key&#x27;s tags that a run gave, from the request alone. &#124; |
| Text after | &#124; whole-task recall &#124; The share of all the key&#x27;s tags that a run gave. The run sees only the request, so here it is measured against tags the rest of the task revealed too. &#124; |
| Steps made explicit | the run sees only the request; the key&#x27;s tags include ones only the rest of the task revealed; the measure is the share of all those tags the run still gave |

### C8. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; tag precision &#124; The share of a run&#x27;s tags that the whole-task key has: a first-round tag the task turned out to need is not wrong. &#124; |
| Text after | &#124; tag precision &#124; The share of a run&#x27;s tags that the whole-task key has. The whole-task key includes tags only the task revealed, so a first-round tag the task turned out to need is in the key and counts as right, not wrong. &#124; |
| Steps made explicit | precision is the share of a run&#x27;s tags found in the whole-task key; the whole-task key includes tags only the task revealed; so a first-round tag the task turned out to need is in the key and counts as right |

### C9. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Vocabulary evolution — the answer key, for the owner to confirm (term table)

| | |
|---|---|
| Text before | &#124; reached by the runs &#124; The runs&#x27; subjects lead to it through their links: what retrieval would open. &#124; |
| Text after | &#124; reached by the runs &#124; The runs gave subject tags; each subject value links to parts of pygim; this part is one those links lead to. That is what retrieval would open. &#124; |
| Steps made explicit | the runs gave subject tags; each subject value links to parts of pygim; following those links from the runs&#x27; subjects reaches this part — what retrieval would open |

### C10. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Open questions from your comments

| | |
|---|---|
| Text before | Each is answered under its task; *open* waits for your decision, *phase 2* for a measure. |
| Text after | Each is answered under its task. A note marked *open* waits for your decision; one marked *phase 2* waits for phase 2 to measure it. |
| Steps made explicit | every open question is answered under its task; status *open* means it waits for the owner&#x27;s decision; status *phase 2* means it waits for phase 2 to measure it |

### C11. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — How far `I5-no-artifact` agrees with the key

| | |
|---|---|
| Text before | Over all 60 development tasks, both runs. Exploratory: the key is agents&#x27; hindsight, with your corrections. |
| Text after | Over all 60 development tasks, both runs. Read it as exploratory: the key was written by agents in hindsight, and your corrections amend it. |
| Steps made explicit | the key was written by agents; they wrote it in hindsight, knowing the whole task; the owner&#x27;s corrections amend it; that is why the agreement numbers are exploratory |

### C12. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Where it went, and where the runs' tags would have led

| | |
|---|---|
| Text before | *The store&#x27;s vocabulary asked for a component; the draft never does.* Tagged then, in its names — {said}. / None was tagged then. |
| Text after | *The store&#x27;s vocabulary asked for a component; the draft never does — the part is resolved from the inventory instead.* What each tagged at the time, in the store&#x27;s names — {said}. / None tagged a component at the time. |
| Steps made explicit | the store&#x27;s old vocabulary made the tagger name a component; the draft never asks for one: the part of pygim is resolved from the inventory; what follows is what each tagger named at the time, in the store&#x27;s names; the empty branch now says what was not tagged: a component |

### C13. `docs/design/task/studies/vocabulary-evolution/progress.py` — Vocabulary evolution — progress (term table)

| | |
|---|---|
| Text before | &#124; Observed recall &#124; Of the parts of pygim the real session touched for a task, the share the run&#x27;s tags reach through retrieval. Mean over the 60 development tasks, with a 95% interval from resampling tasks. &#124; |
| Text after | &#124; Observed recall &#124; First take the parts of pygim the real session touched for the task. Then take the parts the run&#x27;s tags reach through retrieval. Observed recall is the share of the touched parts that are reached. Mean over the 60 development tasks, with a 95% interval from resampling tasks. &#124; |
| Steps made explicit | first take the parts of pygim the real session touched for the task; then take the parts the run&#x27;s tags reach through retrieval; observed recall is the share of the touched parts that are reached; mean over the 60 development tasks, interval from resampling tasks |

### C14. `docs/design/task/studies/vocabulary-evolution/progress.py` — Vocabulary evolution — progress (term table)

| | |
|---|---|
| Text before | &#124; Paired change &#124; The candidate&#x27;s observed recall minus its control&#x27;s, task by task, with a 95% interval. An interval that includes 0 is no evidence of a change. &#124; |
| Text after | &#124; Paired change &#124; For each task, the candidate&#x27;s observed recall minus its control&#x27;s on that same task; the number shown is the mean of these per-task differences, with a 95% interval. An interval that includes 0 is no evidence of a change. &#124; |
| Steps made explicit | for each task, subtract the control&#x27;s observed recall from the candidate&#x27;s on that same task; the shown number is the mean of these per-task differences; an interval that includes 0 is no evidence of a change |

### C15. `docs/design/task/studies/vocabulary-evolution/progress.py` — Vocabulary evolution — progress (term table)

| | |
|---|---|
| Text before | &#124; Procedure hit &#124; Exploratory: the share of tasks whose chosen activity leads to the procedure the answer key names. &#124; |
| Text after | &#124; Procedure hit &#124; Exploratory. An activity value leads to a procedure, and the answer key can name a procedure for a task. The number is the share of tasks whose chosen activity leads to the procedure the key names. &#124; |
| Steps made explicit | an activity value leads to a procedure; the answer key can name a procedure for a task; a task counts when the activity the run chose leads to the procedure the key names; the number is the share of such tasks |

### C16. `docs/design/task/studies/vocabulary-evolution/progress.py` — Vocabulary evolution — progress (term table)

| | |
|---|---|
| Text before | &#124; recall / precision &#124; Request recall: the share of the tags the request states that a run gave — what the first round can know. Whole-task recall: the share of all the key&#x27;s tags. Precision: the share of the run&#x27;s tags the whole-task key has. Over both runs of the 60 development tasks. &#124; |
| Text after | &#124; recall / precision &#124; Request recall: the share of the tags the request states — what the first round can know — that a run gave. Whole-task recall: the share of all the key&#x27;s tags that a run gave. Precision: the share of the run&#x27;s tags the whole-task key has. Over both runs of the 60 development tasks. &#124; |
| Steps made explicit | whole-task recall was elliptical: &#x27;the share of all the key&#x27;s tags&#x27; left the reader to carry over &#x27;that a run gave&#x27;; the elided step is now stated for both recalls; precision unchanged |

### C17. `docs/design/task/studies/vocabulary-evolution/progress.py` — Tag agreement with the proposed tags (exploratory)

| | |
|---|---|
| Text before | Each run&#x27;s tags against the key: the tags `proposed/` gives each development task in the draft vocabulary, each stated by the request or found in the rest of the task, corrected by the owner&#x27;s answers (`owner-answers.json`). Not in the locked plan; the owner has confirmed 1 of the 20 tasks sent (`owner-review.md`). Candidates on another vocabulary (B1) are left out; — where a candidate does not offer the dimension. |
| Text after | Each run&#x27;s tags are scored against the key. The key is the tags `proposed/` gives each development task, in the draft vocabulary; each of its tags is either stated by the request or found in the rest of the task; the owner&#x27;s answers (`owner-answers.json`) correct it. This measure is not in the locked plan, and the owner has confirmed 1 of the 20 tasks sent (`owner-review.md`). Candidates on another vocabulary (B1) are left out. A — marks a dimension the candidate does not offer. |
| Steps made explicit | each run&#x27;s tags are scored against a key; the key is the tags proposed/ gives each development task, in the draft vocabulary; each key tag is either stated by the request or found in the rest of the task; the owner&#x27;s answers correct the key; the measure is outside the locked plan, and 1 of the 20 review tasks is confirmed; B1-vocabulary candidates are excluded, and a dash marks a dimension a candidate does not offer |

### C18. `docs/design/task/studies/vocabulary-evolution/report2.py` — Vocabulary evolution — phase 2 (term table)

| | |
|---|---|
| Text before | &#124; Arm &#124; One candidate first round (`plan-2.md`). Generation 1: P0 the new values alone; P1 with the task definitions; P2 with the session&#x27;s context; P3 both; P4 both and the artifact dimension. Generation 2: Q1 is P2 told to give the tags that fit, not every tag that could; Q5 is P2 told to give every tag the request&#x27;s words or its context point to, and none for what the work might need. &#124; |
| Text after | &#124; Arm &#124; One candidate first round (`plan-2.md`). Generation 1 starts from P0, the new values alone, and adds to it: P1 adds the task definitions; P2 adds the session&#x27;s context; P3 adds both; P4 adds both and the artifact dimension. Generation 2: Q1 is P2 told to give the tags that fit, not every tag that could; Q5 is P2 told to give every tag the request&#x27;s words or its context point to, and none for what the work might need. &#124; |
| Steps made explicit | generation 1 is additive on P0, the new values alone; P1 adds the task definitions, P2 the session&#x27;s context, P3 both, P4 both plus the artifact dimension; generation 2 (Q1, Q5) left as it was: already stated as changed instructions on P2 |

### C19. `docs/design/task/studies/vocabulary-evolution/report2.py` — Vocabulary evolution — phase 2 (term table)

| | |
|---|---|
| Text before | &#124; tag precision &#124; Of a run&#x27;s tags, the share the key has at all. Co-primary. &#124; |
| Text after | &#124; tag precision &#124; Of a run&#x27;s tags, the share the key has at all. Co-primary: one of the plan&#x27;s two primary measures, judged together with step-1 recall. &#124; |
| Steps made explicit | &#x27;co-primary&#x27; hid a relationship: the plan has two primary measures; they are step-1 recall and tag precision, judged together |

### C20. `docs/design/task/studies/vocabulary-evolution/report2.py` — Vocabulary evolution — phase 2 (term table)

| | |
|---|---|
| Text before | &#124; Against &#124; The arm it is judged against, and the pool: *pooled* is every dimension the arm offers, *core* task, subject and concern. &#124; |
| Text after | &#124; Against &#124; The arm it is judged against, and the pool of dimensions the comparison counts: *pooled* is every dimension the arm offers; *core* is task, subject and concern. &#124; |
| Steps made explicit | a comparison counts a pool of dimensions; *pooled* counts every dimension the arm offers; *core* counts task, subject and concern |

### C21. `docs/design/task/studies/vocabulary-evolution/report2.py` — Vocabulary evolution — phase 2 (term table)

| | |
|---|---|
| Text before | &#124; Step-1 change &#124; The arm&#x27;s step-1 recall minus its control&#x27;s, task by task, with a 95% interval. &#124; |
| Text after | &#124; Step-1 change &#124; For each task, the arm&#x27;s step-1 recall minus its control&#x27;s on that same task; the number shown is the mean of these per-task differences, with a 95% interval. &#124; |
| Steps made explicit | for each task, subtract the control&#x27;s step-1 recall from the arm&#x27;s on that same task; the shown number is the mean of these per-task differences, with a 95% interval |

### C22. `docs/design/task/studies/vocabulary-evolution/report2.py` — Vocabulary evolution — phase 2 (term table)

| | |
|---|---|
| Text before | &#124; By the plan &#124; Better when one primary rises beyond chance and the other does not fall; worse when either falls. &#124; |
| Text after | &#124; By the plan &#124; The two primaries are step-1 recall and tag precision. A primary rises beyond chance when its change interval lies wholly above 0, and falls when the interval lies wholly below 0. Better: one primary rises and the other does not fall. Worse: either primary falls. &#124; |
| Steps made explicit | the two primaries are step-1 recall and tag precision; rises beyond chance = the change&#x27;s 95% interval lies wholly above 0; falls = the interval lies wholly below 0; better: one primary rises and the other does not fall; worse: either falls |

### C23. `docs/design/task/studies/vocabulary-evolution/report2.py` — Vocabulary evolution — phase 2 (term table)

| | |
|---|---|
| Text before | &#124; given where the key has it &#124; Of the tasks whose key has the value, the share of runs that gave it. &#124; |
| Text after | &#124; given where the key has it &#124; Of the tasks whose key has the value, the share of their runs that gave it. &#124; |
| Steps made explicit | the denominator is the runs of the tasks whose key has the value, not the tasks themselves; &#x27;their runs&#x27; ties the counted runs to those tasks, matching the run counts the table shows |

### C24. `docs/design/task/studies/vocabulary-evolution/report2.py` — The key

| | |
|---|---|
| Text before | The owner&#x27;s 20 stand as he answered; the other 100 are the blind re-judge&#x27;s. On the owner&#x27;s 20 the re-judge gave 0.66–0.80 of the owner&#x27;s tags per dimension, and the owner kept 0.67–0.98 of its tags (`key_v2.py`): read every number below as agreement with a key that is itself about three-quarters right. |
| Text after | For the 20 tasks the owner reviewed, the key is his answers, unchanged; for the other 100, it is what the blind re-judge gave. The two can be compared on the owner&#x27;s 20: there the re-judge gave 0.66–0.80 of the owner&#x27;s tags per dimension, and of the tags the re-judge gave, the owner kept 0.67–0.98 (`key_v2.py`). So read every number below as agreement with a key that is itself about three-quarters right. |
| Steps made explicit | for the 20 owner-reviewed tasks the key is his answers, unchanged; for the other 100 tasks it is what the blind re-judge gave; the two sources can be compared on the owner&#x27;s 20; there the re-judge gave 0.66–0.80 of the owner&#x27;s tags per dimension, and of the re-judge&#x27;s tags the owner kept 0.67–0.98 (&#x27;its&#x27; resolved); hence every number below is agreement with a key itself about three-quarters right |

### C25. `docs/design/task/studies/vocabulary-evolution/report2.py` — Each arm against its control

| | |
|---|---|
| Text before | Noise: P0&#x27;s run 2 against its run 1 —  |
| Text after | Noise: P0&#x27;s run 2 against its run 1 — the same arm run twice, so the difference is run-to-run variation alone —  |
| Steps made explicit | P0&#x27;s run 2 is compared against its own run 1; the same arm run twice differs only in sampling; so this difference is the run-to-run noise floor for the comparisons above |

### C26. `docs/design/task/studies/vocabulary-evolution/evidence.py` — arm pages 'Every task' and task pages 'Every arm's answers' (tag legend, same string on both)

| | |
|---|---|
| Text before | Tags: **bold** in the step-1 key; plain in the key at a later basis; *italic* not in the key. |
| Text after | Tags: **bold**, in the key with a step-1 basis (stated or context); plain, in the key but at a later basis (retrieval or work); *italic*, not in the key at all. |
| Steps made explicit | bold: the key has the tag with a step-1 basis, that is stated or context; plain: the key has it, but at a later basis, that is retrieval or work; italic: the key does not have it at all |

### C27. `docs/design/task/studies/vocabulary-evolution/evidence.py` — Highlights: the rows behind the claims

| | |
|---|---|
| Text before | - **explain on a critique whose key has no explain:** {len(explain_runs)} of {2 * len(crit)} runs —  |
| Text after | - **explain on a critique whose key has no explain:** {len(explain_runs)} of the {2 * len(crit)} runs on such critique tasks gave explain —  |
| Steps made explicit | the denominator is the runs on tasks whose key has critique and no explain; the count is how many of those runs gave explain anyway |

### C28. `docs/design/task/studies/vocabulary-evolution/evidence.py` — Highlights: the rows behind the claims

| | |
|---|---|
| Text before | - **discover given where the key has none:** {len(disc_wrong)} of {len(disc)} runs that gave it —  |
| Text after | - **discover given where the key has none:** {len(disc_wrong)} of the {len(disc)} runs that gave discover were on tasks whose key does not have it —  |
| Steps made explicit | the denominator is the runs that gave discover; the count is how many of those runs were on tasks whose key does not have it |

### C29. `docs/design/task/studies/vocabulary-evolution/evidence.py` — Every task, the largest step-1 gain first (compare pages)

| | |
|---|---|
| Text before | ◆ marks the {len(marked)} tasks whose step-1 or precision change is {MARK:.2f} or more: the rows that move the mean most. |
| Text after | ◆ marks the {len(marked)} tasks whose step-1 or precision change is {MARK:.2f} or more in either direction: the rows that move the mean most. |
| Steps made explicit | a task is marked by the size of its change, not its sign; 0.25 or more in either direction — a fall of that size marks too, as the marking rule does; these are the rows that move the mean most |

### C30. `docs/design/task/studies/vocabulary-evolution/gaps.py` — The candidate values, most asked for first

| | |
|---|---|
| Text before | act on the review comments the owner left on a served page; today &#x27;comment on page&#x27; sits in subject=web_ui, which is about a page&#x27;s behaviour. Partly met since by task=document (option B, 2026-09-27): the work now has a value, the comments as its input still have none |
| Text after | act on the review comments the owner left on a served page; today &#x27;comment on page&#x27; sits in subject=web_ui, and that value is about a page&#x27;s behaviour, not about acting on comments. Partly met since by task=document (option B, 2026-09-27): the work now has a value; the comments as its input still have none |
| Steps made explicit | the need: a value for acting on the owner&#x27;s review comments on a served page; today &#x27;comment on page&#x27; sits in subject=web_ui, a value about a page&#x27;s behaviour — not about acting on comments; the owner&#x27;s option B decision (task=document) since gave the work itself a value; the comments as the work&#x27;s input still have no value |

## Every unpack of round 2, in full

### C31. `docs/design/task/studies/vocabulary-evolution/owner_review.py` — Where it went, and where the runs' tags would have led (inline legend)

| | |
|---|---|
| Text before | *reached by the runs*, what retrieval would open from the runs&#x27; tags. |
| Text after | *reached by the runs*, the parts the runs&#x27; subject tags identify: each subject value links to parts of pygim, and retrieval would open the parts those links name. |
| Steps made explicit | rule 1: the chain tags -&gt; links -&gt; parts was compressed to its endpoints (&#x27;what retrieval would open from the runs&#x27; tags&#x27;); the runs gave subject tags; each subject value links to parts of pygim; the links name the parts, and those are what retrieval would open; now matches the mechanism the &#x27;reached by the runs&#x27; hover cell already states |

### C32. `docs/design/task/studies/vocabulary-evolution/progress.py` — Vocabulary evolution — progress (term table)

| | |
|---|---|
| Text before | &#124; Procedure hit &#124; Exploratory. An activity value leads to a procedure, and the answer key can name a procedure for a task. The number is the share of tasks whose chosen activity leads to the procedure the key names. &#124; |
| Text after | &#124; Procedure hit &#124; Exploratory. Each global procedure names the activity it is for, and that pairing ties an activity value to a procedure. The answer key can also name a procedure for a task. The number is the share of tasks whose chosen activity is tied to the procedure the key names. &#124; |
| Steps made explicit | rule 1: &#x27;leads to&#x27; stated only the endpoints of the activity-to-procedure relationship; the mechanism, from the scoring code (evolve.py PROCEDURE, &#x27;by the global procedures&#x27; own tasks&#x27;): each global procedure names the activity it is for; that naming pairs an activity value with one procedure; a task counts when the run&#x27;s chosen activity is paired with the procedure the key names |

### C33. `docs/design/task/studies/vocabulary-evolution/progress.py` — Vocabulary evolution — progress (term table)

| | |
|---|---|
| Text before | &#124; κ subject &#124; Agreement between two independent runs on the subject tags, beyond chance. &#124; |
| Text after | &#124; κ subject &#124; The same request is run twice, independently. For each subject value, compare whether the two runs agree on giving it or leaving it out, and correct that agreement for the agreement chance alone would produce; κ subject is the mean of this over the subject values. &#124; |
| Steps made explicit | rule 2: the measurement now precedes the metric&#x27;s name; the same request is run twice, independently; per subject value, agreement is whether the two runs both give it or both leave it out; that agreement is corrected for what chance alone would produce (the kappa correction, per evolve.py _kappa); the reported number is the mean over the subject values |

### C34. `docs/design/task/studies/vocabulary-evolution/report2.py` — The key

| | |
|---|---|
| Text before | The two can be compared on the owner&#x27;s 20: there the re-judge gave 0.66–0.80 of the owner&#x27;s tags per dimension, and of the tags the re-judge gave, the owner kept 0.67–0.98 (`key_v2.py`). |
| Text after | The two can be compared on the owner&#x27;s 20. First take the tags the owner&#x27;s answers give those tasks: per dimension, the re-judge independently gave 0.66–0.80 of them. Then take the tags the re-judge gave: per dimension, the owner kept 0.67–0.98 of them (`key_v2.py`). |
| Steps made explicit | rule 2: each share now says its base set and direction before its number; first base set: the tags the owner&#x27;s answers give the 20 tasks; the re-judge independently gave 0.66–0.80 of them, per dimension; second base set: the tags the re-judge gave; the owner kept 0.67–0.98 of them, per dimension; the owner&#x27;s word &#x27;kept&#x27; preserved |

### C35. `docs/design/task/studies/vocabulary-evolution/report2.py` — Vocabulary evolution — phase 2 (term table, step-1 recall — second sentence only; first sentence is the owner's protected wording, untouched)

| | |
|---|---|
| Text before | Which tags could is the key&#x27;s call: those whose basis is stated or context. |
| Text after | The key decides which tags could have been assigned: those whose basis is stated or context. |
| Steps made explicit | rule 3 / clarity of logic: the garden-path clause &#x27;Which tags could is the key&#x27;s call&#x27; made the reader reconstruct that the key defines the eligible set; stated directly: the key decides which tags could have been assigned; the eligible set is unchanged: tags whose basis is stated or context; the identical cell in evidence.py (evidence/index.md) was changed to the same words, keeping the two pages word-for-word identical |

### C36. `docs/design/task/studies/vocabulary-evolution/evidence.py` — Phase 2 — the evidence (term table, When knowable)

| | |
|---|---|
| Text before | &#124; When knowable &#124; stated: the request&#x27;s words say it; context: the context the session had does; retrieval: the first read&#x27;s results do; work: only doing the task did. &#124; |
| Text after | &#124; When knowable &#124; stated: the request&#x27;s words say it; context: the context the session had says it; retrieval: the results of the first read say it; work: only doing the task revealed it. &#124; |
| Steps made explicit | the elliptical verbs &#x27;does&#x27;, &#x27;do&#x27;, &#x27;did&#x27; each made the reader carry &#x27;say it&#x27; forward from the first clause; each basis now states its own predicate; &#x27;work&#x27; says what only doing the task did: revealed it |

### C37. `docs/design/task/studies/vocabulary-evolution/report2.py` — The key (and gaps.py 'Found by the re-judge', evidence.py key page) — resolved centrally by the session

| | |
|---|---|
| Text before | for the other 100, it is what the blind re-judge gave. |
| Text after | for the other 100, it is what the blind re-judge gave — blind meaning each judge saw only the request and the context its session had, never the owner&#x27;s answers and never a run&#x27;s output (rejudge/instructions.md). |
| Steps made explicit | the round-2 ambiguous row asked: blind to what; the re-judge instructions record the inputs: each judge saw the request and its session&#x27;s context only; so blind means never the owner&#x27;s answers and never a run&#x27;s output; one wording applied at all three pages that introduce the term |

### C38. `docs/design/task/studies/vocabulary-evolution/evidence.py` — Tag dimension term row (and owner_review.py, the same unified cell) — resolved centrally by the session

| | |
|---|---|
| Text before | The part of pygim is not among them: it is resolved from the inventory, never tagged. |
| Text after | The part of pygim is not among them: each subject value carries links to the components that hold it, and following those links against the inventory names the part — so no run tags a component directly. |
| Steps made explicit | each subject value carries links to the components that hold it; following those links against the inventory names the part; so a component is never a tag a run gives; applied identically to both pages that define the unified term |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/design/task/studies/vocabulary-evolution/evidence.py` — The owner&#x27;s 20 against the blind re-judge (key.md) | The draft&#x27;s evidence lists named some of these tasks; `key_v2.py` prints the agreement with and without them. | either a contamination guard (the re-judge could have seen those tasks named in the draft&#x27;s evidence lists, so agreement is shown with and without them) or plain bookkeeping of an overlap; the page says neither which, nor what the draft&#x27;s evidence lists are |
| 1 | `docs/design/task/studies/vocabulary-evolution/gaps.py` — The candidate values, most asked for first | a need that holds in every project, or a field outside pygim (D&amp;D), which the four first-round dimensions cannot say | &#x27;the four first-round dimensions&#x27; is never named on the page: the draft&#x27;s dimensions minus domain, or one candidate&#x27;s first round; naming them would be an assumption |
| 2 | `docs/design/task/studies/vocabulary-evolution/gaps.py` — Found by the re-judge (phase 2&#x27;s key) | The twelve agents that re-judged all 120 tasks blind recorded {len(rejudged)} gaps; not grouped yet. | &#x27;blind&#x27; names no object: blind to the owner&#x27;s answers, to phase 1&#x27;s proposed tags, or to the runs&#x27; tags; the same unexplained &#x27;blind re-judge&#x27; recurs in report2.py and evidence.py, so one central wording should resolve it |
| 2 | `docs/design/task/studies/vocabulary-evolution/evidence.py` — The owner&#x27;s 20 against the blind re-judge (key.md) | The draft&#x27;s evidence lists named some of these tasks; `key_v2.py` prints the agreement with and without them. | still open from round 1: either a contamination guard (the re-judge could have seen those tasks named in the draft&#x27;s evidence lists) or plain bookkeeping; unpacking would pick one |
| 2 | `docs/design/task/studies/vocabulary-evolution/owner_review.py` — term table, Tag dimension (unified hover term — also in evidence.py; Meaning cell left untouched per the round 2 guardrail) | The part of pygim is not among them: it is resolved from the inventory, never tagged. | rule 1 would want the mechanism of &#x27;resolved from the inventory&#x27; (resolved from what input, via what mapping — the files a memory cites, or something else); the cell is unified site-wide, so the session must apply one text centrally |
