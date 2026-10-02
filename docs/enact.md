# ENACT: setting up and using it

ENACT is pygim's adaptive cognitive layer. It gives an agent knowledge, remembers what happened,
and learns from whether what it offered helped. The knowledge is not found by similarity to the
prompt: it is filed by the *kind of problem* it belongs to, and found by asking what kind of
problem the agent is working on. ENACT runs as an MCP server; Claude Code starts it for
each session. The design is [docs/design/enact/](design/enact/00_overview.md), whose overview
lists the seven parts — Memory, Knowledge, Vocabulary, Adaptive Context, Retrieval Policy,
Consolidation and Experience Learning.

New stores start with twelve shared activities and F3-derived request guidance: cue words,
positive decision questions and exclusions. The default `vocabulary` call delivers that
guide. Classify what the current request asks for, using the conversation; tag a stored
memory by what its knowledge helps someone do. Those can be different activities.
Domain packs supply subjects and concerns. Existing stores keep their own vocabulary until
explicitly migrated, so installing a newer pygim does not rewrite their definitions or tags.
See the [request vocabulary example](examples/enact/example_01_request_vocabulary.py) and
[vocabulary ownership rules](design/enact/02_taxonomy_and_sources.md#request-guidance-and-memory-classification).

## Set up a project

After `pip install` of pygim, run one command inside the project:

```bash
oo enact setup --branch      # the store lives on an orphan `enact` branch, shared through git
# or
oo enact setup --user        # the store lives in your user data directory, on this machine only
# or
oo enact setup --local       # the store lives in the project as .enact, committed with the code
```

`setup` does whatever is missing:

1. **Finds or creates the store.** `--branch` checks out an orphan `enact` branch as a worktree
   beside the project (`../<project>-enact`); it shares no history with the code. `--user` uses
   `~/.local/share/pygim/memory/<project>` on Linux, `~/Library/Application Support/...` on macOS,
   `%LOCALAPPDATA%\...` on Windows. `--local` keeps `.enact` inside the project, on the branch
   that carries it — simplest for a project with one checkout. But the store is then committed
   with the code, so checking out another branch means working against that branch's copy of the
   store: each branch has its own.
   `--from .enact` starts a new store as a copy of an existing one, with its history.
2. **Points every worktree at it** with `git config pygim.enact <path>` (`--branch` and `--user`). Git keeps that in the
   clone's shared config, so all worktrees of the project, on any branch, use the same store.
3. **Registers the MCP server with Claude Code** at user scope, once for every project. The server
   is registered without a store path: it finds each project's store from the directory Claude Code
   starts it in. If the `claude` command is not on your PATH, `setup` prints the command to run.

Restart Claude Code (or `/mcp` → reconnect) and the `pygim-enact` tools and prompts appear.

### How the store is found

For any command, and for the server, in order:

| | Where | When to use it |
|---|---|---|
| 1 | `--root <path>` | a one-off command |
| 2 | `$PYGIM_ENACT_ROOT` | one store for everything in a shell |
| 3 | `git config pygim.enact` | what `setup` writes: per clone, shared by its worktrees |
| 4 | a `.enact` directory above the working directory | a store committed inside the project |

`oo enact status` says which store it found.

### One store for what is not about a project

Knowledge about no single project — how you want explanations written, how you want options laid
out — goes in a **global store**, which every project on this machine reads:

```bash
oo enact setup --global      # ~/.local/share/pygim/memory/global, recorded in your git config
```

The agent reads and writes it by passing `scope: global`, and tags such knowledge `domain=any`.
Each write is committed at once, and pushed if you give the store a git remote — that is what
carries a preference to your other projects and machines. Sessions already running are told at
their next memory call; sessions started afterwards get it in their standing knowledge.

A store shared with other people is the other case. Its `policy.yaml` says `push: manual`: a
write is still committed, but only in your own clone — nothing is pushed for you. To reach the
others, the writes go out as a pull request, and they arrive when the store's owners merge it.

### Several stores, one repository

Each store keeps its own history, so unrelated stores can share one private repository as separate
branches — for example `memory` for a project's, `global` for the machine's, `dnd` for another
project's. Two rules of thumb. First, a store's remote need not be its project's — and for a
project whose repository is public it must not be, or pushing the store would publish the memory
with the code.
Second, a store can live anywhere, as long as its inventory paths are relative to the project
root, so they resolve no matter where the store sits (`oo enact status` opens it and reports
anything it cannot resolve).

### Stores this machine holds

```bash
oo enact stores            # what a session can name with `scope`
oo enact stores --remote   # and what the remote holds that is not checked out here
```

A store is found, never configured: the project's own, the global one, anything in your user data
directory, and any `<name>-enact` directory beside the project. Its name comes from its
`policy.yaml`, or from the directory with the store suffix dropped — so `~/projects/ddd-memory` is `ddd`,
and an agent reads it with `scope: "ddd"`.

Two things a named store does *not* do: it never contributes to the standing knowledge injected
into every session (only the project's store and the global one do), and a write to it follows its
own policy — a store marked `sharing: community` is published by its owners, not by you.

### On another machine

```bash
pip install pygim
git fetch origin memory          # --branch projects: the memory travels with the repository
oo enact setup --branch         # checks the existing branch out, points git at it, registers
```

A `--user` store stays on the machine that has it; copy the directory, or use `--branch` to share.

## Start a new project's memory

Two prompts do the first work. In Claude Code they appear under `/` as
`/mcp__pygim-enact__prepare-vocabulary` and `/mcp__pygim-enact__seed-memories`.

1. **`prepare-vocabulary`** drafts the project's vocabulary from its own documents. The
   vocabulary is the set of questions knowledge is filed by; the draft names those questions in
   the project's own words, and each value cites the place in the documents its word comes from.
   The draft lands in the store under `taxonomy/studies/<date>-<domain>/` with a report.
   Read the report, then make the vocabulary live:

   ```bash
   oo enact accept --pack <store>/taxonomy/studies/<date>-<domain>/proposal/pack-<domain>.yaml
   ```

   A running server picks it up at its next call, and tells the agent the vocabulary changed.
   `accept` also checks each citation against the document it names, and prints a warning for any
   whose cited text the document does not carry where the citation points. A replacement
   pack (`--replace`) can drop values the live pack defines. While any memory still carries a
   dropped value, the replacement is refused, and the message names those memories — retag them
   first, then replace.
2. **`seed-memories`** records what the documents and history already know — decisions,
   conventions, how recurring tasks are done — as the first memories.

## During work, and after

The agent reads before it works and writes what outlives the task; the server's instructions teach
it that loop. A read can narrow by a word (`term`) when tags cannot name the subject. It also
reports how its candidates rest on the project's documents. A memory carries citations, and each
citation names a document; the store's inventory lists the project's documents. The read compares
the two and reports both sides: which documents the candidate memories cite, and which inventoried
documents none of them cites. So when the memory cannot answer a question, the
read has already named the document to send it to.

**Standing knowledge** is what a session should know before it does anything: every memory of kind
`preference` in full, and the title of every `procedure`, from the global store and the project's.
It is returned by `session`, the first call every session makes. The server's startup instructions
carry only an index of titles, newest first, because Claude Code keeps the first 2,048 characters
of a server's instructions and drops the rest without saying so. That silent cut has already cost
something once: a preference sat past it, never reached the agent, and a recommendation went out
that the preference would have changed. A read also names, under `standing`, any
preference in its space that it did not place. To put the full texts in front of an agent without
depending on it to ask, print them from a session-start hook:

```bash
oo enact status --standing
```

When you want the session's lessons drawn together, run
`/mcp__pygim-enact__consolidate`. The agent writes each pattern it finds as a generalisation and
records its lessons learnt in `reviews/session-<n>.md` in the store: what it generalised, what it
left as cases and why, and the gaps it saw. Read it, then accept each pattern you agree with:

```bash
oo enact accept <key> --reason "the cases do share it"
```

Only then do that pattern's cases fold under it in reads: instead of each case standing in the
context on its own, a read places the accepted generalisation and lists its cases under it as
evidence. The agent has no tool to accept its own pattern.

Reading them is the point, so the command shows rather than asks for a key:

```bash
oo enact accept              # what is waiting, by title, and how many memories each would fold
oo enact accept --all        # each one in full — text, tags, what it folds — answered y / n / q
```

Enter accepts. A wrong yes is not a trap: retiring an entry procedure unfolds its instances again.
`--yes` skips the display, for scripts.

## Leaving messages for other sessions

Several sessions, agents and people work on one project. The store's **mailbox** is where they
leave each other feedback, requests and comments — separate from memories, which are knowledge that
outlives a task.

```bash
oo enact mailbox                                   # what is open, oldest first
oo enact mailbox --post "Finish the rebase" --kind request --to next-session
oo enact mailbox --all                             # resolved ones too; nothing is deleted
```

The agent has `mailbox` and `post`. A message that `resolves` another closes it and must say
something itself, so a thread is never closed silently. Open messages are listed by `session` and
printed by the session-start hook, so a new session sees them without being asked.

## Commands

| Command | What it does |
|---|---|
| `oo enact setup [--branch \| --user \| --local] [--from DIR]` | find or create the store, point the clone at it, register the server |
| `oo enact setup --global` | create this machine's global store, read by every project |
| `oo enact reload` | ask running servers to restart into the installed code (`--signal` reaches other projects') |
| `oo enact mailbox [--post TEXT]` | read or leave messages for other sessions and agents |
| `oo enact status` | which store, its version, reviews waiting, proposals |
| `oo enact vocabulary [DIMENSION]` | the store's current vocabulary: every dimension and value, or one dimension in full (`--request`: the guide an agent classifies a request with; `--scope global`: another store) |
| `oo enact accept [--all]` | see what is waiting, in words, and accept what you agree with |
| `oo enact accept --pack <proposal>` | make a drafted vocabulary pack live |
| `oo enact ingest <file>` | bring in hand-written memories |
| `oo enact mcp` | the server itself; Claude Code runs this |

## How the output reads

The memory commands follow the project's rule for glance value: a visible title, indentation that
means something, a blank line where the subject changes, and colour on the few things worth acting
on — a count, a refusal, a change that happened. Colour is never the only signal; every coloured
thing also carries a word.

It turns itself off when the output is not a terminal, when `NO_COLOR` is set to any value, when
`TERM=dumb`, and when you pass `oo --no-color`. So piping into `grep` or a CI log gives plain text
without asking.

## When something is off

- **A tool says there is no store.** Run `oo enact setup` in the project.
- **The tools do not appear.** `claude mcp list` should show `pygim-enact`. A `.mcp.json` in the
  project that also defines `pygim-enact` overrides the user registration — remove its entry.
- **A new server feature is missing in a running session.** Run `oo enact reload`: each server
  restarts into the installed code at its next quiet moment, keeping your session. A result also
  says `server_stale` once when the code on disk has moved on. A reload replaces the server's
  code, but not what the agent knows about it: the agent still holds the tool descriptions it was
  given when it connected, and a parameter added since then is missing from those descriptions.
  So if a *new parameter* seems ignored, reconnect (`/mcp`) as well.
- **A vocabulary edit breaks loading.** The next tool call reports the file and line; fix the file
  and the server reopens it.
- **A memory's text is empty, and `oo enact status` says `text missing`.** Its content object was
  deleted. The file under `memories/` may hold the only copy — have the agent write it back as a
  memory superseding that one.
- **You edited a file under `memories/`.** It is kept as you left it, and `oo enact status` lists
  it as a review. The file is a view: it is generated from the memory and shows it, so
  editing the file changes what is shown, not the memory itself. To make the edit the memory,
  have the agent write the edited text superseding that memory.

## Driving it from a shell

Everything the agent can do, `oo enact call` can do, through the same dispatch the MCP server uses:

```bash
oo enact call read --json '{"hard": ["domain=pygim", "artifact=store", "task=implement"]}'
echo '{"memory": "#12", "cite": "design-03:L451", "reason": "where it is said"}' | oo enact call link
```

It prints the tool's JSON result. A refusal is a result here as everywhere else — it prints with
`refused` and exits **0**. Exit **1** means the call could not be made at all: no such tool, or the
arguments were not JSON. `oo enact call nonesuch --json '{}'` lists the tools there are.

Each call is a process, and a process is a session. For the MCP server that is right: the server
is one process per conversation, so the conversation is one session. For a script it is wrong:
each call would be a new process, and so a new session, putting every memory in a session of its
own and leaving `review` nothing to gather. Name one instead:

```bash
export PYGIM_ENACT_SESSION=$(oo enact call session --json '{}' | jq -r .session)
# or per call: oo enact call remember --session 12 --json '{...}'
```

This is what the end-to-end tests use: a process per step, nothing imported, so they see the CLI,
the MCP dispatch, the adapter, the C++ service and the files store the way a shell script does.
One round trip is about 90 ms.
