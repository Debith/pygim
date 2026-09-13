---
memory: be637228831618b0e63870c6a21a939e
title: "An artifact value names what a thing is, not how it is used"
origin: written
tags: ["domain=pygim","component=memory","artifact=vocabulary","task=design","kind=principle","concern=correctness"]
---
`artifact` is hard, so its answer must not depend on the situation: every value has to be a property of the thing itself, or the same entry gets different answers from different readers. dnd's `npc` broke this. A Bandit Captain stat block is a monster or an NPC depending only on how the table uses it — which is what `task`, or a soft dimension, is for — and that is also why the one-entry-per-category probe found no NPC entries at all.

Test for a candidate value: given only the thing, with no knowledge of how it will be used, would two classifiers agree? If not, the concept belongs in another dimension.
