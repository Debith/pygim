---
memory: 4355ffe47ca65f96db246035d903b958
title: "Memories are written concrete during a session and consolidated at its end"
origin: written
tags: ["domain=pygim","component=memory","artifact=design_doc","task=design","kind=decision"]
---
Debith's direction (2026-09-12): during work, memories record what was learned in that session, concretely and with the case. At the end of the session the memories written are reorganised, patterns across them are identified, and those are recorded at a more general level. This session did it by hand: the dnd study's fixes became one evaluation procedure and a few principles, each citing its case.

Consequences for the design, not yet built:
- A generalisation must not supersede its instances; they stay as its evidence. It needs a link of its own ("generalises" or "derived from"), distinct from supersedes and from merge, which joins memories that say one thing.
- The end of a session needs a trigger: an explicit consolidate step, or a session-close review that lists the session's writes and asks what they share.
- Generalise only as far as the evidence holds: `any` claims every value, and one case in one domain has not shown that.
