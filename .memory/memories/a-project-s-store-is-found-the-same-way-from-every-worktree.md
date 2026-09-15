---
memory: 07af2bbec0cceb8f4b4888050d62dee3
title: "A project's store is found the same way from every worktree"
origin: written
tags: ["domain=pygim","component=memory","artifact=store","task=design","kind=decision","concern=portability"]
---
Debith works with several worktrees of one project on different branches (pygim, pygim-rc, pygim-registry, pygim-release), so a store committed inside the project gives each branch its own copy, or none — pygim-rc already held a drifting second copy of .memory at v24. Decided 2026-09-15: stores live on an orphan `memory` branch checked out as a worktree of its own, or in the user data directory, and every command and the MCP server find one in the order --root, $PYGIM_MEMORY_ROOT, `git config pygim.memory`, a .memory above the working directory. The git config is the global option: git keeps it in the clone's shared config, so every worktree sees one store. `oo memory setup` creates or joins the store (--from copies an existing one with its history), writes the config, and registers the server at user scope without a root. Consequence: source inventory paths are relative to the project root, not the store (design 02 §5.2, 03 §9.1).
