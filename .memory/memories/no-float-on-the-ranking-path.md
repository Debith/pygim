---
memory: df9b22e1bf2cc14191918454ba5c365c
title: "No float on the ranking path"
origin: written
tags: ["domain=pygim","component=memory","artifact=service","task=implement","kind=principle","layer=core","concern=correctness"]
---
Weights and scores are integer milli-units end to end. Ranking is then bit-for-bit reproducible across platforms and compilers, which is what lets a retrieval receipt be rerun and compared. Parse weights with parse_weight, which returns milli-units and never a double.
