---
memory: 64dc2ee3d56f4e306125e5e0ce319b82
title: "Extend a command with options instead of adding a near-duplicate command"
origin: written
tags: ["domain=pygim","component=cli","artifact=cli_command","task=design","kind=preference","concern=public_api","language=python"]
---
Debith (2026-09-15): prefer adding options to an existing CLI command over creating several slightly different commands — respect DRY and keep commands rich. A separate verb is for a different job (`setup` versus `status`), not a different flavour of the same job.

Cases: `oo memory init` duplicated what `oo memory setup` does, so it was dropped and its one unique case became `setup --local`. `oo memory accept` (a generalisation) and `oo memory accept-pack` (a vocabulary draft) were one job on two targets, so they became `oo memory accept [MEMORY] [--pack PROPOSAL]`.

Before adding a verb, check whether an existing verb does the same kind of job on another target or in another mode, and give it an option instead.
