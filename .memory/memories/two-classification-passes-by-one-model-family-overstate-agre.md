---
memory: 6229856292800efdbc42a5e1aa519e29
title: "Two classification passes by one model family overstate agreement"
origin: written
tags: ["domain=pygim","component=memory","artifact=vocabulary","task=evaluate","kind=principle","concern=correctness"]
---
Cohen's κ measures agreement between passes, not truth. When both passes come from the same model family, and one of them wrote the codebook, they share blind spots and agree where independent coders would not. dnd-2026-09-11 reported κ from 0.773 to 1.000 under exactly this limitation. Read such a κ as an upper bound. For a vocabulary others will depend on, take at least one pass from a different model or a person, or check a subsample by hand, before treating the agreement threshold as met.
