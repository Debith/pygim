---
memory: f22c7c3d6978f1808d1ed26016301c3c
title: "Tags are the domain's ubiquitous language"
origin: written
tags: ["domain=pygim","component=memory","artifact=vocabulary","task=design","kind=principle","concern=documentation"]
---
Name a value with the term the domain itself uses, found in its sources, and cite where. The dimension and value together read as that term: layer=python is pygim's "Python layer", so a value name shared with another dimension is fine when each qualified tag is a term the domain really uses (language=python is too). A clash the domain would not make is not fine: component=build and concern=build were two meanings pygim's docs never share, so the concern became toolchain, the docs' own word.

Coined names fail the reader. dnd's `extended` (a casting time of a minute or more) is a word the rulebooks never use, and a reviewer could not tell what it meant; proposing `build_integrity` for pygim was the same mistake. Let counts in the sources decide between candidates — pygim's docs say "lifetime" 90 times and "memory safety" never. When the domain has no settled term, say so in the entry instead of inventing one silently.
