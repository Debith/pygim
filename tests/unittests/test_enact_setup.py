"""Where a project's memory lives — found from every worktree, set up on a new machine — and the
prompts and tools that give a new project its vocabulary.

Every git repository here is a throwaway under tmp_path, with its own identity and no global or
system git config, so nothing on the machine running the tests is read or written.
"""
from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path

import pytest
from click.testing import CliRunner

from _pygim._cli import _style
from _pygim import _config
from _pygim._mcp import _packs, _stores
from _pygim._mcp.enact import PROMPTS, build
from pygim.__main__ import cli_oo
from pygim.enact import Enact, digest

PACK = """\
pack: proj
entry:
  brief: The proj shop front.
  when: About building the shop front.
  when_not: Not another project.
  example: Why the basket keeps items for a week.
dimensions:
  area:
    role: soft
    weight: 1.0
    entry: {brief: Which part of the shop., full: The part of the shop front the knowledge is about., when: The knowledge holds for one part., when_not: Not the kind of thing made — that is artifact., example: The checkout.}
    values:
      basket: {entry: {brief: Holding items before paying., when: About the basket., when_not: Not paying — that is checkout., example: Adding an item.}}
      checkout: {entry: {brief: Paying., when: About payment., when_not: Not the basket., example: Paying by card.}}
extends:
  artifact:
    page: {entry: {brief: A screen a shopper sees., when: About one screen., when_not: Not a shared component., example: The basket screen.}}
"""


def sh(*args: str, cwd: Path) -> str:
    return subprocess.run(args, cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def git_at_least(major: int, minor: int) -> bool:
    found = re.search(r"(\d+)\.(\d+)", sh("git", "--version", cwd=Path.cwd()))
    return bool(found) and (int(found.group(1)), int(found.group(2))) >= (major, minor)


def call(server, name, **arguments):
    """One tool, over the protocol. A write gets a valid card unless the test names its own: these
    tests are about stores and scopes, and the template has tests of its own."""
    if name in ("remember", "merge") and "when" not in arguments:
        arguments = {"when": "In the case this test sets up.", "why": "The test needs a memory the store will accept.",
                     **arguments}
    """One tool call, as the MCP host makes it: (is_error, the decoded result or the error text)."""
    resp = server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                          "params": {"name": name, "arguments": arguments}})
    result = resp["result"]
    return result["isError"], (result["content"][0]["text"] if result["isError"] else json.loads(result["content"][0]["text"]))


@pytest.fixture
def isolated(tmp_path, monkeypatch):
    """Nothing about pygim is patched here. What is set is what a *subprocess* reads for itself:
    git's identity and configuration, and — for the tests that run the real command, whose
    composition root reads the environment exactly as a person's shell gives it — where this
    machine's stores would be."""
    for key, value in {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.com",
                       "GIT_COMMITTER_NAME": "Test", "GIT_COMMITTER_EMAIL": "test@example.com",
                       "GIT_CONFIG_GLOBAL": str(tmp_path / "gitconfig"), "GIT_CONFIG_NOSYSTEM": "1",
                       "HOME": str(tmp_path / "home"), "XDG_DATA_HOME": str(tmp_path),
                       "PYGIM_ENACT_GLOBAL": str(tmp_path / "no-global-store")}.items():
        monkeypatch.setenv(key, value)
    (tmp_path / "home").mkdir(exist_ok=True)
    return tmp_path


def at(cwd, isolated, **overrides):
    """The configuration, as a value. Production fills this same object from the environment at its
    composition root (`_config.from_process`); a test fills it with plain values. Everything below
    the root — discovery, the server, the store — is then the very same code, not a variant of it,
    and no production function has to be replaced for a test to say where to look."""
    return _config.Environment(cwd=Path(cwd), home=isolated / "home",
                               user_data=isolated / "pygim" / "enact", **overrides)


def machine(cwd, isolated, **overrides):
    """The machine's stores, built from that configuration by the same class production builds —
    `_stores.Stores`. A test names where to look; nothing else about it differs."""
    return _stores.Stores(at(cwd, isolated, **overrides))


@pytest.fixture
def project(isolated):
    repo = isolated / "proj"
    repo.mkdir()
    sh("git", "init", "-q", "-b", "main", cwd=repo)
    (repo / "README.md").write_text("# Proj\n\nA shop front.\nThe basket keeps items for a week.\n", encoding="utf-8")
    sh("git", "add", "-A", cwd=repo)
    sh("git", "commit", "-qm", "init", cwd=repo)
    sh("git", "worktree", "add", "-q", "-b", "feature", str(isolated / "proj-feature"), cwd=repo)
    return repo


class TestFinding:
    def test_the_order_is_flag_environment_git_config_then_dot_memory(self, project, isolated, monkeypatch):
        feature = isolated / "proj-feature"
        assert machine(feature, isolated).find() is None
        Enact.init(str(project / ".enact"))
        assert machine(project, isolated).find().how.startswith(".enact")
        assert machine(feature, isolated).find() is None                        # an untracked .memory is one worktree's only
        shared = isolated / "shared"
        sh("git", "config", _stores.GIT_KEY, str(shared), cwd=project)
        found = machine(feature, isolated).find()
        assert found.root == shared.resolve() and found.how == "git config pygim.enact"
        # The remaining two are decided before `find` ever runs: the environment and the command
        # line are read once, by the composition root, and arrive as values.
        from_env = _config.read({_config.ROOT: str(isolated / "from-env")}, cwd=feature,
                                home=isolated / "home", platform="linux")
        assert _stores.Stores(from_env).find().how == "$PYGIM_ENACT_ROOT"
        assert _stores.Stores(from_env.with_root(str(isolated / "flag"))).find().how == "--root"     # a flag wins

    def test_standing_in_a_store_finds_that_store(self, project, isolated):
        """A store's own worktree is where `oo enact accept --pack` is run from; before this, a
        command there said the project had no store at all."""
        store = isolated / "dnd-enact"
        _stores.create(store)
        found = machine(store, isolated).find()
        assert found is not None and found.root == store.resolve() and found.exists
        assert "working directory is a store" in found.how
        inside = store / "taxonomy" / "studies"
        inside.mkdir(parents=True, exist_ok=True)
        assert machine(inside, isolated).find().root == store.resolve()          # and from anywhere inside it
        assert machine(project, isolated).find() is None                          # a project with no store still has none

    def test_a_relative_git_config_is_relative_to_the_main_worktree(self, project, isolated):
        sh("git", "config", _stores.GIT_KEY, "../shared", cwd=project)
        assert machine(isolated / "proj-feature", isolated).find().root == (isolated / "shared").resolve()


class TestTheNameItHadBefore:
    """The system was called `memory` until 2026-09-22 and the stores on this machine still say so
    in their directory, their branch and the git config that points at them. Nothing was moved, so
    every lookup asks for each spelling it has had — and a store found under the old one is a store."""

    def test_a_dot_memory_directory_above_the_working_directory_is_still_a_store(self, project, isolated):
        Enact.init(str(project / ".memory"))
        found = machine(project, isolated).find()
        assert found is not None and found.root == (project / ".memory").resolve()
        assert found.how.startswith(".memory")

    def test_the_old_git_config_key_and_environment_variable_still_point_at_one(self, project, isolated, monkeypatch):
        shared = isolated / "shared"
        sh("git", "config", "pygim.memory", str(shared), cwd=project)
        assert machine(project, isolated).find().how == "git config pygim.memory"
        older = _config.read({"PYGIM_MEMORY_ROOT": str(isolated / "from-env")}, cwd=project,
                             home=isolated / "home", platform="linux")
        assert _stores.Stores(older).find().how == "$PYGIM_MEMORY_ROOT"

    def test_a_sibling_under_either_suffix_is_discovered_and_named_without_it(self, project, isolated):
        _stores.create(isolated / "proj-memory")
        _stores.create(isolated / "ddd-enact")
        names = {s.name for s in machine(project, isolated).scopes()}
        assert {"proj", "ddd"} <= names, names


class TestSetup:
    def test_a_user_store_is_found_from_every_worktree(self, project, isolated):
        root = machine(project, isolated).setup_user()
        assert root == isolated / "pygim" / "enact" / "proj" and _stores.is_store(root)
        assert machine(isolated / "proj-feature", isolated).find().root == root.resolve()
        assert machine(project, isolated).setup_user() == root                      # a second run changes nothing

    @pytest.mark.skipif(not git_at_least(2, 42), reason="git worktree add --orphan needs git 2.42")
    def test_a_branch_store_is_an_orphan_worktree_shared_through_git(self, project, isolated):
        root = machine(project, isolated).setup_branch()
        assert root == (isolated / "proj-enact").resolve() and _stores.is_store(root)
        assert sh("git", "rev-list", "--count", _stores.BRANCH, cwd=project) == "1"
        with pytest.raises(subprocess.CalledProcessError):             # shares no history with the code
            sh("git", "merge-base", "main", _stores.BRANCH, cwd=project)
        assert machine(isolated / "proj-feature", isolated).find().root == root
        assert machine(project, isolated).setup_branch() == root

    @pytest.mark.skipif(not git_at_least(2, 42), reason="git worktree add --orphan needs git 2.42")
    def test_another_clone_joins_the_existing_memory_branch(self, project, isolated):
        first = machine(project, isolated).setup_branch()
        other = isolated / "elsewhere"
        sh("git", "clone", "-q", str(project), str(other), cwd=isolated)
        joined = machine(other, isolated).setup_branch(isolated / "elsewhere-enact")
        assert _stores.is_store(joined)
        assert (joined / "taxonomy" / "base.yaml").read_bytes() == (first / "taxonomy" / "base.yaml").read_bytes()

    @pytest.mark.skipif(not git_at_least(2, 42), reason="git worktree add --orphan needs git 2.42")
    def test_an_existing_store_moves_to_the_branch_with_its_history(self, project, isolated):
        old = project / ".enact"
        Enact.init(str(old))
        before = Enact(str(old))
        before.remember(title="Kept", text="This memory moves with the store.", tags=["domain=any", "artifact=any", "task=design"])
        root = machine(project, isolated).setup_branch(source=old)
        moved = Enact(str(root))
        assert [w["title"] for w in moved.review(0)["written"]] == ["Kept"]
        assert not (root / "local" / "clone").exists() or (root / "local" / "clone").read_text() != (old / "local" / "clone").read_text()
        assert sh("git", "status", "--porcelain", cwd=root) == ""                    # committed, local/ ignored

    def test_a_local_store_is_the_project_s_own_and_leaves_git_config_alone(self, project, isolated):
        root = machine(project, isolated).setup_local()
        assert root == project / ".enact" and _stores.is_store(root)
        assert machine(project, isolated).find().how.startswith(".enact")
        assert _stores.git(["config", "--get", _stores.GIT_KEY], project) is None
        assert machine(isolated / "proj-feature", isolated).find() is None       # another branch's worktree has its own, or none

    def test_one_command_per_job(self, project, isolated, monkeypatch):
        for gone in ("init", "accept-pack"):
            out = CliRunner().invoke(cli_oo, ["enact", gone])
            assert out.exit_code != 0 and "No such command" in out.output, gone
        Enact.init(str(project / ".enact"))
        monkeypatch.chdir(project)
        nothing = CliRunner().invoke(cli_oo, ["enact", "accept"])       # no argument asks, it does not fail
        assert nothing.exit_code == 0 and "nothing is waiting" in nothing.output
        both = CliRunner().invoke(cli_oo, ["enact", "accept", "abcdef123456", "--pack", str(project / "README.md")])
        assert both.exit_code != 0 and "accept one thing" in both.output

    def test_outside_git_a_branch_store_is_refused_with_the_alternative(self, isolated):
        plain = isolated / "plain"
        plain.mkdir()
        with pytest.raises(RuntimeError, match="setup --user"):
            machine(plain, isolated).setup_branch()

    def test_without_claude_on_path_registration_prints_the_command(self, monkeypatch):
        monkeypatch.setattr(_stores.shutil, "which", lambda name: None)
        r = _stores.register()
        assert not r.ran and r.command[:6] == ["claude", "mcp", "add", "--scope", "user", _stores.SERVER]
        assert r.command[-2:] == ["enact", "mcp"] and "--root" not in r.command

    def test_the_command(self, project, isolated, monkeypatch):
        monkeypatch.chdir(isolated / "proj-feature")
        out = CliRunner().invoke(cli_oo, ["enact", "setup", "--user", "--no-register"])
        assert out.exit_code == 0, out.output
        assert "store: " in out.output and "every worktree" in out.output


class TestTheServerWithoutAStore:
    def test_it_starts_and_says_how_to_set_up(self, isolated):
        server = build(at(isolated, isolated))
        assert server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]
        result = server.call("vocabulary", {})
        assert result["isError"] and "oo enact setup" in result["content"][0]["text"]
        listed = server.handle({"jsonrpc": "2.0", "id": 2, "method": "prompts/list"})["result"]["prompts"]
        assert [p["name"] for p in listed] == ["consolidate", "prepare-vocabulary", "seed-memories"]

    def test_it_finds_a_store_created_after_it_started(self, isolated):
        server = build(at(isolated, isolated))
        Enact.init(str(isolated / ".enact"))
        result = server.call("session", {})
        assert not result["isError"] and json.loads(result["content"][0]["text"])["root"] == str((isolated / ".enact").resolve())


class TestTheGlobalStore:
    """One store per machine for knowledge about no single project, read by every project's
    sessions, and published the moment it is written (03 §9.1)."""

    PREF = dict(tags=["domain=any", "artifact=any", "task=any", "kind=preference"])

    @staticmethod
    def with_project(project):
        store = project / ".enact"
        Enact.init(str(store))
        return store

    def test_setup_makes_it_personal_published_and_found_from_any_project(self, project, isolated):
        out = CliRunner().invoke(cli_oo, ["enact", "setup", "--global"])
        assert out.exit_code == 0, out.output
        root = isolated / "pygim" / "enact" / "global"
        assert _stores.is_store(root) and "sharing: personal" in out.output
        assert _stores.policy(root) == _stores.Policy(sharing="personal", push="auto")
        assert sh("git", "log", "--oneline", "-1", cwd=root)                       # committed as it was made
        assert machine(project, isolated).global_root() == root                                       # through git's global config
        assert machine(project, isolated).find() is None or machine(project, isolated).find().root != root   # not the project's store

    def test_a_write_with_scope_global_lands_there_and_is_committed(self, project, isolated, monkeypatch):
        store = self.with_project(project)
        wide = machine(project, isolated).setup_global()
        monkeypatch.setenv(_config.GLOBAL, str(wide))
        server = build(at(project, isolated))
        err, written = call(server, "remember", scope="global", title="Explain in layers",
                            text="Assumed words first, then one line.", **self.PREF)
        assert not err and written["ok"] and written["store"] == "global"
        assert written["synced"] == "committed (no remote yet)"
        assert "Explain in layers" in sh("git", "log", "-1", "--pretty=%s", cwd=wide)
        assert Enact(str(wide)).session()["memories"] == 1
        err, here = call(server, "remember", title="A project rule", text="Only here.", **self.PREF)
        assert here["ok"] and "store" not in here and Enact(str(store)).session()["memories"] == 1
        assert sh("git", "status", "--porcelain", cwd=wide) == ""                  # nothing left uncommitted

    def test_both_stores_reach_the_session_and_the_global_ones_say_so(self, project, isolated, monkeypatch):
        store = self.with_project(project)
        wide = machine(project, isolated).setup_global()
        monkeypatch.setenv(_config.GLOBAL, str(wide))
        Enact(str(wide)).remember(title="Explain in layers", text="Assumed words first.", **self.PREF)
        Enact(str(store)).remember(title="Prefer templates", text="Template it.", **self.PREF)
        server = build(at(project, isolated))
        text = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]["instructions"]
        assert "(global) Explain in layers" in text and "Prefer templates" in text and "(global) Prefer templates" not in text
        standing = call(server, "session")[1]["standing"]
        assert [(p["scope"], p["title"]) for p in standing["preferences"]] == [
            ("global", "Explain in layers"), ("project", "Prefer templates")]
        assert "Assumed words first." in standing["preferences"][0]["card"]        # each arrives as a card
        assert "the project's is the rule" in standing["note"]                     # global first, the nearer rule last
        monkeypatch.chdir(project)
        out = CliRunner().invoke(cli_oo, ["enact", "status", "--standing"])
        assert out.exit_code == 0 and "(global) Explain in layers" in out.output and "Template it." in out.output

    def test_a_preference_written_elsewhere_reaches_a_session_already_running(self, project, isolated, monkeypatch):
        self.with_project(project)
        wide = machine(project, isolated).setup_global()
        monkeypatch.setenv(_config.GLOBAL, str(wide))
        server = build(at(project, isolated))
        call(server, "session")                                                     # the baseline it will compare against
        Enact(str(wide)).remember(title="Lay options out as a table", text="With a recommendation.", **self.PREF)
        err, info = call(server, "session")
        assert info["standing_changed"]["added"] == ["Lay options out as a table"]
        assert "no_longer" in info["standing_changed"] and info["standing_changed"]["no_longer"] == []
        assert "standing_changed" not in call(server, "session")[1]                 # said once, not on every call

    def test_a_store_whose_owners_publish_it_is_never_pushed_here(self, project, isolated):
        shared = isolated / "community"
        _stores.create(shared)
        _stores.write_policy(shared, _stores.Policy(sharing="community", push="manual"))
        sh("git", "init", "-q", cwd=shared)
        assert _stores.publish(shared, "memory: something").startswith("manual")
        assert _stores.git(["rev-parse", "--verify", "HEAD"], shared) is None       # no commit made on our say-so

    def test_without_a_global_store_the_scope_says_how_to_make_one(self, project, isolated, monkeypatch):
        self.with_project(project)
        monkeypatch.delenv(_config.GLOBAL, raising=False)
        err, text = call(build(at(project, isolated)), "remember", scope="global", title="t", text="x", **self.PREF)
        assert err and "oo enact setup --global" in text


class TestDiscoveringStores:
    """A machine's stores are found, not configured: a session names any of them with `scope`."""

    def test_a_store_beside_the_project_is_a_scope_under_its_own_name(self, project, isolated, monkeypatch):
        Enact.init(str(project / ".enact"))
        subject = isolated / "ddd-memory"                       # the convention: <name>-memory beside the project
        _stores.create(subject)
        _stores.write_policy(subject, _stores.Policy(sharing="community", push="manual"))
        named = {s.name: s for s in machine(project, isolated).scopes()}
        assert named["ddd"].root == subject.resolve() and "beside" in named["ddd"].how
        assert named["project"].root == (project / ".enact").resolve()
        Enact(str(subject)).remember(title="Value objects compare by value", text="And carry no identity.",
                                      tags=["domain=any", "artifact=any", "task=design", "kind=principle"])
        err, read = call(build(at(project, isolated)), "read", scope="ddd", hard=["task=design"])
        assert not err and [m["title"] for m in read["memories"]] == ["Value objects compare by value"]

    def test_a_policy_name_wins_over_the_directory(self, project, isolated):
        store = isolated / "D-D-2024-memory"
        _stores.create(store)
        _stores.write_policy(store, _stores.Policy(sharing="project", push="manual", name="dnd"))
        assert _stores.store_name(store) == "dnd"
        assert {s.name for s in machine(project, isolated).scopes()} >= {"dnd"}

    def test_session_lists_them_and_an_unknown_scope_names_what_there_is(self, project, isolated):
        Enact.init(str(project / ".enact"))
        _stores.create(isolated / "ddd-memory")
        server = build(at(project, isolated))
        err, info = call(server, "session")
        assert [s["scope"] for s in info["scopes"]] == ["project", "ddd"]
        assert info["scopes"][0]["also"] == ["proj"]                       # the project directory's own name
        err, text = call(server, "show", scope="nope", memory="#0")
        assert err and "no store called `nope`" in text and "project, ddd" in text

    def test_the_command_lists_stores_and_what_the_remote_holds(self, project, isolated, monkeypatch):
        Enact.init(str(project / ".enact"))
        _stores.create(isolated / "ddd-memory")
        monkeypatch.setattr(_stores, "remote_stores", lambda root: ["memory", "ddd", "global"])
        monkeypatch.chdir(project)
        out = CliRunner().invoke(cli_oo, ["enact", "stores", "--remote"])
        assert out.exit_code == 0, out.output
        assert "ddd" in out.output and "sharing: project" in out.output
        assert "not checked out here: memory, global" in out.output


class TestTheMailboxReachesASession:
    def test_the_command_posts_and_lists_and_the_hook_output_names_what_waits(self, project, isolated, monkeypatch):
        store = project / ".enact"
        Enact.init(str(store))
        monkeypatch.chdir(project)
        posted = CliRunner().invoke(cli_oo, ["enact", "mailbox", "--post", "Finish the rebase", "--kind", "request",
                                             "--to", "next-session"])
        assert posted.exit_code == 0 and "1 message(s) waiting" in posted.output
        listed = CliRunner().invoke(cli_oo, ["enact", "mailbox"])
        assert "request to next-session" in listed.output and "Finish the rebase" in listed.output
        standing = CliRunner().invoke(cli_oo, ["enact", "status", "--standing"])   # what the session-start hook prints
        assert "Waiting in the mailbox (1)" in standing.output and "Finish the rebase" in standing.output
        Enact(str(store)).post("Done.", resolves=Enact(str(store)).mailbox()[0]["id"], author="human")
        assert "nothing waiting" in CliRunner().invoke(cli_oo, ["enact", "mailbox"]).output


class TestHowTheOutputReads:
    """Colour is conditional output and never the only signal (global memory: glance value)."""

    def waiting(self, project):
        store = project / ".enact"
        Enact.init(str(store))
        m = Enact(str(store))
        tags = ["domain=any", "artifact=any", "task=design", "kind=principle"]
        a = m.remember(title="One case", text="First.", tags=tags)
        b = m.remember(title="Another case", text="Second.", tags=tags, seen=[a["memory"]])
        m.remember(title="What they share", text="The pattern.", tags=tags,
                   generalises=[a["memory"], b["memory"]], seen=[a["memory"], b["memory"]])
        return store

    def test_colour_marks_the_heading_and_the_count_and_never_stands_alone(self, project, monkeypatch):
        self.waiting(project)
        monkeypatch.delenv("NO_COLOR", raising=False)
        monkeypatch.delenv(_config.NO_COLOUR, raising=False)
        monkeypatch.setenv("TERM", "xterm")
        out = CliRunner().invoke(cli_oo, ["enact", "accept", "--root", str(project / ".enact")], color=True)
        assert out.exit_code == 0 and "\x1b[" in out.output
        assert "\x1b[1m1 waiting for you\x1b[0m" in out.output              # the count is bold, and it is a word too
        assert "What they share" in out.output                              # the title reads the same without colour

    def test_every_switch_that_must_turn_colour_off(self, project, monkeypatch):
        self.waiting(project)
        monkeypatch.setenv("TERM", "xterm")
        monkeypatch.delenv("NO_COLOR", raising=False)
        monkeypatch.delenv(_config.NO_COLOUR, raising=False)
        where = ["enact", "accept", "--root", str(project / ".enact")]
        assert "\x1b[" not in CliRunner().invoke(cli_oo, where).output                       # not a terminal
        assert "\x1b[" not in CliRunner().invoke(cli_oo, ["--no-color"] + where, color=True).output
        monkeypatch.setenv("NO_COLOR", "")                                                   # any value, even empty
        assert "\x1b[" not in CliRunner().invoke(cli_oo, where, color=True).output
        monkeypatch.delenv("NO_COLOR")
        monkeypatch.setenv("TERM", "dumb")
        assert "\x1b[" not in CliRunner().invoke(cli_oo, where, color=True).output


class TestAskingForAReload:
    def test_reload_marks_the_stores_a_server_here_would_serve(self, project, isolated, monkeypatch):
        store = project / ".enact"
        Enact.init(str(store))
        wide = machine(project, isolated).setup_global()
        monkeypatch.setenv(_config.GLOBAL, str(wide))
        def never(*_):                                                   # signals are opt-in: a server
            raise AssertionError("no signal without --signal")            # too old to handle SIGHUP dies of it
        monkeypatch.setattr(_stores, "server_pids", never)
        monkeypatch.chdir(project)
        out = CliRunner().invoke(cli_oo, ["enact", "reload"])
        assert out.exit_code == 0, out.output
        assert (store / "local" / "reload").is_file() and (wide / "local" / "reload").is_file()
        assert "reloads at its next call" in out.output
        monkeypatch.setattr(_stores, "server_pids", lambda: [])
        assert CliRunner().invoke(cli_oo, ["enact", "reload", "--signal"]).exit_code == 0


class TestANewProjectsVocabulary:
    def test_prepare_vocabulary_names_the_pack_and_the_person_s_step(self, project, isolated):
        server = build(at(project, isolated))
        got = server.handle({"jsonrpc": "2.0", "id": 1, "method": "prompts/get",
                             "params": {"name": "prepare-vocabulary", "arguments": {"domain": "Shop Front"}}})
        text = got["result"]["messages"][0]["content"]["text"]
        assert "proposal/pack-shop_front.yaml" in text and "oo enact accept --pack" in text and "`check_pack`" in text
        default = server.handle({"jsonrpc": "2.0", "id": 2, "method": "prompts/get", "params": {"name": "prepare-vocabulary"}})
        assert "pack-proj.yaml" in default["result"]["messages"][0]["content"]["text"]
        assert {p["name"] for p in PROMPTS} >= {"prepare-vocabulary", "seed-memories"}

    def test_cite_gives_the_passage_digest_and_the_inventory_entry(self, project, isolated):
        result = build(at(project, isolated)).call("cite", {"path": "README.md", "line": 4})
        cited = json.loads(result["content"][0]["text"])
        assert cited["text"] == "The basket keeps items for a week."
        assert cited["source"] == {"doc": "readme", "line": 4, "lines": 1, "passage": digest(cited["text"].encode("utf-8"))}
        assert cited["inventory"]["path"] == "README.md"
        assert cited["inventory"]["version"] == digest((project / "README.md").read_bytes().replace(b"\r\n", b"\n"))

    def test_cite_keeps_the_inventory_s_id_and_gives_a_locator_for_a_span(self, project, isolated):
        store = project / ".enact"
        Enact.init(str(store))
        (store / "sources").mkdir(exist_ok=True)
        (store / "sources" / "inventory.yaml").write_text('proj-readme:\n  kind: text\n  path: "README.md"\n', encoding="utf-8")
        cited = json.loads(build(at(project, isolated)).call("cite", {"path": "README.md", "line": 3, "lines": 2})["content"][0]["text"])
        assert cited["source"]["doc"] == "proj-readme" and cited["inventory"]["id"] == "proj-readme"
        assert cited["locator"] == "proj-readme:L3-4"

    def test_check_pack_warns_of_locators_that_do_not_hold(self, project, isolated):
        store = project / ".enact"
        Enact.init(str(store))
        (project / "GUIDE.md").write_text("Basket\nSome text.\nBasket\nCheckout\n", encoding="utf-8")
        basket = _packs.cite(project, "GUIDE.md", 1)["source"]
        checkout = dict(_packs.cite(project, "GUIDE.md", 4)["source"], passage=basket["passage"])  # a digest not of line 4
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        source = lambda s: "source: {doc: %s, line: %d, lines: 1, passage: %s}" % (s["doc"], s["line"], s["passage"])
        draft.write_text(PACK.replace("example: Adding an item.}}", "example: Adding an item.}, " + source(basket) + "}")
                             .replace("example: Paying by card.}}", "example: Paying by card.}, " + source(checkout) + "}"), encoding="utf-8")
        checked = json.loads(build(at(project, isolated)).call("check_pack", {"path": str(draft)})["content"][0]["text"])
        assert checked["ok"] and len(checked["warnings"]) == 2 and all("not in the inventory" in w for w in checked["warnings"])
        (draft.parent / "inventory.yaml").write_text("guide:\n  kind: text\n  path: GUIDE.md\n", encoding="utf-8")
        warnings = json.loads(build(at(project, isolated)).call("check_pack", {"path": str(draft)})["content"][0]["text"])["warnings"]
        assert any("area=basket" in w and "also occurs at L3" in w for w in warnings)
        assert any("area=checkout" in w and "not the cited passage" in w for w in warnings)

    def test_a_store_that_is_its_own_project_resolves_its_own_documents(self, project, isolated):
        """A knowledge store belongs to no checkout: its sources live inside it, so a citation is
        relative to the store even when the command runs in some other project."""
        store = isolated / "ddd-memory"
        _stores.create(store)
        (store / "sources").mkdir()
        (store / "sources" / "book.txt").write_text("Aggregates\nCluster the entities into aggregates.\n", encoding="utf-8")
        (store / "sources" / "inventory.yaml").write_text('book:\n  kind: text\n  path: "sources/book.txt"\n', encoding="utf-8")
        cited = _packs.cite(store, "sources/book.txt", 2, store=store)
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-ddd.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK.replace("pack: proj", "pack: ddd").replace(
            "example: Adding an item.}}",
            "example: Adding an item.}, source: {doc: %s, line: %d, lines: 1, passage: %s}}"
            % (cited["source"]["doc"], cited["source"]["line"], cited["source"]["passage"])), encoding="utf-8")
        checked = _packs.check(store, draft, project=project)      # the project is somewhere else entirely
        assert checked["ok"] and checked["warnings"] == []
        (store / "sources" / "book.txt").unlink()
        gone = _packs.check(store, draft, project=project)["warnings"]
        assert len(gone) == 1 and "not found under" in gone[0] and str(store) in gone[0]

    def test_a_store_beside_its_project_resolves_citations_into_it(self, project, isolated):
        """The store is kept out of the project it serves, so its citations point into a checkout it
        has to name — by its policy, or by the `<project>-memory` convention."""
        store = isolated / "proj-enact"
        _stores.create(store)
        assert _stores.project_of(store) == project.resolve()                  # the convention alone
        _stores.write_policy(store, _stores.Policy(name="proj", project="../proj"))
        assert _stores.project_of(store) == project.resolve()                  # and what it declares
        (store / "sources").mkdir(exist_ok=True)
        (store / "sources" / "inventory.yaml").write_text('readme:\n  kind: text\n  path: "README.md"\n', encoding="utf-8")
        cited = _packs.cite(project, "README.md", 4, store=store)
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK.replace(
            "example: Adding an item.}}",
            "example: Adding an item.}, source: {doc: %s, line: %d, lines: 1, passage: %s}}"
            % (cited["source"]["doc"], cited["source"]["line"], cited["source"]["passage"])), encoding="utf-8")
        checked = _packs.check(store, draft, project=store)     # run from inside the store, as one does
        assert checked["ok"] and checked["warnings"] == []

    def test_replacing_a_pack_is_refused_while_memories_carry_what_it_removes(self, project, isolated):
        store = project / ".enact"
        Enact.init(str(store))
        (store / "taxonomy" / "pack-proj.yaml").write_text(PACK, encoding="utf-8")
        memory = Enact(str(store))
        carrier = memory.remember(title="Checkout", text="Card only.", tags=["domain=proj", "artifact=page", "task=design",
                                                                            "kind=principle", "area=checkout"])
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK.replace("      checkout: {entry: {brief: Paying., when: About payment., when_not: Not the basket., example: Paying by card.}}\n", ""),
                         encoding="utf-8")
        checked = json.loads(build(at(project, isolated)).call("check_pack", {"path": str(draft)})["content"][0]["text"])
        assert checked["removed"] == [{"tag": "area=checkout", "carried_by": [f"{carrier['memory']} Checkout"]}]
        refused = _packs.accept(store, draft, replace=True)
        assert not refused["ok"] and "area=checkout" in refused["errors"] and "unlink" in refused["errors"]
        memory.unlink(carrier["memory"], "area=checkout", reason="the value goes")
        assert _packs.accept(store, draft, replace=True)["ok"]

    def test_windows_line_endings_cite_the_same_passage_and_version(self, project, isolated):
        lf = build(at(project, isolated)).call("cite", {"path": "README.md", "line": 4})
        (project / "README.md").write_bytes((project / "README.md").read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        crlf = build(at(project, isolated)).call("cite", {"path": "README.md", "line": 4})
        assert json.loads(crlf["content"][0]["text"]) == json.loads(lf["content"][0]["text"])

    def test_a_broken_draft_is_named_by_its_own_path(self, project, isolated):
        store = project / ".enact"
        Enact.init(str(store))
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK.replace("      checkout: {entry: {brief: Paying., when: About payment., when_not: Not the basket., example: Paying by card.}}\n",
                                      "      checkout: {entry: {brief: Paying.}}\n"), encoding="utf-8")
        checked = json.loads(build(at(project, isolated)).call("check_pack", {"path": str(draft.relative_to(store))})["content"][0]["text"])
        assert not checked["ok"] and str(draft) in checked["errors"] and "checkout" in checked["errors"]
        assert not (store / "taxonomy" / "pack-proj.yaml").exists()

    def test_an_accepted_pack_is_live_at_a_running_server_s_next_call(self, project, isolated):
        store = project / ".enact"
        Enact.init(str(store))
        server = build(at(project, isolated))
        assert "area" not in [d["name"] for d in json.loads(server.call("vocabulary", {})["content"][0]["text"])["dimensions"]]
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK, encoding="utf-8")
        (draft.parent / "inventory.yaml").write_text("readme:\n  kind: text\n  path: README.md\n  version: abc\n", encoding="utf-8")
        checked = json.loads(server.call("check_pack", {"path": str(draft)})["content"][0]["text"])
        assert checked["ok"] and checked["dimensions"] == ["area"]
        # every tag gained, wherever it lands — the old count said 2, missing the extension and the domain
        assert checked["adds"] == ["area=basket", "area=checkout", "artifact=page", "domain=proj"]
        assert checked["values"] == 4
        out = CliRunner().invoke(cli_oo, ["enact", "accept", "--pack", str(draft), "--root", str(store)])
        assert out.exit_code == 0, out.output
        dims = [d["name"] for d in json.loads(server.call("vocabulary", {})["content"][0]["text"])["dimensions"]]
        assert "area" in dims
        assert "readme:" in (store / "sources" / "inventory.yaml").read_text(encoding="utf-8")
        again = CliRunner().invoke(cli_oo, ["enact", "accept", "--pack", str(draft), "--root", str(store)])
        assert again.exit_code != 0 and "--replace" in again.output
        assert _packs.accept(store, draft, replace=True)["ok"]
