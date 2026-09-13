---
memory: 7a85609343c62f2238ddaaa9cb4b745b
title: "Evaluating a vocabulary: reconcile it with its sources before trusting a measure"
origin: written
tags: ["domain=pygim","component=memory","artifact=vocabulary","task=evaluate","kind=procedure","concern=correctness"]
---
Steps, in order, for reviewing a vocabulary or a study report about one:

1. Frame. Check every count derived from the sources against the sources' own markers (headings, tables of contents, enumerations). A parse is wrong until its counts match. Fix, then re-lock before any classification, and keep the superseded lock.
2. Names. Every value is a term of the domain's ubiquitous language and carries a locator to where the term is used. List the values that have none.
3. Lists. Reconcile every derived list (a sample, a parse, a report table) with the source's complete enumeration. Show three groups: in both; in the source only (unmeasured or missed); derived only (parse errors). Never present a derived list alone.
4. Measures. Read each Cramér's V with the number of entries behind it, and tell applicability (a value that belongs to one kind of entry) apart from redundancy.
5. Coverage. Take one real entry from every artifact value, including the ones a sample cannot enumerate, and try to tag it.
6. Record what failed, and turn every check that can be code into code: a memory makes a miss less likely, a check rules it out.

Case, dnd-2026-09-11: the creature_type card showed 11 of 14 types because 16 sampled monsters held no construct, giant or plant (step 3); the parser also yielded Fiends, Monstrosities and "Celestial or Fiend" (step 3); a footnote was framed as a feat (step 1); `extended` was a coined name with no locator (step 2); the one-entry-per-category probe found no artifact value for a rule (step 5). report.py's declared_values() now performs step 3 for every card.
