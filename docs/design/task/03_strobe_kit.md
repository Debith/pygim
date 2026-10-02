# Starting a task — Studies by STROBE: the procedures, the templates and the vocabulary

**Section 03: How a study is run, reported and reviewed, in any project**
Status: draft · Owner: Debith · Last updated: 2026-09-27

Debith, 2026-09-27: "Analyze through the STROBE process. Dnd is using variant of the template. I need
our review process to follow STROBE. We need to write vocabulary for it even, also procedure and some
templates. All can be memorized." Section 02 is the analysis. This section is the rest: two procedures
and four templates, memorized in the machine's global store — the store a session reads in every
project — because a study can be run in any project, and a vocabulary pack drafted for that
store, which waits for acceptance.

It comes from the D&D project, which already runs every play-test as a small study — a protocol
locked before play, an evidence sheet of observations only, a report across runs — and which no
store described (0 of the D&D store's 293 memories mention STROBE).

| Term | Meaning |
|---|---|
| STROBE | Strengthening the Reporting of Observational Studies in Epidemiology (von Elm et al., 2007): 22 items a report of an observational study should carry, from the title to who paid for it. It says what to report, not how to run the study. |
| collection practices | The D&D variant's six ways of collecting evidence so the report can be honest: protocol lock, observations apart from conclusions, counts that cite rows, every alternative, quality per row, graded evidence. |
| pack | A drafted addition to a store's vocabulary; it becomes live only when the owner accepts it. |
| Memory | The memory's number in the global store. |
| Asked by | The words that make the request hook deliver a procedure. |

---

## 1. What was memorized

All in the global store, tagged `domain=any artifact=any` because a study is run the same way in any
field.

| Memory | What it is | Asked by |
|---|---|---|
| #56 | procedure: **run a study** — lock the protocol before looking, collect evidence apart from conclusions, recount the flow, measure agreement, label the exploratory, report every item, name each bias's direction, review before sharing | STROBE, observational study, study the sessions, study report, evidence sheet, protocol lock, play-test, cross-sectional, cohort, case-control |
| #55 | procedure: **review a study** — read the rows, not only the report; the 22 items; the six practices; recount the flow and every quoted number; each bias with its direction; rank by what could overturn the result | STROBE, review the study, study review, study report, evidence sheet, through STROBE, reporting checklist |
| #51 | template: the protocol, locked before the data is looked at | — |
| #52 | template: the evidence sheet — one row per observation, its alternatives, quality, coder and review, and the coverage of every count | — |
| #53 | template: the study report, one heading per STROBE item | — |
| #54 | template: the review — the 22 items and the six practices, each with its verdict | — |
| #57 | the general review procedure (was #46), unchanged except that its *Not* now sends a study, a study report or an evidence sheet to #55 | analyze, review, critique, … |

`show` any of them for the full text; the templates are in their bodies as Markdown.

---

## 2. The vocabulary pack

Drafted in `__notes__/strobe/pack-study.yaml` and checked with ENACT's pack checker: it adds 29
values and removes nothing. The checker has no scope to point it at another store, so it checked the pack beside pygim's
vocabulary rather than the global store's; the check carries over, because the global store's base
vocabulary is in the same format.

| Adds | Values | What for |
|---|---|---|
| domain | `study` | every pack is a domain: running and reporting studies of what happened |
| artifact | `study_protocol`, `evidence_sheet`, `study_report` | the three documents of a study, so a read can ask for one |
| study_design (soft) | `cohort`, `case_control`, `cross_sectional` | STROBE asks some items differently of each design |
| strobe (soft) | the 22 items: `title_abstract`, `background`, `objectives`, `design`, `setting`, `participants`, `variables`, `data_sources`, `bias`, `study_size`, `quantitative_variables`, `statistical_methods`, `participant_flow`, `descriptive_data`, `outcome_data`, `main_results`, `other_analyses`, `key_results`, `limitations`, `interpretation`, `generalisability`, `funding` | a memory about one item carries the item's value as a tag, so a read that asks for the value returns it — "agreement between coders" is tagged `strobe=statistical_methods` |

To accept it into the global store — a person runs this:

```
oo enact accept --pack __notes__/strobe/pack-study.yaml --root ~/.local/share/pygim/memory/global
```

Once it is live, #51–#56 can carry the new tags (the protocol template `artifact=study_protocol`,
the review `strobe=` every item it checks), which is a follow-up, not part of the pack.

---

## 3. How it maps onto the D&D variant

The D&D project keeps its own, richer forms; the global ones are what every study needs, and the
D&D forms are one specialisation of them.

| In the D&D project | The global form | What the D&D form adds |
|---|---|---|
| `00 - Run Protocol Lock.md` per run | #51 the protocol | the variants under test, the dice seed |
| `04 - STROBE Evidence Sheet.md`, 28 fields a row | #52 the evidence sheet | the rules code, the journal beat, the game-state change, the full comparator bundle |
| the Feature Coverage Ledger | #52's coverage table | per-variant uses against opportunities |
| `living-feature-evidence-report.md`, updated in a separate step | #53 the report | evidence grades; the owner starts every synthesis |
| `RUN-FIDELITY-STANDARD.md` §6, the completion checklist | #54's six practices | dice-grade, journal and decision-log completeness |

---

## 4. For the owner

| Decision | Options | Recommendation |
|---|---|---|
| The pack | accept as drafted / change first / not now | **accept**: it is additive, and the procedures work without it until then |
| The D&D store | leave it without STROBE / add one memory there pointing at #56 and the D&D forms | **add the pointer**, so a session in that project finds both the global procedure and its own richer forms |
| Section 01 | leave it / bring it up to STROBE with section 02's list, starting with a blind second coding of fifty tasks | **bring it up**, agreement first: every share in it depends on the coding, and agreement is what shows whether the coding can be trusted |

---

## 5. What this leaves out

STROBE's sub-items (1a and 1b, 12a to 12e, and so on) are folded into one line per item; its
explanation-and-elaboration paper and its extensions for particular fields were not used. The pack
was checked against pygim's vocabulary, not the global store's. Nothing in the D&D project was
changed.
