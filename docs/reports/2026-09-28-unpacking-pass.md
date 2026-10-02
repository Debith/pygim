# The conceptual unpacking pass, 2026-09-28

The owner asked for every document to be reviewed for conceptual compression: sentences that pack several
steps, assumptions or pieces of context into one phrase, so a reader must reconstruct the meaning. Eight
agents edited the documents in place under shared rules (`__notes__/unpacking/instructions.md`): meaning
preserved exactly, nothing new added, every substantial unpack logged verbatim. This report is generated
from those logs (`__notes__/unpacking/findings_*.jsonl`, the pass's record — its edits share their files and
their commit with other changes, so git cannot isolate the pass); every count below is recountable from them.

The pass ran twice. After reading round 1 the owner refined the rules, and round 2 re-read every file
under them: a mechanism is stated, never only its endpoints — the tags have links, and the links identify
the components, not "the components the tags reach"; a measurement is explained — what is compared, and
how — before its metric is named; an explicit relationship is never replaced by a statement of intent; and
standard notation the reader can be assumed to know is not explained.

Three rows logged as left as written in round 2 were resolved afterwards by the session against the record —
what the re-judge was blind to, the Tag dimension cell's mechanism, and a cross-reference completed with its
document — and appear as the last unpacks of clusters C and F.

**312 passages unpacked and 39 left as written, across 34 files.** Every generated
page was rebuilt from its edited generator, and the whole site re-rendered: 215 pages, no broken markup, no
dead links, every table header on the unlocked study pages hovered. One incident during compilation: the
step that unified shared hover definitions overwrote eleven generated-page header rows across its two
rounds; the render checks' match counts and the lost-term audit caught them, and every header was
restored from the generators' last good output.

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

## The clusters

| Pass cluster | Documents | Unpacked, round 1 | Unpacked, round 2 | Left as written | Every row |
|---|---|---|---|---|---|
| A | task docs (00, 02, 03, 04's prose, section 01's generator) | 15 | 16 | 5 | [cluster A](unpacking/cluster-A.md) |
| B | study reports (phase-1 report, its STROBE review, the two split studies) | 35 | 12 | 4 | [cluster B](unpacking/cluster-B.md) |
| C | page generators (owner-review, progress, gaps, progress-2, evidence) | 30 | 8 | 5 | [cluster C](unpacking/cluster-C.md) |
| D | ENACT design, sections 00 and 00a | 28 | 8 | 4 | [cluster D](unpacking/cluster-D.md) |
| E | ENACT design, sections 01, 01a and 02 | 16 | 15 | 4 | [cluster E](unpacking/cluster-E.md) |
| F | ENACT design, sections 03, 04 and 05 | 18 | 18 | 7 | [cluster F](unpacking/cluster-F.md) |
| G | user-facing docs (enact guide, definition of done, releasing, examples, defects report) | 32 | 9 | 6 | [cluster G](unpacking/cluster-G.md) |
| H | design docs (persistence, pathlike, mapping, C++ linking, PlantUML, pathset) | 40 | 12 | 4 | [cluster H](unpacking/cluster-H.md) |

## The steps most often made explicit

Read across the logs, the compression took a few recurring shapes; each example links to the full
before/after row.

- **A measure hiding its procedure**: first name the set, then what is counted in it — the owner's own
  example; applied to observed recall, step-1 recall, precision and the study's shares ([B](unpacking/cluster-B.md), [C](unpacking/cluster-C.md)).
- **A failure chain compressed to its ends**: digest collision → silently dropped write; GCC-version symbols → load failure;
  shared temp name → why the commit lock ([E](unpacking/cluster-E.md), [H](unpacking/cluster-H.md), [F](unpacking/cluster-F.md)).
- **A mechanism referenced by its conclusion**: the fold happening “before ranking and the budget”, the head check as
  one comparison, the `standing` field's four-step reason ([D](unpacking/cluster-D.md), [F](unpacking/cluster-F.md)).
- **A counterfactual left implicit**: one template instantiation per backend *against what the alternative would cost*;
  what a truncation silently dropped ([H](unpacking/cluster-H.md), [G](unpacking/cluster-G.md)).
- **Two confounded causes in one share**: a number reading on the coder as much as the vocabulary ([A](unpacking/cluster-A.md), [B](unpacking/cluster-B.md)).
- **Round 2's three shapes**: a mechanism stated instead of its endpoints — the add-wins “seen”, the interposition fix,
  the silent tag filter; a measurement explained before its metric's name — the soft score, κ subject, the study's four
  measures; and notation explainers removed as assumed knowledge ([F](unpacking/cluster-F.md), [H](unpacking/cluster-H.md), [A](unpacking/cluster-A.md)).

## Candidate defects the pass exposed

Logged as ambiguous because unpacking them would mean choosing a side; they read as findings about the
content, and the pass changed nothing at them. On 2026-09-28 the owner ruled on each, one by one in chat:
ten are fixed in the sources, one is parked.

| Pass cluster | Candidate defect | Outcome, 2026-09-28 |
|---|---|---|
| [D](unpacking/cluster-D.md) | ENACT 00a closes with “Three doors in for a memory” while its state diagram draws four entry arrows | parked by the owner — still open |
| [D](unpacking/cluster-D.md) | ENACT 00a's closing sentence ends mid-clause — “the state of the index at any past moment is the log” — its completion is missing from the file | fixed — completed from G10's own words: the log replayed to that moment |
| [E](unpacking/cluster-E.md) | ENACT 01 §7 lists `snapshot_version` in the receipt where §3.1 pins `snapshot_id` | fixed — §7 pins `snapshot_id`, with `snapshot_version` beside it as the readable “v55” (owner's choice) |
| [E](unpacking/cluster-E.md) | ENACT 01a's intro says “the same four” actors over a list of five | fixed — four actors, the Index introduced beside them as the snapshot the service reads (owner's choice) |
| [E](unpacking/cluster-E.md) | ENACT 02's appendix gives `kind` seven values where 01 §2 and 01a's counts say six | fixed — `question` is the seventh value; 00's glossary, the seed table and 01 §2 carry it, and the totals moved 53 → 54 |
| [F](unpacking/cluster-F.md) | ENACT 03 §3.5.2: why a lost taxonomy-row tie makes every later open append rows is not derivable from the text | fixed — the mechanism, derived from the fixing commit (08eff76), is now written into §3.5.2 |
| [F](unpacking/cluster-F.md) | ENACT 03 §6 cites “the no-look rule”, which no document defines | fixed — 03 §6 states the exemption in full instead of the coined name: ingestion and origin `seed` skip the no-unread-write check, nothing else does |
| [F](unpacking/cluster-F.md) | ENACT 04 §3.7 and the “Budget kept” law still return an oversized procedure whole, while §3.2.1 (rules v2, 2026-09-22) names it instead | fixed — §3.7 and the “Budget kept” law now state rules v2, which is what the code ships (snapshot.h) |
| [G](unpacking/cluster-G.md) | examples/README says the extensions ship no type stubs; definition_of_done says stubs exist and `pygim stubs` keeps them current | fixed — the README names which public modules carry stubs (pathlike, enact, core.testing) and which do not |
| [H](unpacking/cluster-H.md) | mapping_toolkit's “Next” names the trie and interners as the remaining non-templates, but trie.h and intern.h already have them as templates; the sentence beside it is garbled | fixed — the Next bullet records the templates as done, and the garbled clause is gone |
| [H](unpacking/cluster-H.md) | pathset_storage's legend defines an “objects” column its table does not have | fixed — the legend describes the two columns the table has and points at the third arm's raw numbers |

## Left as written for wording alone

| Pass cluster | Where | Why |
|---|---|---|
| [A](unpacking/cluster-A.md) | `docs/design/task/04_request_vocabulary.md` — ## 1. What is decided | the difference between the two rounds&#x27; evidence for their tags, or between the two vocabulary renderings in how much entry text must back the words |
| [A](unpacking/cluster-A.md) | `docs/design/task/04_request_vocabulary.md` — ## 7. What this leaves out | a measure of the first-round text&#x27;s size deciding where it fits, or a &#x27;size&#x27; measure defined by the study elsewhere (02 §7.1) |
| [A](unpacking/cluster-A.md) | `docs/design/task/04_request_vocabulary.md` — ## 5. Open decisions (row &#x27;component=memory&#x27;) | names persisted in each memory&#x27;s tags (so every memory needs retagging), or persisted store-wide with no id layer through which a rename could happen in one place |
| [A](unpacking/cluster-A.md) | `docs/design/task/03_strobe_kit.md` — ## 2. The vocabulary pack | the checker verifies only format-level properties so any base in the format gives the same verdict, or this additive pack merely happens to pass against either base |
| [A](unpacking/cluster-A.md) | `docs/design/task/04_request_vocabulary.md` — ## 1. What is decided | still fails the round-2 test (logged in round 1): the difference between the rounds&#x27; evidence for their tags, or between the two renderings in how much entry text must back the words |
| [B](unpacking/cluster-B.md) | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Bias (9) | stricter about admitting docs as an expected component, or stricter about retaining docs rows already added |
| [B](unpacking/cluster-B.md) | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Key results (18) | disciplines could mean it makes the chosen subjects more accurate, or that it makes runs choose fewer, tighter subjects |
| [B](unpacking/cluster-B.md) | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Bias (9) | either the judging batches added the docs component less readily (a stricter bar for including it), or they held docs additions to a stricter definition; the mechanism of “kept more strictly” is not stated |
| [B](unpacking/cluster-B.md) | `docs/design/task/studies/vocabulary-evolution/phase-1-report.md` — Key results (18) | “disciplines” is a metaphor for an unstated mechanism: either naming an object constrains which subject tags are chosen, or its presence merely correlated with better subject precision on development |
| [C](unpacking/cluster-C.md) | `docs/design/task/studies/vocabulary-evolution/evidence.py` — The owner&#x27;s 20 against the blind re-judge (key.md) | either a contamination guard (the re-judge could have seen those tasks named in the draft&#x27;s evidence lists, so agreement is shown with and without them) or plain bookkeeping of an overlap; the page says neither which, nor what the draft&#x27;s evidence lists are |
| [C](unpacking/cluster-C.md) | `docs/design/task/studies/vocabulary-evolution/gaps.py` — The candidate values, most asked for first | &#x27;the four first-round dimensions&#x27; is never named on the page: the draft&#x27;s dimensions minus domain, or one candidate&#x27;s first round; naming them would be an assumption |
| [C](unpacking/cluster-C.md) | `docs/design/task/studies/vocabulary-evolution/gaps.py` — Found by the re-judge (phase 2&#x27;s key) | &#x27;blind&#x27; names no object: blind to the owner&#x27;s answers, to phase 1&#x27;s proposed tags, or to the runs&#x27; tags; the same unexplained &#x27;blind re-judge&#x27; recurs in report2.py and evidence.py, so one central wording should resolve it |
| [C](unpacking/cluster-C.md) | `docs/design/task/studies/vocabulary-evolution/evidence.py` — The owner&#x27;s 20 against the blind re-judge (key.md) | still open from round 1: either a contamination guard (the re-judge could have seen those tasks named in the draft&#x27;s evidence lists) or plain bookkeeping; unpacking would pick one |
| [C](unpacking/cluster-C.md) | `docs/design/task/studies/vocabulary-evolution/owner_review.py` — term table, Tag dimension (unified hover term — also in evidence.py; Meaning cell left untouched per the round 2 guardrail) | rule 1 would want the mechanism of &#x27;resolved from the inventory&#x27; (resolved from what input, via what mapping — the files a memory cites, or something else); the cell is unified site-wide, so the session must apply one text centrally |
| [D](unpacking/cluster-D.md) | `docs/design/enact/00_overview.md` — 4.10 Procedures — How retrieval treats it | either the procedure is exempt only from budget-driven ordering and cutoff but still consumes tokens, or it does not count against the token budget at all |
| [D](unpacking/cluster-D.md) | `docs/design/enact/00_overview.md` — 4.12 Where memories live | &#x27;the loop&#x27; could be §4.5&#x27;s writing loop (file, look, decide, write, review) or the whole session use-loop the server&#x27;s startup instructions describe |
| [E](unpacking/cluster-E.md) | `docs/design/enact/01_domain_model.md` — 4. Knowledge — Decision 4.1 | &#x27;by content hash&#x27; could mean the hash locates/keys the canonical file, or that the hash verifies the rebuilt copy is byte-identical — unpacking would pick one |
| [F](unpacking/cluster-F.md) | `docs/design/enact/03_store.md` — 3.4 When a human edits a view (option table) | either the check is inapplicable because a file edit carries no seen list, or it is waived because superseding the current head is inherently a response to what is there; the text gives no reason to make explicit |
| [F](unpacking/cluster-F.md) | `docs/design/enact/03_store.md` — 9.1 Where the repository lives | the reason is unstated: either several worktrees mean no single checkout sits at a fixed offset from the store, or the store simply cannot know where the checkout is — the two readings unpack differently |
| [F](unpacking/cluster-F.md) | `docs/design/enact/04_index_and_retrieval.md` — 3.4 A soft tag the memory does not carry | &#x27;contradiction ties&#x27; could mean contradicting memories tying with silent ones (both score zero) or contradictions merely appearing tied near the top; and the &#x27;so&#x27; may claim the recording is what confines the change to step 6, or only that the data is already kept |
| [F](unpacking/cluster-F.md) | `docs/design/enact/04_index_and_retrieval.md` — 3.2.1 Which procedure is placed, and what the rules version is for | no §4.13 exists in this file (§4 is the worked example); either an overview-§4.13 reference missing its document name, or a stale number — unpacking would require choosing |
| [G](unpacking/cluster-G.md) | `docs/enact.md` — During work, and after | &#x27;its space&#x27; could mean the memories the read&#x27;s hard tags admit, or the stores/scope the read consulted; unpacking &#x27;did not place&#x27; would commit to one |
| [G](unpacking/cluster-G.md) | `docs/enact.md` — During work, and after | &#x27;an entry procedure&#x27; reads either as an accepted entry that is a procedure, or as a slip for &#x27;an entry&#x27;/&#x27;a procedure&#x27;; what exactly is retired decides the unpacking |
| [G](unpacking/cluster-G.md) | `docs/enact.md` — During work, and after | &#x27;its space&#x27; may mean the stores the read covers or the tag space the read matched, and unpacking &#x27;did not place&#x27; requires choosing one |
| [G](unpacking/cluster-G.md) | `docs/enact.md` — One store for what is not about a project | the push may be what reaches other machines only (projects on this machine read the store directly), or the route to all other projects too; the sentence supports both |
| [G](unpacking/cluster-G.md) | `docs/reports/2026-09-16-memory-defects-root-cause.md` — B. Absence was never a result | a queried soft tag that matched no candidate, or any soft tag no candidate carries — the parenthesis supports both |
| [H](unpacking/cluster-H.md) | `docs/design/pathlike_engine_registry.md` — What every build proves | either only an exact case-fold of a listed extension resolves and every other near-miss stays unknown, or the generated chain of derived spellings is itself what is checked to be exact; which of the five example spellings resolve is not decidable from the text |
| [H](unpacking/cluster-H.md) | `docs/design/pathset_storage.md` — Open | probes stay but become cache-friendly ordered accesses, or the walk replaces probing with an in-order merge altogether |

## Not touched, and why

| Documents | Reason |
|---|---|
| plan.md, brief.md, plan-2.md, both split-study protocols | locked: their hashes prove they predate the data, and an edit would break that proof |
| the vocabulary YAML and its first-round render, every candidate's system.md, the batch instructions | experiment instruments: they are what runs and agents were shown, and rewording them would falsify the record |
| log.jsonl, owner-answers.json, the keys and samples | records: history is not rewritten; future entries follow the principle |
| the owner's 2026-09-20 evolving-memory synthesis | the owner's own text |
| memories in the ENACT stores | they change by supersede, not by editing; new memories follow the principle |

## Known limits

- Four hover terms still show another page's meaning (Memory, Value, Locator, and Arm on the locked plan):
  the docs server gives a term one meaning site-wide, first page by name — the renderer defect already on
  the decision list, out of this pass's reach.
- Table headers on the locked pages and on some pre-pass design pages remain without hover definitions;
  the pass's rules forbade adding term rows.
- The editing and the drift re-reads were done by the same model family that wrote most of the originals.
