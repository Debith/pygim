---
memory: c4992fbd5c85fda643ffd36fb8055013
title: "Mermaid pitfalls in served design documents"
origin: written
tags: ["domain=pygim","component=docs","artifact=design_doc","task=implement","kind=reference","concern=documentation"]
---
Served documents render Mermaid 11 in the browser, and there is no local renderer, so a broken diagram shows only when someone opens the page. Scan every new block for these before publishing:
- `@` inside a flowchart label breaks it (it is shape syntax): write a locator as `source:L297`.
- `;` ends a statement in sequence and state diagrams: never put one in a message or a transition label.
- Class members go one per line, never `{ a; b }`; a member never starts with `+ - # ~` and never contains `#` or `~`. Write `generalises 54 55`, not `#54`.
`#n` is fine in flowchart and sequence labels.

Cases: docs/design/memory, where `;` in two sequence messages and one state label, and `@` in locators, would each have broken a page.
