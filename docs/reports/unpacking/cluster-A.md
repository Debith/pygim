# Unpacking pass — cluster A

[The report](../2026-09-28-unpacking-pass.generated.html) · task docs (00, 02, 03, 04's prose, section 01's generator) · 31 unpacked, 5 left as written, over two rounds.

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

### A1. `docs/design/task/00_overview.md` — ## 3. Session start (step 12)

| | |
|---|---|
| Text before | it runs the inventory — the code behind `oo inventory` — over every Python file: what the code imports, joined with the modules the project ships and the packages installed where the command itself runs (`dnd` here; for another project, not necessarily its own). The map keeps two lines of the result: what the project ships and its own work leaves idle, and what it reaches for |
| Text after | it runs the inventory — the code behind `oo inventory` — over every Python file. The inventory reads what the code imports, and joins that against two lists: the modules the project ships, and the packages installed where the command itself runs (`dnd` here; for another project, not necessarily its own). The map keeps two lines of the result: one for the modules the project ships, marking those its own work never imports as idle, and one for the third-party packages the code reaches for |
| Steps made explicit | the inventory reads what every Python file imports; it joins the imports against the modules the project ships; and against the packages installed where the command runs, which for another project is not necessarily that project&#x27;s own environment; the map keeps one line for what the project ships, marking modules its own work never imports as idle; and one line for the third-party packages the code reaches for |

### A2. `docs/design/task/00_overview.md` — ## 3. Session start (step 14)

| | |
|---|---|
| Text before | it cuts the cards, never the map or the messages: first to 7,500 characters for the cards alone (`STANDING_BUDGET`), then, while the whole text is over 8,900 (`SESSION_START_LIMIT`), by what it is over — each time giving up fields one card at a time, in the order of §3.1&#x27;s levels |
| Text after | it cuts the cards, never the map or the messages. Two limits apply, one after the other: first it cuts the cards until they alone fit 7,500 characters (`STANDING_BUDGET`); then, while the whole text — the map, the messages and the cards together — is still over 8,900 (`SESSION_START_LIMIT`), it keeps cutting the cards by the amount the text is over. Each cut gives up fields one card at a time, in the order of §3.1&#x27;s levels |
| Steps made explicit | only the cards are ever cut, the map and the messages never; first limit: the cards alone must fit 7,500 characters (STANDING_BUDGET); second limit: while the whole text — map, messages and cards — is still over 8,900 (SESSION_START_LIMIT), cutting continues by the amount the text is over; each cut gives up fields one card at a time, in the order of the levels |

### A3. `docs/design/task/00_overview.md` — ### 3.1 What the agent receives from the hook

| | |
|---|---|
| Text before | Step 14&#x27;s levels, from fullest to barest. Each field is given up one card at a time, the longest<br>first, and only until the text fits, so the cards stop between two levels rather than falling to<br>the next; the sizes are the whole level. |
| Text after | Step 14&#x27;s levels, from fullest to barest. The cut moves field by field: a field is given up on<br>one card at a time, the longest card first, and the cutting stops the moment the text fits. So<br>when it stops partway through a field, some cards have given the field up and the rest still keep<br>it: the cards stop between two levels rather than all falling to the next. The sizes in the last<br>column are the whole level — what all 31 cards come to when every card keeps exactly that level&#x27;s<br>fields. |
| Steps made explicit | a field is given up on one card at a time, the longest card first; the cutting stops the moment the text fits; so it can stop partway through a field: some cards have given the field up, the rest keep it — the cards stop between two levels; the table&#x27;s sizes describe whole levels: all 31 cards with exactly that level&#x27;s fields |

### A4. `docs/design/task/00_overview.md` — ## 6. Where it failed this week (row 2026-09-26)

| | |
|---|---|
| Text before | there, and cut on the way: the cards&#x27; `when` level fits their own 7,500 characters, but with the map and four messages the whole came to 8,944, 44 over the limit, and the next level down was bare titles — 3,551 of the 8,900 characters went unused. |
| Text after | there, and cut on the way. At the `when` level the cards alone fit their own 7,500 characters; with the map and four messages the whole text came to 8,944 characters, 44 over the 8,900 limit. Before the fix a field was given up on every card at once, so those 44 characters pushed every card down a whole level, and the next level down was bare titles. That cut left 3,551 of the 8,900 characters unused. |
| Steps made explicit | at the when level the cards alone fit their own 7,500-character budget; the map and four messages brought the whole text to 8,944 characters, 44 over the 8,900 limit; before the fix a field was given up on every card at once, so a 44-character overrun pushed every card down a whole level; the next level down was bare titles; that cut left 3,551 of the 8,900 characters unused |

### A5. `docs/design/task/00_overview.md` — ## 7. What the sequence leaves undefined (first bullet)

| | |
|---|---|
| Text before | and results<br>  chosen for *this* task are a classification, which ENACT&#x27;s design gives to the agent (its 00<br>  §4.7). |
| Text after | and results<br>  chosen for *this* task first need the request classified, and ENACT&#x27;s design gives that<br>  classification to the agent, not to a fixed rule (its 00 §4.7). |
| Steps made explicit | choosing results for this particular task means classifying the request first; ENACT&#x27;s design gives that classification to the agent; so a fixed rule, such as the hook runs, cannot make the choice |

### A6. `docs/design/task/00_overview.md` — ## 6. Where it failed this week (row 2026-09-25)

| | |
|---|---|
| Text before | the moment did not resemble it: no procedure matched a question, and the reads were filtered |
| Text after | the moment did not resemble it: no procedure matched a question, and the reads the agent did make carried a term and tags the memories did not match, so the reads filtered them out |
| Steps made explicit | no procedure&#x27;s asked words match a question, so the request delivered nothing; the reads the agent made carried a term and tags; the relevant memories did not match them, so the reads filtered the memories out |

### A7. `docs/design/task/02_september_study_by_strobe.md` — front matter (term table, 'agreement')

| | |
|---|---|
| Text before | How often two coders, working apart, give the same value to the same unit. It is what shows whether a coding measures the unit or the coder. |
| Text after | How often two coders, working apart, give the same value to the same unit. High agreement means the value follows from the unit itself; low agreement means it follows from whoever coded it. That is why agreement is what shows whether a coding measures the unit or the coder. |
| Steps made explicit | high agreement: the value follows from the unit itself; low agreement: the value follows from whoever coded it; that is why agreement shows which of the two a coding measures |

### A8. `docs/design/task/02_september_study_by_strobe.md` — ## 4. What section 01 needs, most important first (point 1)

| | |
|---|---|
| Text before | Without it, a share such as &quot;86 tasks: several values fit&quot; measures the<br>   coder as much as the vocabulary (items 8, 12). |
| Text after | Without it, a share such as &quot;86 tasks: several values fit&quot; mixes two causes<br>   that cannot be told apart: how often the vocabulary truly offers several values, and how this<br>   one coder judged fits — the share measures the coder as much as the vocabulary (items 8, 12). |
| Steps made explicit | the share has two possible causes: the vocabulary truly offering several values, and the coder&#x27;s way of judging fits; with one coder per task nothing tells the two apart; so the share measures the coder as much as the vocabulary |

### A9. `docs/design/task/02_september_study_by_strobe.md` — ## 5. What this review leaves out

| | |
|---|---|
| Text before | items 12 and 16 assume estimates of effects,<br>which a descriptive census does not make, so their verdicts weigh the parts that apply. |
| Text after | items 12 and 16 assume a study that estimates<br>effects; a descriptive census makes no such estimates, so parts of those two items do not apply to<br>section 01, and their verdicts weigh only the parts that do. |
| Steps made explicit | items 12 and 16 are written for studies that estimate effects; a descriptive census makes no such estimates; so parts of those two items do not apply to section 01; their verdicts therefore weigh only the parts that do apply |

### A10. `docs/design/task/02_september_study_by_strobe.md` — ## 1. The 22 items (item 16, To add)

| | |
|---|---|
| Text before | whether a month is read as a sample of how requests look — if it is, an interval for each share |
| Text after | a statement of how the month is read: as a census, describing only itself, or as a sample of how requests look in general — and, read as a sample, an interval for each share, because each share is then an estimate |
| Steps made explicit | state which of two readings the month gets: a census that describes only itself, or a sample of how requests look in general; read as a sample, each share becomes an estimate; an estimate needs an interval |

### A11. `docs/design/task/03_strobe_kit.md` — ## 2. The vocabulary pack

| | |
|---|---|
| Text before | The checker has no scope, so it checked the pack beside pygim&#x27;s<br>vocabulary; the global store&#x27;s base is the same format. |
| Text after | The checker has no scope to point it at another store, so it checked the pack beside pygim&#x27;s<br>vocabulary rather than the global store&#x27;s; the check carries over, because the global store&#x27;s base<br>vocabulary is in the same format. |
| Steps made explicit | the checker takes no scope, so it cannot be pointed at the global store; it therefore checked the pack beside pygim&#x27;s vocabulary instead of the store the pack is meant for; the check still carries over, because the global store&#x27;s base vocabulary is in the same format |

### A12. `docs/design/task/03_strobe_kit.md` — ## 4. For the owner (row 'Section 01')

| | |
|---|---|
| Text before | **bring it up**, agreement first: every share in it depends on the coding |
| Text after | **bring it up**, agreement first: every share in it depends on the coding, and agreement is what shows whether the coding can be trusted |
| Steps made explicit | every share in section 01 is computed from the coding; agreement is what shows whether the coding can be trusted; so agreement must be measured before the shares can carry anything |

### A13. `docs/design/task/04_request_vocabulary.md` — ## 4. Confidence, carried with each tag

| | |
|---|---|
| Text before | In September, *several values fit* changed once the task was known in 101 of 151 cases; *from the<br>request&#x27;s words* in 114 of 312, most of them `task`, because the activity itself moved during the work. |
| Text after | In September, tags where *several values fit* changed once the task was known in 101 of 151 cases;<br>tags decided *from the request&#x27;s words* changed in 114 of 312. Most of those 114 were `task` tags:<br>the request&#x27;s words had decided the activity, and the activity itself then moved during the work. |
| Steps made explicit | in September, tags where several values fit changed once the task was known in 101 of 151 cases; tags decided by the request&#x27;s words changed in 114 of 312 cases; most of those 114 were task tags; they changed because the activity the words had decided itself moved during the work |

### A14. `docs/design/task/04_request_vocabulary.md` — ## 5. Open decisions (row 'subject: hard or soft')

| | |
|---|---|
| Text before | **soft** until memories carry it: as hard it would exclude every memory written before it |
| Text after | **soft** until memories carry it: a hard tag filters what a read returns, and no memory written before this dimension carries a `subject` tag, so as hard it would exclude every memory written before it |
| Steps made explicit | a hard tag filters what a read returns; no memory written before this dimension carries a subject tag; so making subject hard would exclude every memory written before it |

### A15. `__notes__/september_tasks/assemble.py` — string for '## 2. Which values the tasks used'

| | |
|---|---|
| Text before | At the request. A value no task used is either rare in this month&#x27;s work or not how requests are phrased. |
| Text after | At the request. A value no task used tells one of two things: the work it names was rare in this month, or the value&#x27;s words are not the words requests use. |
| Steps made explicit | a value with zero uses has two possible explanations; the work the value names was rare in this month; or the value&#x27;s words are not the words requests use |

## Every unpack of round 2, in full

### A16. `docs/design/task/00_overview.md` — ## Scenario — "How does oo inventory work now?" (table, report row)

| | |
|---|---|
| Text before | a read that returns #60, then its citation |
| Text after | a read that returns #60; #60 cites the report, and following that citation opens it |
| Steps made explicit | a read returns memory #60; #60 carries a citation naming the report; following the citation opens the report |

### A17. `docs/design/task/00_overview.md` — ## 5. What each part of the problem space depends on (memories row)

| | |
|---|---|
| Text before | a `read` with the right tags; one wrong tag or a `term` hides them silently |
| Text after | a `read` with the right tags. The tags filter: a memory that does not carry a tag the read asks for is left out of the answer, and so is one whose text does not contain the read&#x27;s `term` — and the answer does not say that anything was left out |
| Steps made explicit | a read&#x27;s tags act as filters; a memory lacking an asked-for tag is excluded from the answer; a memory whose text lacks the read&#x27;s term is excluded too; the answer does not report that anything was excluded |

### A18. `docs/design/task/00_overview.md` — ## 5. What each part of the problem space depends on (source documents row)

| | |
|---|---|
| Text before | through a citation, or `coverage` in a read&#x27;s answer |
| Text after | through a memory that cites it — the read returns the memory, and the memory&#x27;s citation names the document — or through `coverage` in a read&#x27;s answer |
| Steps made explicit | a read returns a memory; the memory&#x27;s citation names the document; the reader follows the citation to the document |

### A19. `docs/design/task/00_overview.md` — ## 7. What the sequence leaves undefined (Who names the problem space?)

| | |
|---|---|
| Text before | but nothing checks the tags it chose, and a narrow tag empties the answer<br>  without a word. |
| Text after | but nothing checks the tags it chose. Tags filter: a tag narrower than what<br>  the memories carry filters every one of them out, the read returns an empty answer, and<br>  nothing says why it is empty. |
| Steps made explicit | tags act as filters; a tag narrower than the memories&#x27; tags matches none of them; the read then returns an empty answer; nothing reports why the answer is empty |

### A20. `docs/design/task/00_overview.md` — ## 7. What the sequence leaves undefined (first bullet)

| | |
|---|---|
| Text before | and results<br>  chosen for *this* task first need the request classified, and ENACT&#x27;s design gives that<br>  classification to the agent, not to a fixed rule (its 00 §4.7). |
| Text after | and results<br>  chosen for *this* task first need the request classified; ENACT&#x27;s design gives that<br>  classification to the agent, not to a fixed rule (its 00 §4.7), and the hook is a fixed<br>  rule, so the hook cannot choose them. |
| Steps made explicit | choosing results for a task requires the request classified first; ENACT assigns classification to the agent, not to a fixed rule; the request hook is a fixed rule; therefore the hook cannot choose the results |

### A21. `docs/design/task/02_september_study_by_strobe.md` — ## 4. What section 01 needs (point 2, hindsight)

| | |
|---|---|
| Text before | Anything they carried back makes the request look easier to tag than it was, so the<br>     main finding — 18 of 255 decided by the words — is if anything too high. |
| Text after | Anything they carried back could make a tag look decided by the request&#x27;s words when<br>     it was in fact decided by what came later, so the request looks easier to tag than it was, and<br>     the main finding — 18 of 255 decided by the words — is if anything too high. |
| Steps made explicit | the coders saw later messages while tagging the first; knowledge carried back can make a tag look word-decided when later context decided it; that inflates the count of word-decided tags; so 18 of 255 is if anything too high |

### A22. `docs/design/task/02_september_study_by_strobe.md` — ## 4. What section 01 needs (point 2, parts cut by date)

| | |
|---|---|
| Text before | A task that crossed from one part to the next was split in two. It adds<br>     tasks; its size is unmeasured. |
| Text after | A task that crossed from one part to the next was split in two, so one<br>     task is counted as two. It adds tasks to the total; how many tasks were split this way is<br>     unmeasured. |
| Steps made explicit | a task crossing a part boundary was split into two tasks; one task is therefore counted as two; the task total is inflated; how many tasks this affected is unmeasured |

### A23. `docs/design/task/02_september_study_by_strobe.md` — ## 4. What section 01 needs (point 2, vocabulary by folder)

| | |
|---|---|
| Text before | 19 of the 43 D&amp;D tasks had no domain value because they were<br>     not about D&amp;D; it inflates *none* for that project. |
| Text after | Each task was tagged with the vocabulary of the project<br>     folder it was typed in. 19 of the 43 D&amp;D tasks were not about D&amp;D, so no domain value of the<br>     D&amp;D vocabulary fits them and they were counted as *none*; it inflates *none* for that project. |
| Steps made explicit | the vocabulary used for a task is chosen by the folder it was typed in; 19 of 43 D&amp;D-folder tasks were about something other than D&amp;D; no domain value of the D&amp;D vocabulary fits such a task; those tasks were counted as none, inflating none for that project |

### A24. `docs/design/task/03_strobe_kit.md` — front matter (intro)

| | |
|---|---|
| Text before | two procedures<br>and four templates, memorized in the machine&#x27;s global store because a study can be run in any<br>project |
| Text after | two procedures<br>and four templates, memorized in the machine&#x27;s global store — the store a session reads in every<br>project — because a study can be run in any project |
| Steps made explicit | the global store is read by sessions in every project on the machine; a study can be run in any project; so memories about studies go in the global store, where every project&#x27;s session finds them |

### A25. `docs/design/task/03_strobe_kit.md` — ## 2. The vocabulary pack (strobe row, What for)

| | |
|---|---|
| Text before | a memory about one item is found by the item — &quot;agreement between coders&quot; is `strobe=statistical_methods` |
| Text after | a memory about one item carries the item&#x27;s value as a tag, so a read that asks for the value returns it — &quot;agreement between coders&quot; is tagged `strobe=statistical_methods` |
| Steps made explicit | a memory about a STROBE item carries that item&#x27;s value as a tag; a read asking for the value returns the memories tagged with it; that is how a memory about one item is found by the item |

### A26. `docs/design/task/04_request_vocabulary.md` — ## 4. Confidence, carried with each tag

| | |
|---|---|
| Text before | In September, tags where *several values fit* changed once the task was known in 101 of 151 cases;<br>tags decided *from the request&#x27;s words* changed in 114 of 312. |
| Text after | Section 01 tagged each September task twice: once at the request, and once in hindsight, from all of<br>its messages; a tag *changed* when the hindsight tagging gave a different value than the tagging at<br>the request. Tags where *several values fit* changed in 101 of 151 cases; tags decided *from the<br>request&#x27;s words* changed in 114 of 312. |
| Steps made explicit | each task was tagged twice: at the request and in hindsight; a tag counts as changed when the two taggings differ; only then the counts: 101 of 151 for several-fit tags, 114 of 312 for word-decided tags |

### A27. `docs/design/task/04_request_vocabulary.md` — ## 2. Three kinds of dimension (subject row)

| | |
|---|---|
| Text before | what it is about; the runs wrote it into their summaries unasked, and it is what retrieval resolves |
| Text after | what it is about; the runs wrote it into their summaries unasked, and it is what retrieval resolves: each subject value&#x27;s `resolves` entry links it to components, and retrieval follows those links to the components where it looks |
| Steps made explicit | each subject value carries a resolves entry; the entry links the value to pygim components; retrieval follows those links to the components where it looks |

### A28. `docs/design/task/04_request_vocabulary.md` — ## 5. Open decisions (concern=testing beside subject=testing)

| | |
|---|---|
| Text before | **keep both, and let the independence measure decide**; the study will show whether they move together |
| Text after | **keep both, and let the independence measure decide**: the study will show whether they move together — whether a task&#x27;s value on one side predicts its value on the other. If they move together, one side is redundant, which decides for renaming it; if not, both stay |
| Steps made explicit | the independence measure compares the two dimensions&#x27; tags across the study&#x27;s tasks; moving together means one side&#x27;s value predicts the other&#x27;s; two that move together are redundant, which decides for renaming one side; independence decides for keeping both |

### A29. `docs/design/task/04_request_vocabulary.md` — ## 5. Open decisions (component=memory)

| | |
|---|---|
| Text before | **rename by retag and retire**, since names are persisted and ids are not (02 §1.3). |
| Text after | **rename by retag and retire**: a memory persists the tag&#x27;s name itself, never an id that a rename could redirect (02 §1.3), so the new name must be written onto each memory — the retag — and the old value retired. |
| Steps made explicit | a memory&#x27;s persisted tags hold the value&#x27;s name, not an id; so no single place exists where a rename could redirect every memory at once; renaming therefore means writing the new name onto each memory (the retag); and retiring the old value |

### A30. `docs/design/task/04_request_vocabulary.md` — ## 6. What comes next (step 3)

| | |
|---|---|
| Text before | **The four measures** — coverage, discrimination, agreement (κ ≥ 0.70) and independence (V &lt; 0.60) —<br>   with the confused pairs beneath each κ, as 02 §4.1 computes them. |
| Text after | **The four measures**, as 02 §4.1 computes them. Coverage: how many of the sample&#x27;s tasks<br>   receive at least one value of a dimension. Discrimination: the largest share any single value of<br>   the dimension takes among the tasks that have one. Agreement: whether the two blind passes give<br>   the same values to the same task, per value as Cohen&#x27;s κ (κ ≥ 0.70), with the confused pairs<br>   beneath each κ. Independence: whether two dimensions&#x27; values move together across the sample, as<br>   Cramér&#x27;s V (V &lt; 0.60). |
| Steps made explicit | each measure&#x27;s measurement stated before its metric name; coverage: tasks with at least one value of the dimension; discrimination: the largest share one value takes; agreement: the two blind passes compared per value, then named as Cohen&#x27;s κ; independence: co-movement of two dimensions&#x27; values, then named as Cramér&#x27;s V |

### A31. `__notes__/september_tasks/assemble.py` — string for '## 3. Where hindsight disagreed with the request'

| | |
|---|---|
| Text before | {len(changed)} of {len(tasks)} tasks turned out to be about something the request did not say. Each row is what the tags became once the whole task was known. |
| Text after | Every task was tagged twice: at the request, from its first message and the open file only, and in hindsight, from all of its messages. In {len(changed)} of {len(tasks)} tasks the hindsight tagging gave different tags than the request had: the task turned out to be about something the request did not say. Each row is what the tags became once the whole task was known. |
| Steps made explicit | each task was tagged twice: at the request and in hindsight; the comparison of the two taggings is what &#x27;turned out to be about something else&#x27; means; only then the count of tasks whose tags differ |

## Left as written

| Round | Where | Passage | Why it could not be unpacked safely |
|---|---|---|---|
| 1 | `docs/design/task/04_request_vocabulary.md` — ## 1. What is decided | Both tag sets are kept; the difference is the evidence the words need. | the difference between the two rounds&#x27; evidence for their tags, or between the two vocabulary renderings in how much entry text must back the words |
| 1 | `docs/design/task/04_request_vocabulary.md` — ## 7. What this leaves out | how the first round is delivered — the request hook or the session start — which the study&#x27;s size measure informs | a measure of the first-round text&#x27;s size deciding where it fits, or a &#x27;size&#x27; measure defined by the study elsewhere (02 §7.1) |
| 1 | `docs/design/task/04_request_vocabulary.md` — ## 5. Open decisions (row &#x27;component=memory&#x27;) | **rename by retag and retire**, since names are persisted and ids are not (02 §1.3) | names persisted in each memory&#x27;s tags (so every memory needs retagging), or persisted store-wide with no id layer through which a rename could happen in one place |
| 2 | `docs/design/task/03_strobe_kit.md` — ## 2. The vocabulary pack | the check carries over, because the global store&#x27;s base<br>vocabulary is in the same format | the checker verifies only format-level properties so any base in the format gives the same verdict, or this additive pack merely happens to pass against either base |
| 2 | `docs/design/task/04_request_vocabulary.md` — ## 1. What is decided | Both tag sets are kept; the difference is the evidence the words need. | still fails the round-2 test (logged in round 1): the difference between the rounds&#x27; evidence for their tags, or between the two renderings in how much entry text must back the words |
