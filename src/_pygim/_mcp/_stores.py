"""Where a project's memory lives, and how a machine is set up to use it.

A store is found, in order, by:

1. an explicit root (``--root``);
2. ``$PYGIM_MEMORY_ROOT``;
3. ``git config pygim.memory`` — git keeps it in the clone's shared config, so every worktree of
   the project finds the same store;
4. a ``.memory`` directory found by walking up from the working directory.

``oo memory setup`` writes that git config, creates the store — in a user-level directory, or
on an orphan ``memory`` branch checked out as a worktree of its own — and registers the MCP
server with Claude Code at user scope, without a root, so it finds each project's store from the
directory it is started in. Nothing here imports click; the CLI and the MCP server both use it.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence

ENV = "PYGIM_MEMORY_ROOT"
GIT_KEY = "pygim.memory"
LOCAL = ".memory"
BRANCH = "memory"
SERVER = "pygim-memory"


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


def find(explicit: Optional[str] = None, cwd: Optional[Path] = None) -> Optional[Found]:
    """The store for *cwd*, by the order in the module docstring, or None when nothing names one.
    The first three are returned whether or not a store exists there yet — setup creates it."""
    cwd = Path(cwd or os.getcwd()).resolve()
    if explicit:
        return Found(Path(explicit).expanduser().resolve(), "--root")
    if os.environ.get(ENV):
        return Found(Path(os.environ[ENV]).expanduser().resolve(), "$" + ENV)
    configured = git(["config", "--get", GIT_KEY], cwd)
    if configured:
        path = Path(configured).expanduser()
        if not path.is_absolute():
            path = (main_worktree(cwd) or cwd) / path
        return Found(path.resolve(), "git config " + GIT_KEY)
    for directory in (cwd, *cwd.parents):
        if is_store(directory / LOCAL):
            return Found((directory / LOCAL).resolve(), LOCAL + " above the working directory")
    return None


def guidance(cwd: Optional[Path] = None) -> str:
    """What to tell someone whose project has no store yet."""
    where = find(cwd=cwd)
    if where and not where.exists:
        return f"{where.root} (from {where.how}) is not a memory store yet — run `oo memory setup` in the project"
    return ("no memory store for this project — run `oo memory setup --user` (a store in your user directory) "
            "or `oo memory setup --branch` (an orphan `memory` branch shared through git) in the project; "
            "`--local` keeps one inside the project instead")


# ── setup ─────────────────────────────────────────────────────────────────────


def user_data_dir() -> Path:
    """Where user-level stores live: the platform's per-user data directory, under pygim/memory."""
    if sys.platform == "win32":
        base = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local")
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share")
    return base / "pygim" / "memory"


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
    from pygim.memory import Memory

    if source is None:
        Memory.init(str(root))
        return
    if not is_store(source):
        raise RuntimeError(f"{source} is not a memory store — nothing to copy")
    shutil.copytree(source, root, ignore=shutil.ignore_patterns("local"), dirs_exist_ok=True)
    (root / "local").mkdir(exist_ok=True)


def setup_user(cwd: Path, name: Optional[str] = None, source: Optional[Path] = None) -> Path:
    """A store under the user data directory, named for the project, and git pointed at it."""
    root = user_data_dir() / (name or project_name(cwd))
    if not is_store(root):
        create(root, source)
    point_git_at(root, cwd)
    return root


def setup_local(cwd: Path, source: Optional[Path] = None) -> Path:
    """A store inside the project, as ``.memory`` at this worktree's top, committed with the code. Git
    config is left alone: this store belongs to the branch that carries it, and other worktrees on
    other branches find theirs, or none."""
    root = project_root(cwd) / LOCAL
    if not is_store(root):
        create(root, source)
    return root


def setup_branch(cwd: Path, path: Optional[Path] = None, source: Optional[Path] = None) -> Path:
    """A store on the orphan ``memory`` branch, checked out as its own worktree beside the main one.
    An existing local or remote-tracking ``memory`` branch is checked out rather than created — a
    second machine gets the project's memory with ``git fetch`` and this call."""
    main = main_worktree(cwd)
    if main is None:
        raise RuntimeError(f"{cwd} is not in a git repository — use `oo memory setup --user` instead")
    root = (path or main.parent / f"{main.name}-{BRANCH}").expanduser().resolve()
    if is_store(root):
        point_git_at(root, cwd)
        return root
    if root.exists() and any(root.iterdir()):
        raise RuntimeError(f"{root} exists and is not a memory store — choose another --path")
    has_local = git(["show-ref", "--verify", "--quiet", f"refs/heads/{BRANCH}"], cwd) is not None
    has_remote = git(["show-ref", "--verify", "--quiet", f"refs/remotes/origin/{BRANCH}"], cwd) is not None
    if has_local:
        _require(git(["worktree", "add", str(root), BRANCH], cwd), f"git worktree add {root} {BRANCH}")
    elif has_remote:
        _require(git(["worktree", "add", "--track", "-b", BRANCH, str(root), f"origin/{BRANCH}"], cwd),
                 f"git worktree add --track -b {BRANCH} {root} origin/{BRANCH}")
    else:
        _require(git(["worktree", "add", "--orphan", "-b", BRANCH, str(root)], cwd),
                 f"git worktree add --orphan -b {BRANCH} {root}")
        create(root, source)
        git(["add", "-A"], root)
        if git(["commit", "-m", "memory: the store, as initialised by oo memory setup"], root) is None:
            raise RuntimeError(f"created the store in {root}, but could not commit it — commit it there yourself")
    if not is_store(root):
        raise RuntimeError(f"the {BRANCH} branch checked out in {root} holds no memory store")
    point_git_at(root, cwd)
    return root


def _require(result: Optional[str], what: str) -> None:
    if result is None:
        raise RuntimeError(f"`{what}` failed — run it yourself to see why")


# ── registration ──────────────────────────────────────────────────────────────


def server_command() -> List[str]:
    """How to start this installation's server, with no root: it finds each project's store from the
    directory the host starts it in. Absolute, so the host needs nothing on its PATH."""
    scripts = Path(sys.executable).parent
    for candidate in (scripts / "oo", scripts / "oo.exe", scripts / "Scripts" / "oo.exe"):
        if candidate.is_file():
            return [str(candidate), "memory", "mcp"]
    return [sys.executable, "-c", "from pygim.__main__ import cli_oo; cli_oo()", "memory", "mcp"]


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
