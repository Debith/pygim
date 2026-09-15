---
memory: d2d847afdc946b20ba46c893ceaec09b
title: "Design documents lead with practical scenarios, drawn in small diagrams"
origin: written
tags: ["domain=pygim","component=docs","artifact=design_doc","task=design","kind=preference","concern=documentation"]
---
Debith's preference for pygim's design documents (2026-09-09/10). Structure an illustration of a design as features with scenarios, the way a behaviour spec is written: a feature is something a person does with the system; a scenario is one concrete situation stated as Given / When / Then and then drawn panel by panel. Use the real domain with consistent ids and texts from first scenario to last — they double as acceptance-test fixtures — and put the setup feature first (the vocabulary: "nothing else can be done before that"). Let the mechanism appear as steps of a scenario, never as abstract mechanism panels.

Carry small, simple Mermaid diagrams near what they explain, with field tables, rather than code; code only where the shape itself is the design, such as a template signature. Six small diagrams in place beat one big class diagram at the end. Never ASCII boxes in served documents.

Case: the memory design's "how a memory is made" (docs/design/memory/00a) and its model by scenario (01a).
