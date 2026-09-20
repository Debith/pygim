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

from _pygim._mcp import _packs, _stores
from _pygim._mcp.memory import PROMPTS, MemoryServer
from pygim.__main__ import cli_oo
from pygim.memory import Memory, digest

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
    """One tool call, as the MCP host makes it: (is_error, the decoded result or the error text)."""
    resp = server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                          "params": {"name": name, "arguments": arguments}})
    result = resp["result"]
    return result["isError"], (result["content"][0]["text"] if result["isError"] else json.loads(result["content"][0]["text"]))


@pytest.fixture
def isolated(tmp_path, monkeypatch):
    for key, value in {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.com",
                       "GIT_COMMITTER_NAME": "Test", "GIT_COMMITTER_EMAIL": "test@example.com",
                       "GIT_CONFIG_GLOBAL": str(tmp_path / "gitconfig"), "GIT_CONFIG_NOSYSTEM": "1"}.items():
        monkeypatch.setenv(key, value)
    monkeypatch.delenv(_stores.ENV, raising=False)
    monkeypatch.setattr(_stores, "user_data_dir", lambda: tmp_path / "user-data")
    return tmp_path


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
        assert _stores.find(cwd=feature) is None
        Memory.init(str(project / ".memory"))
        assert _stores.find(cwd=project).how.startswith(".memory")
        assert _stores.find(cwd=feature) is None                        # an untracked .memory is one worktree's only
        shared = isolated / "shared"
        sh("git", "config", _stores.GIT_KEY, str(shared), cwd=project)
        found = _stores.find(cwd=feature)
        assert found.root == shared.resolve() and found.how == "git config pygim.memory"
        monkeypatch.setenv(_stores.ENV, str(isolated / "from-env"))
        assert _stores.find(cwd=feature).how == "$PYGIM_MEMORY_ROOT"
        assert _stores.find(str(isolated / "flag"), cwd=feature).how == "--root"

    def test_a_relative_git_config_is_relative_to_the_main_worktree(self, project, isolated):
        sh("git", "config", _stores.GIT_KEY, "../shared", cwd=project)
        assert _stores.find(cwd=isolated / "proj-feature").root == (isolated / "shared").resolve()


class TestSetup:
    def test_a_user_store_is_found_from_every_worktree(self, project, isolated):
        root = _stores.setup_user(project)
        assert root == isolated / "user-data" / "proj" and _stores.is_store(root)
        assert _stores.find(cwd=isolated / "proj-feature").root == root.resolve()
        assert _stores.setup_user(project) == root                      # a second run changes nothing

    @pytest.mark.skipif(not git_at_least(2, 42), reason="git worktree add --orphan needs git 2.42")
    def test_a_branch_store_is_an_orphan_worktree_shared_through_git(self, project, isolated):
        root = _stores.setup_branch(project)
        assert root == (isolated / "proj-memory").resolve() and _stores.is_store(root)
        assert sh("git", "rev-list", "--count", _stores.BRANCH, cwd=project) == "1"
        with pytest.raises(subprocess.CalledProcessError):             # shares no history with the code
            sh("git", "merge-base", "main", _stores.BRANCH, cwd=project)
        assert _stores.find(cwd=isolated / "proj-feature").root == root
        assert _stores.setup_branch(project) == root

    @pytest.mark.skipif(not git_at_least(2, 42), reason="git worktree add --orphan needs git 2.42")
    def test_another_clone_joins_the_existing_memory_branch(self, project, isolated):
        first = _stores.setup_branch(project)
        other = isolated / "elsewhere"
        sh("git", "clone", "-q", str(project), str(other), cwd=isolated)
        joined = _stores.setup_branch(other, isolated / "elsewhere-memory")
        assert _stores.is_store(joined)
        assert (joined / "taxonomy" / "base.yaml").read_bytes() == (first / "taxonomy" / "base.yaml").read_bytes()

    @pytest.mark.skipif(not git_at_least(2, 42), reason="git worktree add --orphan needs git 2.42")
    def test_an_existing_store_moves_to_the_branch_with_its_history(self, project, isolated):
        old = project / ".memory"
        Memory.init(str(old))
        before = Memory(str(old))
        before.remember(title="Kept", text="This memory moves with the store.", tags=["domain=any", "artifact=any", "task=design"])
        root = _stores.setup_branch(project, source=old)
        moved = Memory(str(root))
        assert [w["title"] for w in moved.review(0)["written"]] == ["Kept"]
        assert not (root / "local" / "clone").exists() or (root / "local" / "clone").read_text() != (old / "local" / "clone").read_text()
        assert sh("git", "status", "--porcelain", cwd=root) == ""                    # committed, local/ ignored

    def test_a_local_store_is_the_project_s_own_and_leaves_git_config_alone(self, project, isolated):
        root = _stores.setup_local(project)
        assert root == project / ".memory" and _stores.is_store(root)
        assert _stores.find(cwd=project).how.startswith(".memory")
        assert _stores.git(["config", "--get", _stores.GIT_KEY], project) is None
        assert _stores.find(cwd=isolated / "proj-feature") is None       # another branch's worktree has its own, or none

    def test_one_command_per_job(self):
        for gone in ("init", "accept-pack"):
            out = CliRunner().invoke(cli_oo, ["memory", gone])
            assert out.exit_code != 0 and "No such command" in out.output, gone
        neither = CliRunner().invoke(cli_oo, ["memory", "accept"])
        assert neither.exit_code != 0 and "accept one thing" in neither.output

    def test_outside_git_a_branch_store_is_refused_with_the_alternative(self, isolated):
        plain = isolated / "plain"
        plain.mkdir()
        with pytest.raises(RuntimeError, match="setup --user"):
            _stores.setup_branch(plain)

    def test_without_claude_on_path_registration_prints_the_command(self, monkeypatch):
        monkeypatch.setattr(_stores.shutil, "which", lambda name: None)
        r = _stores.register()
        assert not r.ran and r.command[:6] == ["claude", "mcp", "add", "--scope", "user", _stores.SERVER]
        assert r.command[-2:] == ["memory", "mcp"] and "--root" not in r.command

    def test_the_command(self, project, isolated, monkeypatch):
        monkeypatch.chdir(isolated / "proj-feature")
        out = CliRunner().invoke(cli_oo, ["memory", "setup", "--user", "--no-register"])
        assert out.exit_code == 0, out.output
        assert "store: " in out.output and "every worktree" in out.output


class TestTheServerWithoutAStore:
    def test_it_starts_and_says_how_to_set_up(self, isolated):
        server = MemoryServer(cwd=isolated)
        assert server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]
        result = server.call("vocabulary", {})
        assert result["isError"] and "oo memory setup" in result["content"][0]["text"]
        listed = server.handle({"jsonrpc": "2.0", "id": 2, "method": "prompts/list"})["result"]["prompts"]
        assert [p["name"] for p in listed] == ["consolidate", "prepare-vocabulary", "seed-memories"]

    def test_it_finds_a_store_created_after_it_started(self, isolated):
        server = MemoryServer(cwd=isolated)
        Memory.init(str(isolated / ".memory"))
        result = server.call("session", {})
        assert not result["isError"] and json.loads(result["content"][0]["text"])["root"] == str((isolated / ".memory").resolve())


class TestTheGlobalStore:
    """One store per machine for knowledge about no single project, read by every project's
    sessions, and published the moment it is written (03 §9.1)."""

    PREF = dict(tags=["domain=any", "artifact=any", "task=any", "kind=preference"])

    @staticmethod
    def with_project(project):
        store = project / ".memory"
        Memory.init(str(store))
        return store

    def test_setup_makes_it_personal_published_and_found_from_any_project(self, project, isolated):
        out = CliRunner().invoke(cli_oo, ["memory", "setup", "--global"])
        assert out.exit_code == 0, out.output
        root = isolated / "user-data" / "global"
        assert _stores.is_store(root) and "sharing: personal" in out.output
        assert _stores.policy(root) == _stores.Policy(sharing="personal", push="auto")
        assert sh("git", "log", "--oneline", "-1", cwd=root)                       # committed as it was made
        assert _stores.find_global() == root                                       # through git's global config
        assert _stores.find(cwd=project) is None or _stores.find(cwd=project).root != root   # not the project's store

    def test_a_write_with_scope_global_lands_there_and_is_committed(self, project, isolated, monkeypatch):
        store = self.with_project(project)
        wide = _stores.setup_global()
        monkeypatch.setenv(_stores.GLOBAL_ENV, str(wide))
        server = MemoryServer(cwd=project)
        err, written = call(server, "remember", scope="global", title="Explain in layers",
                            text="Assumed words first, then one line.", **self.PREF)
        assert not err and written["ok"] and written["store"] == "global"
        assert written["synced"] == "committed (no remote yet)"
        assert "Explain in layers" in sh("git", "log", "-1", "--pretty=%s", cwd=wide)
        assert Memory(str(wide)).session()["memories"] == 1
        err, here = call(server, "remember", title="A project rule", text="Only here.", **self.PREF)
        assert here["ok"] and "store" not in here and Memory(str(store)).session()["memories"] == 1
        assert sh("git", "status", "--porcelain", cwd=wide) == ""                  # nothing left uncommitted

    def test_both_stores_reach_the_session_and_the_global_ones_say_so(self, project, isolated, monkeypatch):
        store = self.with_project(project)
        wide = _stores.setup_global()
        monkeypatch.setenv(_stores.GLOBAL_ENV, str(wide))
        Memory(str(wide)).remember(title="Explain in layers", text="Assumed words first.", **self.PREF)
        Memory(str(store)).remember(title="Prefer templates", text="Template it.", **self.PREF)
        text = MemoryServer(cwd=project).handle(
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})["result"]["instructions"]
        assert "(global) Explain in layers: Assumed words first." in text
        assert "Prefer templates: Template it." in text and "(global) Prefer templates" not in text
        assert "where the two disagree, this project's is the rule" in text

    def test_a_preference_written_elsewhere_reaches_a_session_already_running(self, project, isolated, monkeypatch):
        self.with_project(project)
        wide = _stores.setup_global()
        monkeypatch.setenv(_stores.GLOBAL_ENV, str(wide))
        server = MemoryServer(cwd=project)
        call(server, "session")                                                     # the baseline it will compare against
        Memory(str(wide)).remember(title="Lay options out as a table", text="With a recommendation.", **self.PREF)
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
        monkeypatch.delenv(_stores.GLOBAL_ENV, raising=False)
        err, text = call(MemoryServer(cwd=project), "remember", scope="global", title="t", text="x", **self.PREF)
        assert err and "oo memory setup --global" in text


class TestDiscoveringStores:
    """A machine's stores are found, not configured: a session names any of them with `scope`."""

    def test_a_store_beside_the_project_is_a_scope_under_its_own_name(self, project, isolated, monkeypatch):
        Memory.init(str(project / ".memory"))
        subject = isolated / "ddd-memory"                       # the convention: <name>-memory beside the project
        _stores.create(subject)
        _stores.write_policy(subject, _stores.Policy(sharing="community", push="manual"))
        named = {s.name: s for s in _stores.discover(project)}
        assert named["ddd"].root == subject.resolve() and "beside" in named["ddd"].how
        assert named["project"].root == (project / ".memory").resolve()
        Memory(str(subject)).remember(title="Value objects compare by value", text="And carry no identity.",
                                      tags=["domain=any", "artifact=any", "task=design", "kind=principle"])
        err, read = call(MemoryServer(cwd=project), "read", scope="ddd", hard=["task=design"])
        assert not err and [m["title"] for m in read["memories"]] == ["Value objects compare by value"]

    def test_a_policy_name_wins_over_the_directory(self, project, isolated):
        store = isolated / "D-D-2024-memory"
        _stores.create(store)
        _stores.write_policy(store, _stores.Policy(sharing="project", push="manual", name="dnd"))
        assert _stores.store_name(store) == "dnd"
        assert {s.name for s in _stores.discover(project)} >= {"dnd"}

    def test_session_lists_them_and_an_unknown_scope_names_what_there_is(self, project, isolated):
        Memory.init(str(project / ".memory"))
        _stores.create(isolated / "ddd-memory")
        server = MemoryServer(cwd=project)
        err, info = call(server, "session")
        assert [s["scope"] for s in info["scopes"]] == ["project", "ddd"]
        assert info["scopes"][0]["also"] == ["proj"]                       # the project directory's own name
        err, text = call(server, "show", scope="nope", memory="#0")
        assert err and "no store called `nope`" in text and "project, ddd" in text

    def test_the_command_lists_stores_and_what_the_remote_holds(self, project, isolated, monkeypatch):
        Memory.init(str(project / ".memory"))
        _stores.create(isolated / "ddd-memory")
        monkeypatch.setattr(_stores, "remote_stores", lambda root: ["memory", "ddd", "global"])
        monkeypatch.chdir(project)
        out = CliRunner().invoke(cli_oo, ["memory", "stores", "--remote"])
        assert out.exit_code == 0, out.output
        assert "ddd" in out.output and "sharing: project" in out.output
        assert "not checked out here: memory, global" in out.output


class TestAskingForAReload:
    def test_reload_marks_the_stores_a_server_here_would_serve(self, project, isolated, monkeypatch):
        store = project / ".memory"
        Memory.init(str(store))
        wide = _stores.setup_global()
        monkeypatch.setenv(_stores.GLOBAL_ENV, str(wide))
        def never(*_):                                                   # signals are opt-in: a server
            raise AssertionError("no signal without --signal")            # too old to handle SIGHUP dies of it
        monkeypatch.setattr(_stores, "server_pids", never)
        monkeypatch.chdir(project)
        out = CliRunner().invoke(cli_oo, ["memory", "reload"])
        assert out.exit_code == 0, out.output
        assert (store / "local" / "reload").is_file() and (wide / "local" / "reload").is_file()
        assert "reloads at its next call" in out.output
        monkeypatch.setattr(_stores, "server_pids", lambda: [])
        assert CliRunner().invoke(cli_oo, ["memory", "reload", "--signal"]).exit_code == 0


class TestANewProjectsVocabulary:
    def test_prepare_vocabulary_names_the_pack_and_the_person_s_step(self, project):
        server = MemoryServer(cwd=project)
        got = server.handle({"jsonrpc": "2.0", "id": 1, "method": "prompts/get",
                             "params": {"name": "prepare-vocabulary", "arguments": {"domain": "Shop Front"}}})
        text = got["result"]["messages"][0]["content"]["text"]
        assert "proposal/pack-shop_front.yaml" in text and "oo memory accept --pack" in text and "`check_pack`" in text
        default = server.handle({"jsonrpc": "2.0", "id": 2, "method": "prompts/get", "params": {"name": "prepare-vocabulary"}})
        assert "pack-proj.yaml" in default["result"]["messages"][0]["content"]["text"]
        assert {p["name"] for p in PROMPTS} >= {"prepare-vocabulary", "seed-memories"}

    def test_cite_gives_the_passage_digest_and_the_inventory_entry(self, project):
        result = MemoryServer(cwd=project).call("cite", {"path": "README.md", "line": 4})
        cited = json.loads(result["content"][0]["text"])
        assert cited["text"] == "The basket keeps items for a week."
        assert cited["source"] == {"doc": "readme", "line": 4, "lines": 1, "passage": digest(cited["text"].encode("utf-8"))}
        assert cited["inventory"]["path"] == "README.md"
        assert cited["inventory"]["version"] == digest((project / "README.md").read_bytes().replace(b"\r\n", b"\n"))

    def test_cite_keeps_the_inventory_s_id_and_gives_a_locator_for_a_span(self, project):
        store = project / ".memory"
        Memory.init(str(store))
        (store / "sources").mkdir(exist_ok=True)
        (store / "sources" / "inventory.yaml").write_text('proj-readme:\n  kind: text\n  path: "README.md"\n', encoding="utf-8")
        cited = json.loads(MemoryServer(cwd=project).call("cite", {"path": "README.md", "line": 3, "lines": 2})["content"][0]["text"])
        assert cited["source"]["doc"] == "proj-readme" and cited["inventory"]["id"] == "proj-readme"
        assert cited["locator"] == "proj-readme:L3-4"

    def test_check_pack_warns_of_locators_that_do_not_hold(self, project):
        store = project / ".memory"
        Memory.init(str(store))
        (project / "GUIDE.md").write_text("Basket\nSome text.\nBasket\nCheckout\n", encoding="utf-8")
        basket = _packs.cite(project, "GUIDE.md", 1)["source"]
        checkout = dict(_packs.cite(project, "GUIDE.md", 4)["source"], passage=basket["passage"])  # a digest not of line 4
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        source = lambda s: "source: {doc: %s, line: %d, lines: 1, passage: %s}" % (s["doc"], s["line"], s["passage"])
        draft.write_text(PACK.replace("example: Adding an item.}}", "example: Adding an item.}, " + source(basket) + "}")
                             .replace("example: Paying by card.}}", "example: Paying by card.}, " + source(checkout) + "}"), encoding="utf-8")
        checked = json.loads(MemoryServer(cwd=project).call("check_pack", {"path": str(draft)})["content"][0]["text"])
        assert checked["ok"] and len(checked["warnings"]) == 2 and all("not in the inventory" in w for w in checked["warnings"])
        (draft.parent / "inventory.yaml").write_text("guide:\n  kind: text\n  path: GUIDE.md\n", encoding="utf-8")
        warnings = json.loads(MemoryServer(cwd=project).call("check_pack", {"path": str(draft)})["content"][0]["text"])["warnings"]
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

    def test_replacing_a_pack_is_refused_while_memories_carry_what_it_removes(self, project):
        store = project / ".memory"
        Memory.init(str(store))
        (store / "taxonomy" / "pack-proj.yaml").write_text(PACK, encoding="utf-8")
        memory = Memory(str(store))
        carrier = memory.remember(title="Checkout", text="Card only.", tags=["domain=proj", "artifact=page", "task=design",
                                                                            "kind=principle", "area=checkout"])
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK.replace("      checkout: {entry: {brief: Paying., when: About payment., when_not: Not the basket., example: Paying by card.}}\n", ""),
                         encoding="utf-8")
        checked = json.loads(MemoryServer(cwd=project).call("check_pack", {"path": str(draft)})["content"][0]["text"])
        assert checked["removed"] == [{"tag": "area=checkout", "carried_by": [f"{carrier['memory']} Checkout"]}]
        refused = _packs.accept(store, draft, replace=True)
        assert not refused["ok"] and "area=checkout" in refused["errors"] and "unlink" in refused["errors"]
        memory.unlink(carrier["memory"], "area=checkout", reason="the value goes")
        assert _packs.accept(store, draft, replace=True)["ok"]

    def test_windows_line_endings_cite_the_same_passage_and_version(self, project):
        lf = MemoryServer(cwd=project).call("cite", {"path": "README.md", "line": 4})
        (project / "README.md").write_bytes((project / "README.md").read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        crlf = MemoryServer(cwd=project).call("cite", {"path": "README.md", "line": 4})
        assert json.loads(crlf["content"][0]["text"]) == json.loads(lf["content"][0]["text"])

    def test_a_broken_draft_is_named_by_its_own_path(self, project):
        store = project / ".memory"
        Memory.init(str(store))
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK.replace("      checkout: {entry: {brief: Paying., when: About payment., when_not: Not the basket., example: Paying by card.}}\n",
                                      "      checkout: {entry: {brief: Paying.}}\n"), encoding="utf-8")
        checked = json.loads(MemoryServer(cwd=project).call("check_pack", {"path": str(draft.relative_to(store))})["content"][0]["text"])
        assert not checked["ok"] and str(draft) in checked["errors"] and "checkout" in checked["errors"]
        assert not (store / "taxonomy" / "pack-proj.yaml").exists()

    def test_an_accepted_pack_is_live_at_a_running_server_s_next_call(self, project):
        store = project / ".memory"
        Memory.init(str(store))
        server = MemoryServer(cwd=project)
        assert "area" not in [d["name"] for d in json.loads(server.call("vocabulary", {})["content"][0]["text"])["dimensions"]]
        draft = store / "taxonomy" / "studies" / "s" / "proposal" / "pack-proj.yaml"
        draft.parent.mkdir(parents=True)
        draft.write_text(PACK, encoding="utf-8")
        (draft.parent / "inventory.yaml").write_text("readme:\n  kind: text\n  path: README.md\n  version: abc\n", encoding="utf-8")
        checked = json.loads(server.call("check_pack", {"path": str(draft)})["content"][0]["text"])
        assert checked["ok"] and checked["dimensions"] == ["area"] and checked["values"] == 2
        out = CliRunner().invoke(cli_oo, ["memory", "accept", "--pack", str(draft), "--root", str(store)])
        assert out.exit_code == 0, out.output
        dims = [d["name"] for d in json.loads(server.call("vocabulary", {})["content"][0]["text"])["dimensions"]]
        assert "area" in dims
        assert "readme:" in (store / "sources" / "inventory.yaml").read_text(encoding="utf-8")
        again = CliRunner().invoke(cli_oo, ["memory", "accept", "--pack", str(draft), "--root", str(store)])
        assert again.exit_code != 0 and "--replace" in again.output
        assert _packs.accept(store, draft, replace=True)["ok"]
