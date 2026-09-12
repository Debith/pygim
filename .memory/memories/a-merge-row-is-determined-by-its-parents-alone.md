---
memory: 01ca92505b3c3ed3fc238c240eabbe19
title: "A merge row is determined by its parents alone"
origin: written
tags: ["domain=pygim","component=memory","artifact=store","task=implement","kind=principle","layer=strategy","concern=correctness"]
---
A merge row carries no clone, sequence or timestamp: its id is the digest of its parents. Two clones that merge the same heads therefore produce the same row, and replay or catch-up dedupes by row id. Stamping a merge with who merged it breaks that symmetry and forks the chain.
