# pygim memory: setting up and using it

pygim's problem-space memory gives an agent knowledge found by the *kind of problem* it is
working on, rather than by similarity to the prompt. It runs as an MCP server; Claude Code starts
it for each session. The design is [docs/design/memory/](design/memory/00_overview.md).

## Set up a project

After `pip install` of pygim, run one command inside the project:

```bash
oo memory setup --branch      # the store lives on an orphan `memory` branch, shared through git
# or
oo memory setup --user        # the store lives in your user data directory, on this machine only
# or
oo memory setup --local       # the store lives in the project as .memory, committed with the code
```

`setup` does whatever is missing:

1. **Finds or creates the store.** `--branch` checks out an orphan `memory` branch as a worktree
   beside the project (`../<project>-memory`); it shares no history with the code. `--user` uses
   `~/.local/share/pygim/memory/<project>` on Linux, `~/Library/Application Support/...` on macOS,
   `%LOCALAPPDATA%\...` on Windows. `--local` keeps `.memory` inside the project, on the branch
   that carries it — simplest for a project with one checkout, but each branch then has its own.
   `--from .memory` starts a new store as a copy of an existing one, with its history.
2. **Points every worktree at it** with `git config pygim.memory <path>` (`--branch` and `--user`). Git keeps that in the
   clone's shared config, so all worktrees of the project, on any branch, use the same store.
3. **Registers the MCP server with Claude Code** at user scope, once for every project. The server
   is registered without a store path: it finds each project's store from the directory Claude Code
   starts it in. If the `claude` command is not on your PATH, `setup` prints the command to run.

Restart Claude Code (or `/mcp` → reconnect) and the `pygim-memory` tools and prompts appear.

### How the store is found

For any command, and for the server, in order:

| | Where | When to use it |
|---|---|---|
| 1 | `--root <path>` | a one-off command |
| 2 | `$PYGIM_MEMORY_ROOT` | one store for everything in a shell |
| 3 | `git config pygim.memory` | what `setup` writes: per clone, shared by its worktrees |
| 4 | a `.memory` directory above the working directory | a store committed inside the project |

`oo memory status` says which store it found.

### On another machine

```bash
pip install pygim
git fetch origin memory          # --branch projects: the memory travels with the repository
oo memory setup --branch         # checks the existing branch out, points git at it, registers
```

A `--user` store stays on the machine that has it; copy the directory, or use `--branch` to share.

## Start a new project's memory

Two prompts do the first work. In Claude Code they appear under `/` as
`/mcp__pygim-memory__prepare-vocabulary` and `/mcp__pygim-memory__seed-memories`.

1. **`prepare-vocabulary`** drafts the project's vocabulary from its own documents: the questions
   knowledge is filed by, named in the project's own words, each value citing where the word comes
   from. The draft lands in the store under `taxonomy/studies/<date>-<domain>/` with a report.
   Read the report, then make the vocabulary live:

   ```bash
   oo memory accept --pack <store>/taxonomy/studies/<date>-<domain>/proposal/pack-<domain>.yaml
   ```

   A running server picks it up at its next call.
2. **`seed-memories`** records what the documents and history already know — decisions,
   conventions, how recurring tasks are done — as the first memories.

## During work, and after

The agent reads before it works and writes what outlives the task; the server's instructions teach
it that loop. When you want the session's lessons drawn together, run
`/mcp__pygim-memory__consolidate`. The agent writes each pattern it finds as a generalisation and
records its lessons learnt in `reviews/session-<n>.md` in the store: what it generalised, what it
left as cases and why, and the gaps it saw. Read it, then accept each pattern you agree with:

```bash
oo memory accept <key> --reason "the cases do share it"
```

Only then do that pattern's cases fold under it in reads. The agent has no tool to accept its own
pattern.

## Commands

| Command | What it does |
|---|---|
| `oo memory setup [--branch \| --user \| --local] [--from DIR]` | find or create the store, point the clone at it, register the server |
| `oo memory status` | which store, its version, reviews waiting, proposals |
| `oo memory accept <key>` | accept a generalisation, so its cases fold |
| `oo memory accept --pack <proposal>` | make a drafted vocabulary pack live |
| `oo memory ingest <file>` | bring in hand-written memories |
| `oo memory mcp` | the server itself; Claude Code runs this |

## When something is off

- **A tool says there is no store.** Run `oo memory setup` in the project.
- **The tools do not appear.** `claude mcp list` should show `pygim-memory`. A `.mcp.json` in the
  project that also defines `pygim-memory` overrides the user registration — remove its entry.
- **A vocabulary edit breaks loading.** The next tool call reports the file and line; fix the file
  and the server reopens it.
