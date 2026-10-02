# Starting a task — The September study, reviewed by STROBE

**Section 02: How complete the study in section 01 is, as a report of an observational study**
Status: draft · Owner: Debith · Last updated: 2026-09-27

Section 01 tagged every request of September with today's vocabulary. It is an observational
study: it counts what happened, in records that already existed, without changing anything. STROBE
is the checklist of 22 items that such a report should carry so a reader can judge it. This section
goes through the 22 items against section 01, then through the practices the D&D project's play-test
variant adds, and ends with what section 01 needs before its numbers can carry a decision.

| Term | Meaning |
|---|---|
| STROBE | *Strengthening the Reporting of Observational Studies in Epidemiology* (von Elm et al., 2007): 22 items a report of an observational study should carry, from the title to who paid for it. It says what to report, not how to run the study. |
| observational study | A study that records what happened without changing it. Section 01 is one: the requests were made before anyone thought to study them. |
| cross-sectional | A study that looks at every unit once, over one period. Section 01 is cross-sectional: one month of requests, each coded once. |
| census | A study of every unit that qualifies, not a sample of them. Section 01 is a census of the owner's typed messages from 1 to 26 September. |
| coder | Whoever turns a record into a value — here, eight agents, each tagging one part of the messages. |
| agreement | How often two coders, working apart, give the same value to the same unit. High agreement means the value follows from the unit itself; low agreement means it follows from whoever coded it. That is why agreement is what shows whether a coding measures the unit or the coder. |
| protocol lock | The question, the variables and the analysis, written and dated before the data is looked at. The D&D variant writes one before every play-test run. |
| exploratory | An analysis decided after seeing the data. It can suggest; it cannot confirm. |
| Item | One of STROBE's 22 items, by its number. |
| STROBE asks | What the item asks a report to state. |
| In 01 | Where section 01 states it, or what it states instead. |
| Verdict | reported — the item is there; partly — some of it is there; missing — none of it is. |
| To add | What section 01 needs for the item to be reported. |

**In one line: section 01 reports its main result with its denominators, but not how far its coding
can be trusted — 5 items reported, 14 partly, 3 missing.** The two gaps that matter most are that
no two coders ever tagged the same task, so agreement is unknown, and that the study was run by the
agent whose design it tests (items 12 and 22).

---

## 1. The 22 items

| Item | STROBE asks | In 01 | Verdict | To add |
|---|---|---|---|---|
| 1 Title and abstract | the design named in the title; a balanced summary of what was done and found | the title names the subject, not the design; *What it shows* is a summary of the findings, not of the method | partly | "a cross-sectional study of one month's requests" in the title; two sentences of method before the findings |
| 2 Background | why the study was done, and what was known | one sentence: to see how far the dimensions let an agent classify a request | partly | the decision it informs (the dimension redesign), and what section 00 §7 already knew |
| 3 Objectives | the objectives, and any hypotheses stated in advance | an aim, no questions, no hypotheses | partly | the questions: how often does each hard dimension follow from the request's words; what can no value express |
| 4 Study design | the design's key elements, early | *How it was made* describes the steps; the design is never named | partly | "cross-sectional census, coded by eight agents from one written protocol" |
| 5 Setting | where and when; the periods of collection | 1–26 September 2026; Claude Code's transcripts under `~/.claude/projects/`; three projects; the vocabularies' versions | reported | — |
| 6 Participants | who qualifies, how they were found, and who was left out | one owner; "records the host marks as typed by a person"; what was left out is listed, but not counted | partly | the eligibility rule in full, including the 6 records typed before the host recorded an origin; the counts of §3 |
| 7 Variables | every variable, defined | the five fits are defined in §1; what a *task* is, *at the request* and *in hindsight* are defined only in the agents' instructions, outside the report | partly | the instructions' definitions, in the report |
| 8 Data sources and measurement | where each variable comes from, how it is measured, and whether the measurement is comparable across groups | the transcripts, and agents with written rules; the per-part table shows coders differing; comparability is not assessed | partly | the model the coders ran on; that each part had one coder; the agreement of §4 |
| 9 Bias | how bias was addressed | three cautions: coders differ, vocabulary by folder, `kind` misused | partly | the biases in §4 (hindsight leaking into *at the request*, a task split at a part's edge, the coder's stake), each with the way it pushes the results |
| 10 Study size | how the size was arrived at | 510 messages, 255 tasks; not said that this is every message | partly | "a census: every typed message of the period" |
| 11 Quantitative variables | how quantities were handled; groupings and why | categories counted; words shown at three tasks or more; themes counted by a text search, marked approximate | reported | — |
| 12 Statistical methods | the methods; subgroups; missing data; sensitivity analyses | none stated: counts and shares only; no agreement, no uncertainty | missing | a methods paragraph; agreement between coders (§4); what was done about messages a coder could not read without the transcript |
| 13 Participants (flow) | numbers at each stage, why units were lost, a flow diagram | 510 messages, 255 tasks, 9 messages in no task; nothing upstream of 510 | partly | the flow of §3 |
| 14 Descriptive data | the units' characteristics; missing data per variable | tasks per project, per session and per part; messages per task | reported | — |
| 15 Outcome data | the outcome counts | the fit of every hard dimension, per project (§1) | reported | — |
| 16 Main results | estimates with their precision | "18 of 255" and the other shares, each with its denominator; no precision | partly | a statement of how the month is read: as a census, describing only itself, or as a sample of how requests look in general — and, read as a sample, an interval for each share, because each share is then an estimate |
| 17 Other analyses | subgroups, interactions, sensitivity analyses | hindsight, gaps, words, themes; the themes were chosen after reading the agents' reports | partly | the themes and the per-part comparison labelled exploratory |
| 18 Key results | the key results, against the objectives | the one-line finding | reported | — |
| 19 Limitations | the limitations, with the direction and size of each bias | three cautions, none with a direction | partly | the directions of §4 |
| 20 Interpretation | a cautious interpretation, with the limitations and other evidence | one conclusion in a caution ("the vocabulary should follow the task"); no interpretation section | partly | what the numbers do and do not support, beside section 00's failures |
| 21 Generalisability | how far the results hold elsewhere | not discussed | missing | one owner, one month, one host, three projects, and a D&D project half about something else |
| 22 Funding and other | who funded it, and their role | not stated | missing | who ran it — the agent that designed the vocabulary under test, arguing for a change to it in the same conversation — and what the coders were told the study was for |

---

## 2. What the D&D variant adds

The D&D project runs each play-test as a small study and reports it with a STROBE sheet of its
own. Its practices go further than STROBE's checklist, which only says what to report; they say how
to collect it so that the report can be honest. Each is a check section 01 can be held to as well.

| Practice | In the D&D project | Serves item | In 01 | Verdict |
|---|---|---|---|---|
| Lock the protocol before looking | `00 - Run Protocol Lock.md` per run: variants, purpose, metrics, the dice seed; "Locked before play" | 3, 4, 7, 12 | the agents' instructions were written before any tagging, but after the records were looked at to decide what counts as typed; neither is dated in the report, and the themes were decided after the results | partly |
| Keep observations apart from conclusions | the evidence sheet says "Observations only; no balance conclusions"; the report across runs is updated only in a separate step the owner starts | 15 against 18–20 | the rows (§6, and the agents' files) are apart from the findings, in the same page; the rows' `gap` notes are judgements, not observations | partly |
| Every count cites its rows, with its denominator | the report's count policy; the Feature Coverage Ledger — uses against opportunities, per variant | 13–16 | every share is "k of n"; the theme counts are from a text search and cannot be traced to rows one by one | partly |
| Record every alternative at a decision | evidence rows carry `comparators:` — every legal option, with why it was not chosen | 7, 9 | `several` records that more than one value fitted, and which; not why each was kept or ruled out | partly |
| Quality, coder and review per row | `data_quality`, `auditor`, `review_status` on every row | 8, 9 | the coder is known by the part's letter; 15 rows were checked by hand, but the rows do not say which | partly |
| Grade the evidence | the report's evidence grades: a *decision scan* is moderate, not audit- or dice-grade | 19, 20 | no grade | missing |

---

## 3. How the records became messages

Item 13 asks for the numbers at each stage. Counted again on 2026-09-27 over every transcript under
`~/.claude/projects/`, for records from 1 September:

| Stage | Records |
|---|---|
| user records in the transcripts | 6,344 |
| — a tool's result | − 5,640 |
| — a skill's text or other text the host marks as not typed | − 45 |
| — a summary written at a compaction | − 10 |
| — a notification of a finished background task | − 99 |
| — an interruption or other record with no origin | − 36 |
| typed by a person, with the host's origin field | 508 |
| typed, before the host recorded an origin | 6 |
| **messages** | **514** |

Section 01 used 510: its extraction ran on 2026-09-26, and four messages were typed after it. From
the 510, the agents found 255 tasks, holding 501 messages, and put 9 messages in no task.

---

## 4. What section 01 needs, most important first

1. **Agreement between coders.** Each task was tagged by one agent. Have a second agent tag a random
   sample of about fifty tasks, blind to the first tags, and report agreement per hard dimension, as
   Cohen's kappa with its n. Without it, a share such as "86 tasks: several values fit" mixes two causes
   that cannot be told apart: how often the vocabulary truly offers several values, and how this
   one coder judged fits — the share measures the coder as much as the vocabulary (items 8, 12).
2. **The biases, with their direction** (items 9, 19):
   - *Hindsight leaking into "at the request".* The coders saw a task's later messages while tagging
     its first. Anything they carried back could make a tag look decided by the request's words when
     it was in fact decided by what came later, so the request looks easier to tag than it was, and
     the main finding — 18 of 255 decided by the words — is if anything too high.
   - *The coder's stake.* The study was run by the agent that argued, in the same conversation, for
     reshaping the vocabulary, and the coders were told the study would inform that. It pushes towards
     finding gaps: 194 of 255 tasks name one.
   - *Parts cut by date.* A task that crossed from one part to the next was split in two, so one
     task is counted as two. It adds tasks to the total; how many tasks were split this way is
     unmeasured.
   - *The vocabulary chosen by folder.* Each task was tagged with the vocabulary of the project
     folder it was typed in. 19 of the 43 D&D tasks were not about D&D, so no domain value of the
     D&D vocabulary fits them and they were counted as *none*; it inflates *none* for that project.
3. **The protocol, dated.** Put the agents' instructions in the report as the protocol, with the
   date they were written, and label everything decided later — the themes, the per-part comparison
   — as exploratory (items 3, 12, 17).
4. **The method, named.** The design in the title; a methods paragraph; the flow of §3 (items 1, 4,
   6, 10, 13).
5. **Who ran it, and how far it holds** (items 21, 22).

---

## 5. What this review leaves out

It judges the report, not the tagging: it did not re-tag any task. It uses STROBE's checklist as the
standard, though STROBE was written for epidemiology; items 12 and 16 assume a study that estimates
effects; a descriptive census makes no such estimates, so parts of those two items do not apply to
section 01, and their verdicts weigh only the parts that do. The D&D
variant was read from its files — one evidence sheet, one protocol lock, the run fidelity standard
and the report across runs — not from the project's history of why each practice was adopted.
