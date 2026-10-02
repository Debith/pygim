"""Where a project's ENACT store lives, and how a machine is set up to use it.

A store is found, in order, by:

1. an explicit root (``--root``);
2. ``$PYGIM_ENACT_ROOT``;
3. ``git config pygim.enact`` — git keeps it in the clone's shared config, so every worktree of
   the project finds the same store;
4. the working directory itself, or an ancestor, being a store — running a command inside a store's
   own worktree means that store;
5. a ``.enact`` directory found by walking up from the working directory.

Each of those is asked for under every name the system has had, newest first: it was called
``memory`` until 2026-09-22 and the stores on disk still say so, and none of them is moved.

``oo enact setup`` writes that git config, creates the store — in a user-level directory, or
on an orphan ``enact`` branch checked out as a worktree of its own — and registers the MCP
server with Claude Code at user scope, without a root, so it finds each project's store from the
directory it is started in.

Beside a project's store there may be one **global store**: what holds knowledge that is about no
single project, tagged ``domain=any`` — how to write for this person, how they like options laid
out. Every session reads it, whatever project it is in, so a preference written once reaches
projects that do not exist yet. It is found by ``$PYGIM_ENACT_GLOBAL``, then
``git config --global pygim.enact.global``, then the default place under the user data directory.

A store says in ``policy.yaml`` how its writes leave the machine: ``push: auto`` commits and
pushes each write (a personal store, one owner), ``push: manual`` leaves that to a person (a
project's store, or one a community shares and whose owners review what enters). The file is
committed, so a clone of a shared store knows not to push before anyone has to remember.

Nothing here imports click, and nothing here reads the environment: every function that needs to
know where to look is given an `Environment`, filled once at a composition root (`_pygim._config`).
The CLI and the MCP server both use this module, and both wire it the same way.
"""
from __future__ import annotations

import os
import shutil
import signal
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, List, Optional, Sequence

from .._config import Environment

GIT_KEY = "pygim.enact"
LOCAL = ".enact"
BRANCH = "enact"
SERVER = "pygim-enact"
GLOBAL_KEY = "pygim.enact.global"
GLOBAL_NAME = "global"
POLICY = "policy.yaml"

# Spellings that are still read, newest first. The system was called "memory" until 2026-09-22 and
# the stores already on disk carry that name in their directory, their branch and the git config
# that points at them; nothing is moved, so every lookup asks for each spelling in turn. A tuple
# rather than a pair of constants, so the next name costs one entry instead of a search.
GIT_KEYS = (GIT_KEY, "pygim.memory")
LOCALS = (LOCAL, ".memory")
BRANCHES = (BRANCH, "memory")
GLOBAL_KEYS = (GLOBAL_KEY, "pygim.memory.global")
SUFFIXES = ("-enact", "-memory")


def without_suffix(name: str) -> str:
    """*name* with whichever store suffix it carries removed — `ddd-memory` and `ddd-enact` are both `ddd`."""
    for suffix in SUFFIXES:
        if name.endswith(suffix):
            return name[:-len(suffix)]
    return name


_UNASKED = object()   # asked-and-nothing is not the same as not-asked-yet


@dataclass(frozen=True)
class Found:
    """A store's root and how it was found — shown to the user, since four places can name one."""
    root: Path
    how: str

    @property
    def exists(self) -> bool:
        return is_store(self.root)


def is_store(path: Path) -> bool:
    return (path / "taxonomy" / "base.yaml").is_file()


def git(args: Sequence[str], cwd: Path) -> Optional[str]:
    """``git <args>`` in *cwd*: its stripped output, or None when git is missing or it fails."""
    try:
        done = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def main_worktree(cwd: Path) -> Optional[Path]:
    """The clone's main worktree — the directory that holds its ``.git`` — from any of its worktrees."""
    common = git(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd)
    if not common:
        return None
    common_dir = Path(common)
    return common_dir.parent if common_dir.name == ".git" else Path(git(["rev-parse", "--show-toplevel"], cwd) or cwd)


def project_root(cwd: Path) -> Path:
    """What a source citation's path is relative to: this worktree's top, or *cwd* outside git."""
    top = git(["rev-parse", "--show-toplevel"], cwd)
    return Path(top) if top else cwd








# ── how a store's writes leave the machine ────────────────────────────────────


@dataclass(frozen=True)
class Policy:
    """A store's own rules, committed with it so a clone arrives knowing them."""
    sharing: str = "project"   # project · personal (one owner) · community (owners review)
    push: str = "manual"       # auto: each write is committed and pushed · manual: a person does it
    name: str = ""             # what a session calls this store; empty means "take it from the directory"
    project: str = ""          # the checkout its citations are relative to, when the store lives outside it

    @property
    def automatic(self) -> bool:
        return self.push == "auto"


def policy(root: Path) -> Policy:
    """*root*'s policy, or the safe default — a store without the file is a project's, published by
    hand, which is how every store made before this existed behaves."""
    fields = {}
    try:
        text = (root / POLICY).read_text(encoding="utf-8")
    except OSError:
        return Policy()
    for line in text.splitlines():
        key, sep, value = line.partition(":")
        if sep and not key.startswith((" ", "\t", "#")):
            fields[key.strip()] = value.split("#")[0].strip()
    return Policy(sharing=fields.get("sharing", "project"), push=fields.get("push", "manual"),
                  name=fields.get("name", ""), project=fields.get("project", ""))


def write_policy(root: Path, p: Policy) -> None:
    named = (f"name: {p.name}\n" if p.name else "") + (f"project: {p.project}\n" if p.project else "")
    (root / POLICY).write_text(
        "# How this store is shared, and how its writes leave this machine (03 §9.1).\n"
        + named
        + f"sharing: {p.sharing}\n"
        f"push: {p.push}\n", encoding="utf-8")


def project_of(root: Path) -> Optional[Path]:
    """The checkout a store's citations are relative to. A store kept beside the project it serves
    has to say where that is, or nothing can resolve `reference/rules/...` — its policy names it,
    and failing that the convention does: `<project>-memory` sits beside `<project>`."""
    declared = policy(root).project
    if declared:
        path = Path(declared).expanduser()
        return (root / path).resolve() if not path.is_absolute() else path
    sibling = root.parent / without_suffix(root.name)
    return sibling.resolve() if sibling != root and sibling.is_dir() else None


def store_name(root: Path) -> str:
    """What a session calls a store: the name its policy declares, else its directory's — with a
    trailing store suffix dropped, so ~/projects/ddd-memory is `ddd`, and a `.enact` named for its
    project. Lowercase; a session addresses a store by this."""
    declared = policy(root).name
    if declared:
        return declared.strip().lower()
    if root.name in LOCALS:
        return root.parent.name.lower()
    return without_suffix(root.name).lower() or root.name.lower()


def publish(root: Path, message: str) -> str:
    """Records a write in *root*'s git history if its policy says to, and pushes when a remote
    exists. Returns what happened, for the caller to pass on; never raises — a store that cannot be
    published is still a store, and the memory is already written."""
    if not policy(root).automatic:
        return "manual: this store is published by its owners"
    if git(["rev-parse", "--git-dir"], root) is None:
        return "not a git repository — nothing to commit to"
    git(["add", "-A"], root)
    if git(["commit", "-m", message], root) is None:
        return "nothing to commit"
    if not git(["remote"], root):
        return "committed (no remote yet)"
    branch = git(["rev-parse", "--abbrev-ref", "HEAD"], root) or "HEAD"
    if git(["push", "origin", branch], root) is None:
        return "committed, but the push failed — push it yourself"
    return "committed and pushed"


# ── what stores this machine has ──────────────────────────────────────────────


@dataclass(frozen=True)
class Scope:
    """A store a session can address by name, and where it was found."""
    name: str
    root: Path
    how: str
    aliases: List[str]


class Stores:
    """What this machine holds, and how one is found.

    Built from an `Environment` where the program is wired, and given the session's lifetime. That
    lifetime is the point: within one session the answer to "which stores are there" cannot change,
    and an object can hold what it found where a free function must re-walk the filesystem and shell
    out to git on every call — 6.6 ms a time, on every scoped tool call, before this existed.

    Everything here was a module function taking `where` as its first argument. Nine of them, each
    reaching into it: that is the method list of an object, written in the wrong place (global
    memory #14). What is still a module function below takes only paths, and is genuinely free.

    Nothing is cached until it is asked for, and `refresh` drops it all — a setup creates a store,
    and what was true a moment ago is not."""

    def __init__(self, where: Environment) -> None:
        self.where = where
        self._found: Any = _UNASKED
        self._scopes: Optional[List[Scope]] = None
        self._global: Any = _UNASKED

    def at(self, cwd: Path) -> "Stores":
        """The same machine seen from another directory. A new object: what was found from here
        says nothing about what is found from there."""
        return Stores(self.where.at(cwd))

    def refresh(self) -> None:
        """Forget what was found. Called after anything that creates or moves a store."""
        self._found = _UNASKED
        self._scopes = None
        self._global = _UNASKED

    # ── finding ───────────────────────────────────────────────────────────

    def find(self) -> Optional[Found]:
        """The store for this environment, by the order in the module docstring, or None when
        nothing names one. A root named on the command line or in the environment is returned
        whether or not a store exists there yet — setup creates it."""
        if self._found is _UNASKED:
            self._found = self._look()
        return self._found

    def _look(self) -> Optional[Found]:
        cwd = Path(self.where.cwd).resolve()
        if self.where.store_root:
            return Found(Path(self.where.store_root).resolve(),
                         self.where.store_root_from or "--root")
        for key in GIT_KEYS:
            configured = git(["config", "--get", key], cwd)
            if configured:
                path = self.where.path(configured) if configured.startswith("~") else Path(configured)
                if not path.is_absolute():
                    path = (main_worktree(cwd) or cwd) / path
                return Found(path.resolve(), "git config " + key)
        for directory in (cwd, *cwd.parents):
            if is_store(directory):      # standing in the store itself, as one does in its own worktree
                return Found(directory.resolve(), "the working directory is a store")
            for local in LOCALS:
                if is_store(directory / local):
                    return Found((directory / local).resolve(), local + " above the working directory")
        return None

    def root(self) -> Optional[Path]:
        """The project's store, if there is one there."""
        found = self.find()
        return found.root if found is not None and found.exists else None

    def guidance(self) -> str:
        """What to tell someone whose project has no store yet."""
        found = self.find()
        if found and not found.exists:
            return f"{found.root} (from {found.how}) is not an ENACT store yet — run `oo enact setup` in the project"
        return ("no ENACT store for this project — run `oo enact setup --user` (a store in your user directory) "
                "or `oo enact setup --branch` (an orphan `enact` branch shared through git) in the project; "
                "`--local` keeps one inside the project instead")

    def global_root(self) -> Optional[Path]:
        """The global store, or None when this machine has none: ``$PYGIM_ENACT_GLOBAL``, then
        ``git config --global pygim.enact.global``, then the default place if a store is there."""
        if self._global is _UNASKED:
            self._global = self._look_global()
        return self._global

    def _look_global(self) -> Optional[Path]:
        if self.where.global_root is not None:
            return self.where.global_root
        for key in GLOBAL_KEYS:
            configured = git(["config", "--global", "--get", key], self.where.home)
            if configured:
                return self.where.path(configured).resolve()
        default = self.where.user_data / GLOBAL_NAME
        return default if is_store(default) else None

    # ── every store, by name ──────────────────────────────────────────────

    def scopes(self) -> List[Scope]:
        """Every store this machine holds, as scopes a session can name. Nothing is configured: a
        store declares its name in its policy or takes it from its directory, and stores are looked
        for where the conventions put them — the project's own (`project`), the machine's global one
        (`global`), the stores in the user data directory, and the `<name>-enact` directories beside
        the project, which is where a store lives when kept out of the project it serves (03 §9.1).

        Order matters: the first entry for a root keeps it, later ones only add aliases, so the
        project's store answers to `project` as well as to its own name.

        Walked once and held. This is the method the object exists for."""
        if self._scopes is None:
            self._scopes = self._walk()
        return self._scopes

    def _walk(self) -> List[Scope]:
        cwd = Path(self.where.cwd).resolve()
        found: List[Scope] = []

        def add(root: Optional[Path], how: str, name: Optional[str] = None) -> None:
            if root is None or not is_store(root):
                return
            root = root.resolve()
            called = (name or store_name(root)).lower()
            for scope in found:
                if scope.root == root:
                    if called != scope.name and called not in scope.aliases:
                        scope.aliases.append(called)
                    return
                if called == scope.name:        # two stores of one name: the second keeps its path
                    called = f"{called}@{root.parent.name}"
            found.append(Scope(called, root, how, []))

        here = self.find()
        if here is not None:
            add(here.root, here.how, "project")
            add(here.root, here.how)             # and by its own name
        add(self.global_root(), "global store", "global")
        if self.where.user_data.is_dir():
            for child in sorted(self.where.user_data.iterdir()):
                add(child, f"in {self.where.user_data}")
        project = project_root(cwd)
        siblings = ({s for suffix in SUFFIXES for s in project.parent.glob("*" + suffix)}
                    if project.parent.is_dir() else set())
        for sibling in sorted(siblings):
            add(sibling, f"beside {project.name}")
        return found

    def named(self, name: str) -> Optional[Scope]:
        """The store a session means by *name*, matched on its name or an alias."""
        wanted = name.strip().lower()
        for s in self.scopes():
            if wanted == s.name or wanted in s.aliases:
                return s
        return None

    # ── making one ────────────────────────────────────────────────────────

    def setup_global(self, source: Optional[Path] = None, path: Optional[Path] = None) -> Path:
        """The machine's global store: created under the user data directory unless *path* says
        otherwise, marked personal so every write is committed, and recorded in the user's git
        config so every project on this machine finds it."""
        root = self.where.path(str(path or self.where.user_data / GLOBAL_NAME)).resolve()
        if not is_store(root):
            create(root, source)
        if not (root / POLICY).is_file():
            write_policy(root, Policy(sharing="personal", push="auto"))
        if git(["rev-parse", "--git-dir"], root) is None:
            git(["init", "-q"], root)
        git(["config", "--global", GLOBAL_KEY, str(root)], root)
        publish(root, "memory: the global store, as initialised by oo enact setup --global")
        self.refresh()
        return root

    def setup_user(self, name: Optional[str] = None, source: Optional[Path] = None) -> Path:
        """A store under the user data directory, named for the project, and git pointed at it."""
        cwd = Path(self.where.cwd)
        root = self.where.user_data / (name or project_name(cwd))
        if not is_store(root):
            create(root, source)
        point_git_at(root, cwd)
        self.refresh()
        return root

    def setup_local(self, source: Optional[Path] = None) -> Path:
        """A store inside the project, as ``.enact`` at this worktree's top, committed with the
        code. Git config is left alone: this store belongs to the branch that carries it, and other
        worktrees on other branches find theirs, or none."""
        root = project_root(Path(self.where.cwd)) / LOCAL
        if not is_store(root):
            create(root, source)
        self.refresh()
        return root

    def setup_branch(self, path: Optional[Path] = None, source: Optional[Path] = None) -> Path:
        """A store on the orphan ``enact`` branch, checked out as its own worktree beside the main
        one. An existing branch under any name the system has had is checked out rather than created
        — a second machine gets the project's memory with ``git fetch`` and this call."""
        cwd = Path(self.where.cwd)
        main = main_worktree(cwd)
        if main is None:
            raise RuntimeError(f"{cwd} is not in a git repository — use `oo enact setup --user` instead")
        branch = next((b for b in BRANCHES
                       if git(["show-ref", "--verify", "--quiet", f"refs/heads/{b}"], cwd) is not None
                       or git(["show-ref", "--verify", "--quiet", f"refs/remotes/origin/{b}"], cwd) is not None),
                      BRANCH)
        root = self.where.path(str(path or main.parent / f"{main.name}-{branch}")).resolve()
        self.refresh()
        if is_store(root):
            point_git_at(root, cwd)
            return root
        if root.exists() and any(root.iterdir()):
            raise RuntimeError(f"{root} exists and is not an ENACT store — choose another --path")
        has_local = git(["show-ref", "--verify", "--quiet", f"refs/heads/{branch}"], cwd) is not None
        has_remote = git(["show-ref", "--verify", "--quiet", f"refs/remotes/origin/{branch}"], cwd) is not None
        if has_local:
            _require(git(["worktree", "add", str(root), branch], cwd), f"git worktree add {root} {branch}")
        elif has_remote:
            _require(git(["worktree", "add", "--track", "-b", branch, str(root), f"origin/{branch}"], cwd),
                     f"git worktree add --track -b {branch} {root} origin/{branch}")
        else:
            _require(git(["worktree", "add", "--orphan", "-b", branch, str(root)], cwd),
                     f"git worktree add --orphan -b {branch} {root}")
            create(root, source)
            git(["add", "-A"], root)
            if git(["commit", "-m", "memory: the store, as initialised by oo enact setup"], root) is None:
                raise RuntimeError(f"created the store in {root}, but could not commit it — commit it there yourself")
        if not is_store(root):
            raise RuntimeError(f"the {branch} branch checked out in {root} holds no ENACT store")
        point_git_at(root, cwd)
        return root

    # ── asking the servers to reload ──────────────────────────────────────

    def ask_reload(self, send_signal: bool = False) -> dict:
        """Asks the servers on every store this machine holds to restart into the code on disk: a
        `local/reload` marker each server checks between messages, so it acts at a quiet moment and
        the host's connection survives.

        SIGHUP does the same for a server serving any other project, but only on request: a server
        older than this feature has no handler for it, and the default action for SIGHUP is to die."""
        signalled = []
        if send_signal and hasattr(signal, "SIGHUP"):
            for pid in server_pids():
                try:
                    os.kill(pid, signal.SIGHUP)
                    signalled.append(pid)
                except OSError:
                    continue
        marked = []
        for scope in self.scopes():      # every store here, not only this project's and the global one
            root = scope.root
            try:
                (root / "local").mkdir(exist_ok=True)
                (root / "local" / "reload").write_text("asked by oo enact reload\n", encoding="utf-8")
                marked.append(root)
            except OSError:
                continue
        return {"signalled": signalled, "marked": marked}









def remote_stores(root: Path) -> List[str]:
    """The branches of a store's remote — every store kept in that repository, including any this
    machine has not checked out yet. Empty when there is no remote or it cannot be reached."""
    url = git(["remote", "get-url", "origin"], root) or git(["remote", "get-url", "private"], root)
    if not url:
        return []
    listing = git(["ls-remote", "--heads", url], root)
    return sorted(line.rsplit("refs/heads/", 1)[-1] for line in (listing or "").splitlines() if "refs/heads/" in line)


# ── setup ─────────────────────────────────────────────────────────────────────








def project_name(cwd: Path) -> str:
    main = main_worktree(cwd)
    return (main or cwd).name


def point_git_at(root: Path, cwd: Path) -> bool:
    """Record *root* in the clone's shared git config; False outside git."""
    if git(["rev-parse", "--git-dir"], cwd) is None:
        return False
    return git(["config", GIT_KEY, str(root)], cwd) is not None


def create(root: Path, source: Optional[Path] = None) -> None:
    """A new store at *root*: empty, or a copy of the store at *source* — every file but ``local/``,
    which is one clone's alone. The copy is a new clone of the same history: its next write starts a
    new audit file, and the old ones keep replaying beside it."""
    from pygim.enact import Enact

    if source is None:
        Enact.init(str(root))
        return
    if not is_store(source):
        raise RuntimeError(f"{source} is not a memory store — nothing to copy")
    shutil.copytree(source, root, ignore=shutil.ignore_patterns("local"), dirs_exist_ok=True)
    (root / "local").mkdir(exist_ok=True)











def _require(result: Optional[str], what: str) -> None:
    if result is None:
        raise RuntimeError(f"`{what}` failed — run it yourself to see why")


# ── reloading a running server ────────────────────────────────────────────────


def server_pids() -> List[int]:
    """The `oo enact mcp` processes running for this user, however they were started."""
    try:
        done = subprocess.run(["ps", "-eo", "pid,args"], capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        return []
    out = []
    for line in done.stdout.splitlines()[1:]:
        pid, _, args = line.strip().partition(" ")
        if "memory" in args and " mcp" in args and pid.isdigit() and int(pid) != os.getpid():
            out.append(int(pid))
    return out





# ── registration ──────────────────────────────────────────────────────────────


def server_command() -> List[str]:
    """How to start this installation's server, with no root: it finds each project's store from the
    directory the host starts it in. Absolute, so the host needs nothing on its PATH."""
    scripts = Path(sys.executable).parent
    for candidate in (scripts / "oo", scripts / "oo.exe", scripts / "Scripts" / "oo.exe"):
        if candidate.is_file():
            return [str(candidate), "enact", "mcp"]
    return [sys.executable, "-c", "from pygim.__main__ import cli_oo; cli_oo()", "enact", "mcp"]


@dataclass(frozen=True)
class Registration:
    ran: bool          # True when `claude mcp add` ran here
    command: List[str]
    message: str


def register() -> Registration:
    """Registers the server with Claude Code at user scope, so every project and worktree gets it.
    Without the ``claude`` command on PATH, returns the command to run instead of guessing a config file."""
    command = ["claude", "mcp", "add", "--scope", "user", SERVER, "--", *server_command()]
    claude = shutil.which("claude")
    if claude is None:
        return Registration(False, command, "the `claude` command is not on PATH — run this where it is")
    if subprocess.run([claude, "mcp", "get", SERVER], capture_output=True, text=True).returncode == 0:
        return Registration(False, command, f"`{SERVER}` is already registered — `claude mcp remove {SERVER} -s user` to replace it")
    done = subprocess.run([claude, *command[1:]], capture_output=True, text=True)
    if done.returncode != 0:
        return Registration(False, command, "`claude mcp add` failed: " + (done.stderr or done.stdout).strip())
    return Registration(True, command, f"registered `{SERVER}` for every project at user scope")
