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

### One store for what is not about a project

Knowledge about no single project — how you want explanations written, how you want options laid
out — goes in a **global store**, which every project on this machine reads:

```bash
oo memory setup --global      # ~/.local/share/pygim/memory/global, recorded in your git config
```

The agent reads and writes it by passing `scope: global`, and tags such knowledge `domain=any`.
Each write is committed at once, and pushed if you give the store a git remote — that is what
carries a preference to your other projects and machines. Sessions already running are told at
their next memory call; sessions started afterwards get it in their standing knowledge.

A store shared with other people is the other case: its `policy.yaml` says `push: manual`, so
writes stay in your own clone and reach the others as a pull request its owners merge.

### Several stores, one repository

Each store keeps its own history, so unrelated stores can share one private repository as separate
branches — for example `memory` for a project's, `global` for the machine's, `dnd` for another
project's. Two rules of thumb: a store's remote need not be its project's, and for a project whose
repository is public it must not be; and a store can live anywhere, as long as its inventory paths
are relative to the project root (`oo memory status` opens it and reports anything it cannot
resolve).

### Stores this machine holds

```bash
oo memory stores            # what a session can name with `scope`
oo memory stores --remote   # and what the remote holds that is not checked out here
```

A store is found, never configured: the project's own, the global one, anything in your user data
directory, and any `<name>-memory` directory beside the project. Its name comes from its
`policy.yaml`, or from the directory with `-memory` dropped — so `~/projects/ddd-memory` is `ddd`,
and an agent reads it with `scope: "ddd"`.

Two things a named store does *not* do: it never contributes to the standing knowledge injected
into every session (only the project's store and the global one do), and a write to it follows its
own policy — a store marked `sharing: community` is published by its owners, not by you.

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

   A running server picks it up at its next call, and tells the agent the vocabulary changed.
   `accept` also prints a warning for any citation that does not hold in its document. Replacing a
   live pack (`--replace`) is refused while memories carry a value it removes; the message names
   them, so they can be retagged first.
2. **`seed-memories`** records what the documents and history already know — decisions,
   conventions, how recurring tasks are done — as the first memories.

## During work, and after

The agent reads before it works and writes what outlives the task; the server's instructions teach
it that loop. A read can narrow by a word (`term`) when tags cannot name the subject, and says which
documents its candidates cite and which inventoried ones none does, so a question the memory cannot
answer is sent to the right document. Those instructions also carry the project's *standing knowledge* — every memory of
kind `preference` in full, and the title of every `procedure` — so it reaches each session without
a read, as of the server's start. When you want the session's lessons drawn together, run
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
| `oo memory setup --global` | create this machine's global store, read by every project |
| `oo memory reload` | ask running servers to restart into the installed code (`--signal` reaches other projects') |
| `oo memory status` | which store, its version, reviews waiting, proposals |
| `oo memory accept <key>` | accept a generalisation, so its cases fold |
| `oo memory accept --pack <proposal>` | make a drafted vocabulary pack live |
| `oo memory ingest <file>` | bring in hand-written memories |
| `oo memory mcp` | the server itself; Claude Code runs this |

## When something is off

- **A tool says there is no store.** Run `oo memory setup` in the project.
- **The tools do not appear.** `claude mcp list` should show `pygim-memory`. A `.mcp.json` in the
  project that also defines `pygim-memory` overrides the user registration — remove its entry.
- **A new server feature is missing in a running session.** Run `oo memory reload`: each server
  restarts into the installed code at its next quiet moment, keeping your session. A result also
  says `server_stale` once when the code on disk has moved on. The agent still holds the tool
  descriptions it was given when it connected, so if a *new parameter* seems ignored, reconnect
  (`/mcp`) as well.
- **A vocabulary edit breaks loading.** The next tool call reports the file and line; fix the file
  and the server reopens it.
- **You edited a file under `memories/`.** It is kept as you left it, and `oo memory status` lists
  it as a review. The file is a view: to make the edit the memory, have the agent write it
  superseding that memory.
