# Problem-Space Memory — Technical Specification

**Section 01a: The model, scenario by scenario**
Status: draft · Owner: Debith · Last updated: 2026-09-13

Every scenario of [section 00a](00a_how_a_memory_is_made.md) again, this time in the types of
[section 01](01_domain_model.md). For each one, two small pictures: a **class diagram** of the
types the scenario touches — only the fields it reads or writes, with its concrete values —
and a **sequence diagram** of who calls whom, in order. Ids, versions and texts are the ones
00a uses, so a panel there and a diagram here describe the same moment.

The actors are always the same four: the **Agent** thinks, the **Service** checks and keeps the
books, the **Index** is the snapshot the service reads, the **Store** is where commits land, and
the **Human** governs the vocabulary and reviews in git.

---

## Scenario 0 — Starting up

Startup reads every record type from files and builds the one thing retrieval reads: the
snapshot, with a `memory_state` per memory. The snapshot itself belongs to section 04; its
version is read back from the audit log, not counted from one.

```mermaid
classDiagram
    direction LR
    class taxonomy {
        version 3
        rejections 1
    }
    class memory {
        count 54
        digest checked at load
    }
    class association {
        source seed, written, curated, learned, proposed
    }
    class lineage {
        supersedes edges
    }
    class usage_record {
        folded at load
    }
    class memory_state {
        tags
        superseded_by
        use
    }
    class snapshot {
        version 55
        inverted tag to memories
        forward memory to tags
        procedure slot
    }
    snapshot --> taxonomy : tag ids
    snapshot --> memory_state : one per memory
    association --> snapshot : both maps
    lineage --> memory_state : which are heads
    usage_record --> memory_state : counters
    memory_state --> memory
    note for snapshot "section 04 defines it — the version comes from the audit log"
```

```mermaid
sequenceDiagram
    participant H as Claude Code
    participant M as MCP server
    participant S as Service
    participant St as Store
    participant X as Index
    participant O as Observers
    H->>M: launch over stdio
    M->>S: build from config — composite store, files canonical
    S->>St: load taxonomy, sources, memories, index side
    St-->>S: taxonomy v3 · 29 sources · 54 memories · associations, lineage, usage
    S->>S: check each memory — digest, heads, chains — 54 of 54
    S->>St: last version in the audit log
    St-->>S: v55
    S->>X: build the maps, the states, the procedure slot — publish v55
    S->>St: rehash 29 documents
    St-->>S: shardwake-corruption-magic changed — 1 citation into it
    S->>O: start query logger and statistics, each on its own thread
    M-->>H: tools registered
    H->>M: session start
    M->>S: session start
    S-->>M: session 12 · vocabulary v3 · 1 review item
    M-->>H: ready
```

---

## Feature 1 — Preparing a domain

### Scenario 1.1 — The base vocabulary ships with the tool

A fresh repository holds one thing: taxonomy v0, five dimensions, every entry described.

```mermaid
classDiagram
    direction LR
    class taxonomy {
        version 0
        dimensions domain, artifact, task, kind, tier
        tags 11
        domain values none yet
    }
    class codebook_entry {
        brief
        full
        when
        when_not
        example
        complete()
    }
    taxonomy "1" --> "16" codebook_entry : one per dimension and value
    note for taxonomy "an empty domain dimension is the signal that a pack is needed"
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant St as Store
    A->>S: open session
    S->>St: load taxonomy
    St-->>S: taxonomy v0
    S-->>A: vocabulary v0 — 5 dimensions, 11 values, every entry described
    Note over A: domain has no values — prepare one before writing anything
```

### Scenario 1.2 — The agent prepares the D&D domain

The study's output is a set of proposals — whole dimensions and single values — each carrying
a codebook entry and, where it was read off a document, a locator.

```mermaid
classDiagram
    direction LR
    class source {
        id phb-2024-glossary
        kind text
        version digest b04c
    }
    class locator {
        source phb-2024-glossary
        line 212
        passage digest
    }
    class dimension_proposal {
        name purpose
        role soft
        weight 2000
        state accepted
    }
    class tag_proposal {
        concept blinded
        dimension mechanic
        state accepted
    }
    class codebook_entry {
        when
        when_not
        example
    }
    class taxonomy {
        version 1
        dimensions 10
        tags 53
    }
    locator --> source : points into
    tag_proposal --> locator : justified_by
    tag_proposal --> codebook_entry
    dimension_proposal --> codebook_entry
    taxonomy --> dimension_proposal : accepted as a set
    taxonomy --> tag_proposal : accepted as a set
```

```mermaid
sequenceDiagram
    participant H as Human
    participant A as Agent
    participant S as Service
    participant St as Store
    H->>A: prepare the dnd domain from reference/rules
    A->>S: inventory(28 documents)
    S->>St: source rows — id, path, kind, version digest
    Note over A: derive structure · harvest given axes · derive implied axes<br/>walk the facet checklist · classify the sample twice
    A->>S: propose(dnd pack — 5 dimension proposals, value proposals, evidence sheet, report)
    S->>S: shape check — every entry complete, every locator resolves
    S-->>H: one card per dimension, with its numbers
    H->>S: accept — 1 boundary rewritten, scaling flagged, school dropped, purpose weight 2000
    S->>St: commit taxonomy v1 and an audit row naming proposer, reviewer and every edit
    S-->>A: vocabulary v1 — 10 dimensions, 53 values
```

### Scenario 1.3 — A word the vocabulary lacks turns up during work

The write lands at once under the tags that exist; the proposal rides along, pending, and
points into a document the inventory does not hold yet.

```mermaid
classDiagram
    direction LR
    class write_request {
        tags domain, artifact, task, purpose, mechanic, kind, tier
        decision new
        seen 1, 2, 3
        proposals corruption, ritual casting
    }
    class memory_4 {
        slug spreading-a-sign-of-corruption
        kind principle
    }
    class tag_proposal {
        concept corruption
        dimension mechanic
        asked_by 4
        state pending
    }
    class locator {
        source shardwake-corruption-magic
        line 41
    }
    class source {
        path homebrew/corruption/WIP/corruption-magic.md
        inventoried on accept
    }
    write_request --> memory_4 : produces
    write_request --> tag_proposal : carries
    tag_proposal --> locator : justified_by
    locator --> source : not yet in the inventory
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant St as Store
    participant H as Human
    A->>S: read(design space)
    S-->>A: context — #1 first, then #2, #3
    A->>S: remember(write_request with 2 proposals)
    S->>S: four checks pass · entries complete · one locator outside the inventory
    S->>St: commit #4, snapshot v4, proposals pending
    S-->>A: #4 written
    H->>S: accept(mechanic=corruption)
    S->>St: taxonomy v2 · source added · link #4 to mechanic=corruption, source proposed
    S-->>A: next session — vocabulary v2, 29 documents
```

### Scenario 1.4 — A proposal is turned down

A rejection is data too: it is published with the vocabulary so it is not proposed again.

```mermaid
classDiagram
    direction LR
    class tag_proposal {
        concept ritual casting
        asked_by 4
        state rejected
        reason covered by action_economy=ritual
    }
    class taxonomy {
        version 3
        rejected ritual casting
    }
    taxonomy --> tag_proposal : publishes the rejection
```

```mermaid
sequenceDiagram
    participant H as Human
    participant S as Service
    participant St as Store
    participant A as Agent
    H->>S: reject(ritual casting, covered by action_economy=ritual)
    S->>St: proposal rejected · audit row
    S-->>A: next session — vocabulary v3, 1 rejected, with its reason
    Note over A: a ritual blight is action_economy=ritual — no proposal this time
```

---

## Feature 2 — Create a new spell

### Scenario 2.1 — No memory yet

Three writes in one session, each a `write_request` that becomes a `memory`. The first is the
procedure, found through the locator the study left on `task=balance`.

```mermaid
classDiagram
    direction LR
    class memory_1 {
        kind procedure
        cites dmg-2024-ch3 line 297
        content six steps
    }
    class memory_2 {
        kind principle
        cites phb-2024-spells line 4459
        content Shield is the yardstick
    }
    class memory_3 {
        kind example
        content Frost Ward, typed resistance
    }
    class lineage {
        kind written
        session 1
    }
    memory_1 --> lineage
    memory_2 --> lineage
    memory_3 --> lineage
    note for memory_1 "seen none — the space was empty"
    note for memory_3 "seen 1 and 2 — the procedure and the yardstick"
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant X as Index
    participant St as Store
    A->>S: read(design space)
    S->>X: candidates
    X-->>S: none
    S-->>A: 0 candidates, no procedure for spell and design
    A->>S: locate(task=balance)
    S-->>A: dmg-2024-ch3 line 297 — Creating a Spell
    A->>S: remember(procedure, seen none)
    S->>St: commit #1, snapshot v1
    Note over A: designs Frost Ward by following #1's steps
    A->>S: remember(yardstick, seen #1, cites Shield)
    S->>St: commit #2, snapshot v2
    A->>S: remember(Frost Ward decision, seen #1 #2)
    S->>St: commit #3, snapshot v3
```

### Scenario 2.2 — A similar spell already has memories

The read returns a `context` with the procedure in the first slot and ranked `match`es after
it; the session ends in two usage records and one new chain.

```mermaid
classDiagram
    direction LR
    class context {
        procedure 1
        selected 2, 3
        candidates 3
    }
    class match_2 {
        soft_score 5500
        rank 1
    }
    class match_3 {
        soft_score 5500
        rank 2
    }
    class retrieval_receipt {
        snapshot_id of v4
        taxonomy_version of v1
    }
    class usage_record {
        id 2
        kind useful
    }
    class memory_5 {
        kind principle
        content cap a rider that scales with damage taken
    }
    context --> match_2
    context --> match_3
    context --> retrieval_receipt : leaves
    usage_record --> match_2 : the note that helped
    note for match_3 "tied at 5500 — the id breaks the tie"
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant X as Index
    participant St as Store
    A->>S: read(defensive, reaction, mitigation, mid)
    S->>X: posting lists, then forward maps
    X-->>S: candidates #1 #2 #3 and their tags
    S-->>A: context — #1 first, #2 rank 1, #3 rank 2 — and a receipt
    Note over A: designs Ember Shell
    A->>S: learn(#2, useful)
    A->>S: learn(#1, steps_held)
    S-)St: two usage records, asynchronously
    A->>S: remember(cap the rider, seen #1 #2 #3)
    S->>St: commit #5, snapshot v5
```

### Scenario 2.3 — A variant of a spell

The one scenario where a check refuses. The refused write and the accepted one differ in two
fields: `decision` and `seen`.

```mermaid
classDiagram
    direction LR
    class refused_write {
        decision new
        seen none
    }
    class accepted_write {
        decision supersedes 3
        seen 1, 2, 3, 4, 5
    }
    class memory_6 {
        content the Ward family
    }
    class lineage {
        kind written
        supersedes 3
    }
    class memory_state_3 {
        superseded_by 6
    }
    accepted_write --> memory_6 : produces
    memory_6 --> lineage
    lineage --> memory_state_3 : the index marks the old head
    note for refused_write "no unread write — 5 candidates unseen"
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant St as Store
    A->>S: read(design space)
    S-->>A: #1 first, then #2 #3 #5 #4
    A->>S: remember(Ember Ward, decision new, seen none)
    S--xA: refused — candidates #1 #2 #3 #4 #5 unseen
    A->>S: remember(the Ward family, supersedes #3, seen #1 to #5)
    S->>S: #3 is the head of its chain — yes
    S->>St: commit #6, lineage supersedes #3, #3 superseded by #6, snapshot v6
    A->>S: remember(high-tier insight, seen #1 to #6)
    S->>St: commit #7, snapshot v7
```

---

## Feature 3 — Balance an existing spell

### Scenario 3.1 — The right note is in a neighbouring space

Nothing is written. Three usage records in three sessions become one learned `association`.

```mermaid
classDiagram
    direction LR
    class query_hard {
        hard task=balance
        result 2 only
    }
    class query_softened {
        soft task=balance
        result 2, 6, 5, 7, 4, 1
    }
    class usage_record {
        id 6
        tag task=balance
        kind useful
    }
    class counters {
        useful 3
    }
    class association {
        memory 6
        tag task=balance
        source learned
    }
    query_softened --> usage_record : the answer came from 6
    usage_record --> counters : folded into
    counters --> association : threshold reached
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant St as Store
    A->>S: read(task=balance as hard)
    S-->>A: #2 only
    A->>S: read(task=balance as soft)
    S-->>A: #2 #6 #5 #7 #4, then #1
    Note over A: answers from #6 — the verdict is the deliverable, no write
    A->>S: learn(#6, task=balance)
    S-)St: usage record — count 1
    Note over S,St: sessions 8 and 11 report the same
    S->>St: count 3 — association learned, audit row, new snapshot
```

### Scenario 3.2 — The human links a tag directly

The same `association` as a promotion, reached in one step, with a reason in the human's words.

```mermaid
classDiagram
    direction LR
    class association {
        memory 7
        tag task=balance
        source curated
        reason high-tier balancing always needs this
    }
    class memory_state_7 {
        tags plus task=balance
    }
    association --> memory_state_7 : materialised into
```

```mermaid
sequenceDiagram
    participant H as Human
    participant S as Service
    participant St as Store
    H->>S: link(#7, task=balance, reason)
    S->>S: value in the list · #7 is a head
    S->>St: association curated · audit row · new snapshot
```

---

## Feature 4 — Bring in a colleague's notes

Ingested memories carry a `lineage` of kind `seed` that names the corpus file — not a source,
because its blocks are memories, not reference text.

```mermaid
classDiagram
    direction LR
    class lineage {
        kind seed
        corpus notes/spells.md
        revision digest
    }
    class memory_8 {
        slug shield-baseline
    }
    class memory_50 {
        slug shield-baseline
    }
    class association {
        source seed
    }
    memory_8 --> lineage
    memory_50 --> memory_8 : supersedes, after the pull
    association --> memory_8
    note for memory_50 "same slug, new content digest"
```

```mermaid
sequenceDiagram
    participant H as Human
    participant S as Service
    participant St as Store
    H->>S: ingest(notes/spells.md)
    S->>S: validate each block against the taxonomy
    S-->>H: line 118 refused — level_band=tier2 is not a value
    S->>St: 42 memories #8 to #49, lineage seed, no look step
    Note over H,St: later — the colleague edits shield-baseline, git pull
    S->>S: next load — known slug, different digest
    S->>St: #50 supersedes #8
```

---

## Feature 5 — Two notes turn out to say one thing

### Scenario 5.1 — An ingested note duplicates a chain

A merge makes one `memory` whose `lineage` supersedes both. The verdict is read from the
lineage of the two sources, not judged.

```mermaid
classDiagram
    direction LR
    class memory_2 {
        lineage written
    }
    class memory_50 {
        lineage seed
    }
    class memory_51 {
        lineage merged
        tags the union
    }
    memory_51 --> memory_2 : supersedes
    memory_51 --> memory_50 : supersedes
    note for memory_50 "seed lineage — no look step was ever expected"
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant St as Store
    A->>S: read(balance space)
    S-->>A: #2 and #50, back to back
    Note over A: one point, two chains
    A->>S: merge(#2 #50, merged text, reason)
    S->>St: commit #51, supersedes #2 and #50, union of tags, snapshot v52
    S->>S: verdict — #50 came through ingestion
    S-->>A: #51 · ingested, no read expected
```

### Scenario 5.2 — A critique session writes the same point under another task

The verdict comes from two records: what the later write saw, and what the earlier head
carried at that moment.

```mermaid
classDiagram
    direction LR
    class write_52 {
        tags task=critique
        decision new
        seen 10, 15, 22, 28, 34, 41
    }
    class memory_state_5 {
        tags task=design only
    }
    class memory_53 {
        lineage merged
        tags task=design and task=critique
    }
    memory_53 --> memory_state_5 : supersedes 5
    memory_53 --> write_52 : supersedes 52
    note for write_52 "5 was never in this candidate set — classification mismatch"
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant St as Store
    A->>S: read(critique space)
    S-->>A: six of the colleague's notes
    A->>S: remember(cap it, seen those six)
    S->>St: commit #52 — every check passes
    Note over A,St: later, a softened read shows #5 and #52 side by side
    A->>S: merge(#5 #52, merged text, reason)
    S->>St: commit #53, tags from both tasks, snapshot v54
    S->>S: verdict — #5 was not in #52's candidate set
    S-->>A: #53 · classification mismatch · review task=critique's description
```

---

## Feature 6 — A note turns out to be wrong

A correction is a write whose `decision` supersedes the note it corrects; the old record keeps
its digest and its history.

```mermaid
classDiagram
    direction LR
    class write_request {
        decision supersedes 6
        seen 6
        reason duration was wrong
    }
    class memory_54 {
        lineage corrected
    }
    class memory_state_6 {
        superseded_by 54
    }
    class memory_6 {
        content digest unchanged
    }
    write_request --> memory_54 : produces
    memory_54 --> memory_6 : supersedes
    memory_state_6 --> memory_6 : index side
```

```mermaid
sequenceDiagram
    participant H as Human
    participant A as Agent
    participant S as Service
    participant St as Store
    H->>A: #6 says end of your next turn — both spells say start
    A->>S: remember(corrected text, supersedes #6, seen #6)
    S->>S: #6 is the head — yes
    S->>St: commit #54, lineage corrected, #6 superseded, snapshot v55
    A->>S: show #6
    S-->>A: retired · digest unchanged · superseded by #54 · in 23 contexts before
```

## Feature 7 — Close a session

A generalisation is a write whose request names the memories it is drawn from. The service
checks that they are heads and that the generalisation covers them; nothing on the instances
changes except the edge that points back.

### Scenario 7.1 — Three new reactions make one point

```mermaid
classDiagram
    direction LR
    class write_request {
        decision new
        generalises 54 55 56 57
        seen 2 7 54 55 56 57
    }
    class memory_58 {
        lineage written
        kind principle
    }
    class memory_state_55 {
        superseded_by empty
        generalised_by 58
    }
    class memory_55 {
        content digest unchanged
    }
    write_request --> memory_58 : produces
    memory_58 --> memory_55 : generalises
    memory_state_55 --> memory_55 : index side
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant St as Store
    A->>S: review(session 13)
    S-->>A: #55 #56 #57 with their tags
    A->>S: read(domain=dnd artifact=spell task=design, purpose~defensive)
    S-->>A: #2 #54 #55 #56 #57 #7
    A->>S: remember(principle, generalises #54 to #57, seen #2 #7 #54 to #57)
    S->>S: #54 to #57 are heads — yes
    S->>S: covers them on every hard dimension — yes
    S->>St: commit #58, generalises #54 to #57, nothing superseded, snapshot v59
```

### Scenario 7.2 — A wider suspicion waits for its case

```mermaid
classDiagram
    direction LR
    class coverage_check {
        for each hard dimension
        every instance value is a generalisation value
        or the generalisation answers any
    }
    class memory_58 {
        artifact spell
        task design
    }
    class instances_54_to_57 {
        artifact spell
        task design
    }
    coverage_check --> memory_58 : reads tags
    coverage_check --> instances_54_to_57 : reads tags
```

```mermaid
sequenceDiagram
    participant A as Agent
    participant S as Service
    participant H as Human
    Note over A: suspects magic items behave the same — no case, so no tag, and nothing is written
    Note over S: had #58 answered artifact=any, coverage would still pass
    H->>H: the diff would show four spells claiming every artifact — review catches it
```
