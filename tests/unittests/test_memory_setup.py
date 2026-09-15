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
