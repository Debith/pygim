"""Request guidance is packaged, store-owned, validated and delivered to agents."""
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest
import yaml

from pygim.enact import Enact, VocabularyError, digest
from pygim.pathlike import path


ACTIVITIES = {"design", "critique", "evaluate", "troubleshoot", "explain", "implement",
              "document", "discover", "research", "operate", "record", "plan"}


@pytest.fixture
def store(tmp_path):
    root = tmp_path / "memory"
    Enact.init(str(root))
    return root


def change(store, edit):
    path = store / "taxonomy" / "base.yaml"
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    edit(data)
    path.write_text(yaml.safe_dump(data, sort_keys=False, allow_unicode=True), encoding="utf-8")


def test_new_store_can_remember_and_retrieve_every_shared_activity(store):
    memory = Enact(str(store))
    vocab = memory.vocabulary(dimension="task")
    values = {v["tag"]: v for v in vocab["dimensions"][0]["values"]}
    assert set(values) == {"task=" + v for v in ACTIVITIES} | {"task=any"}
    seen = []
    for activity in sorted(ACTIVITIES):
        tags = ["domain=any", "artifact=any", "task=" + activity]
        made = memory.remember(title=activity, text="Knowledge for " + activity, tags=tags, seen=seen)
        assert made["ok"], made
        seen.append(made["memory"])
        assert memory.read(tags)["memories"][0]["memory"] == made["memory"]
        assert values["task=" + activity]["request"]["give_if"]


def test_request_guide_excludes_memory_facets_and_universal_or_retired_values(store):
    change(store, lambda d: d["dimensions"]["task"]["values"]["plan"].update(retired="Use the project workflow."))
    guide = Enact(str(store)).vocabulary(brief=True, request=True)["request"]
    assert "task=implement" in guide and "Give it if:" in guide and "Words:" in guide
    assert "task=any" not in guide and "task=plan" not in guide and "kind=reference" not in guide
    assert "activity tags can be soft" in guide and "conversation context" in guide
    assert "exactly one hard task and one hard artifact" in guide


def test_guide_read_patterns_keep_supporting_knowledge_and_place_a_procedure(store):
    memory = Enact(str(store))
    constraint = memory.remember(title="Design constraint", text="Preserve the agreed interface.",
        tags=["domain=any", "artifact=any", "task=design", "kind=principle"])
    procedure = memory.remember(title="Implement the change", text="1. Make the change.\n2. Verify the contract.",
        tags=["domain=any", "artifact=any", "task=implement", "kind=procedure"], seen=[constraint["memory"]])
    assert constraint["ok"] and procedure["ok"]
    broad = memory.read(["domain=any"], ["task=implement"])
    assert broad["ok"] and broad["procedure"] is None
    assert constraint["memory"] in {m["memory"] for m in broad["memories"]}
    focused = memory.read(["domain=any", "artifact=any", "task=implement"])
    assert focused["ok"] and focused["procedure"]["memory"] == procedure["memory"]


def test_explicit_store_rules_replace_packaged_questions_without_rewriting_files(store):
    def edit(d):
        d["dimensions"]["task"]["values"]["design"]["request"] = {
            "words": ["custom cue"], "give_if": "Does the custom condition hold?", "not_if": "Is it excluded here?"}
    change(store, edit)
    before = (store / "taxonomy" / "base.yaml").read_bytes()
    for _ in range(2):
        guide = Enact(str(store)).vocabulary(request=True)["request"]
        assert "custom cue" in guide and "Does the custom condition hold?" in guide
        assert "Does the request ask for something that does not exist yet" not in guide
    assert (store / "taxonomy" / "base.yaml").read_bytes() == before


def test_custom_value_without_request_rules_uses_its_own_codebook(store):
    def edit(d):
        d["dimensions"]["task"]["values"]["balance"] = {"entry": {
            "brief": "Weighing power.", "when": "Judging power for its cost.",
            "when_not": "Not inventing it.", "example": "Is it worth a spell slot?"}}
    change(store, edit)
    guide = Enact(str(store)).vocabulary(request=True)["request"]
    assert "task=balance" in guide and "Memory codebook — When: Judging power for its cost." in guide
    assert "Give it if: Judging power for its cost." not in guide


def test_disabled_or_unselected_request_dimensions_report_absence(store):
    assert "No request dimensions" in Enact(str(store)).vocabulary(dimension="kind", request=True)["request"]
    change(store, lambda d: d["dimensions"]["task"].update(request=False))
    guide = Enact(str(store)).vocabulary(request=True)["request"]
    assert "No request dimensions" in guide and "apply the decision questions" not in guide


def test_pack_can_offer_a_request_dimension_and_extend_task_with_its_own_rules(store):
    entry = {"brief": "Balancing encounter power.", "when": "Knowledge about power for its cost.",
             "when_not": "Not creating a new ability.", "example": "Compare two spell slots."}
    rule = {"words": ["power"], "give_if": "Is relative power being judged?", "not_if": "Is a new ability being created?"}
    pack = {"pack": "game", "entry": {**entry, "brief": "A game and its rules."},
            "dimensions": {"subject": {"role": "soft", "weight": 1, "request": True,
                "entry": {**entry, "full": "The topics a game request concerns."},
                "values": {"balance": {"entry": entry, "request": rule}}}},
            "extends": {"task": {"balance": {"entry": dict(entry), "request": {**rule, "words": ["power"]}}}}}
    (store / "taxonomy" / "pack-game.yaml").write_text(yaml.safe_dump(pack, sort_keys=False), encoding="utf-8")
    memory = Enact(str(store))
    guide = memory.vocabulary(request=True)["request"]
    assert "subject=balance" in guide and "task=balance" in guide
    assert guide.count("Give it if: Is relative power being judged?") == 2
    subject = memory.vocabulary(dimension="subject", request=True)
    assert "task=balance" not in subject["request"]
    assert subject["dimensions"][0]["values"][0]["request"] == rule


@pytest.mark.parametrize("rule, message", [
    ({"words": ["fix"], "give_if": "Broken?"}, "give_if and not_if"),
    ({"words": "fix", "give_if": "Broken?", "not_if": "Working?"}, "words must be a list"),
    ({"words": [""], "give_if": "Broken?", "not_if": "Working?"}, "nonempty text"),
    ({"words": ["\t"], "give_if": "Broken?", "not_if": "Working?"}, "nonempty text"),
    ({"give_if": "\t", "not_if": "Working?"}, "give_if and not_if"),
    ({"word": ["fix"], "give_if": "Broken?", "not_if": "Working?"}, 'unknown field "word"'),
    ("invalid", "give_if and not_if"),
])
def test_invalid_request_rules_fail_by_file_and_line_without_an_audit_write(store, rule, message):
    change(store, lambda d: d["dimensions"]["task"]["values"]["design"].update(request=rule))
    with pytest.raises(VocabularyError, match=message) as exc:
        Enact(str(store))
    assert "taxonomy/base.yaml:" in str(exc.value)
    assert not path(store / "audit").pathset("*.jsonl")


def test_invalid_dimension_request_marker_is_rejected(store):
    change(store, lambda d: d["dimensions"]["task"].update(request="sometimes"))
    with pytest.raises(VocabularyError, match="request must be true or false"):
        Enact(str(store))


@pytest.mark.parametrize("at_value", [False, True])
def test_misspelled_request_field_fails_instead_of_silently_dropping_guidance(store, at_value):
    def edit(data):
        owner = data["dimensions"]["task"]
        if at_value:
            owner = owner["values"]["design"]
        owner["requset"] = owner.pop("request")
    change(store, edit)
    with pytest.raises(VocabularyError, match='unknown field "requset"') as exc:
        Enact(str(store))
    assert "taxonomy/base.yaml:" in str(exc.value)
    assert not path(store / "audit").pathset("*.jsonl")


def test_request_rule_change_is_versioned_and_old_receipt_still_replays(store):
    old = Enact(str(store))
    tags = ["domain=any", "artifact=any", "task=design"]
    made = old.remember(title="Constraint", text="Keep the original constraint.", tags=tags)
    assert made["ok"]
    read = old.read(tags)
    change(store, lambda d: d["dimensions"]["task"]["values"]["design"]["request"]["words"].append("new cue"))
    new = Enact(str(store))
    assert new.taxonomy != old.taxonomy
    replay = new.rerun(read["receipt"])
    assert replay["ok"] and [m["key"] for m in replay["memories"]] == [m["key"] for m in read["memories"]]


def test_base_upgrade_uses_the_store_lock_and_rejects_a_stale_replacement(store):
    memory = Enact(str(store))
    path = store / "taxonomy" / "base.yaml"
    before = path.read_bytes()
    replacement = before.decode() + "\n# A reviewed base update.\n"
    assert memory.write_file("taxonomy/base.yaml", replacement, digest(before))
    assert not memory.write_file("taxonomy/base.yaml", before.decode(), digest(before))
    assert path.read_text() == replacement


def test_real_agent_interface_delivers_rules_and_keeps_store_override(store, tmp_path):
    change(store, lambda d: d["dimensions"]["task"]["values"]["design"]["request"].update(give_if="Custom live question?"))
    env = {**os.environ, "HOME": str(tmp_path / "home"), "XDG_DATA_HOME": str(tmp_path / "data"),
           "PYGIM_ENACT_GLOBAL": str(tmp_path / "absent"), "PYGIM_ENACT_SESSION": "1",
           "GIT_CONFIG_GLOBAL": str(tmp_path / "gitconfig"), "GIT_CONFIG_NOSYSTEM": "1"}
    oo = Path(sys.executable).with_name("oo.exe" if os.name == "nt" else "oo")
    command = [str(oo), "enact", "call", "vocabulary", "--root", str(store), "--json", "{}"]
    result = subprocess.run(command, cwd=tmp_path, env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    response = json.loads(result.stdout)
    assert "Custom live question?" in response["request"]
    assert "task=document" in response["request"]


@pytest.mark.parametrize("revise_after_removal", [False, True])
def test_removing_an_adopted_proposal_survives_a_new_process_and_successor(store, revise_after_removal):
    memory = Enact(str(store))
    tags = ["domain=any", "artifact=any", "task=design", "kind=principle"]
    made = memory.remember(title="Preserve a local practice", text="Use the practice when relevant.", tags=tags,
        proposals=[{"concept": "restore", "dimension": "task", "brief": "Restoring a previous state.",
                    "when": "Bringing a prior state back.", "when_not": "Not choosing a new state.",
                    "example": "Recover a prior working configuration."}])
    assert made["ok"]
    change(store, lambda d: d["dimensions"]["task"]["values"].update(restore={"entry": {
        "brief": "Restoring a previous state.", "when": "Bringing a prior state back.",
        "when_not": "Not choosing a new state.", "example": "Recover a prior working configuration."}}))
    memory = Enact(str(store))
    assert "task=restore" in memory.show(made["key"])["tags"]
    assert memory.unlink(made["key"], "task=restore", reason="The practice does not apply to restoration.")["ok"]
    if revise_after_removal:
        made = memory.remember(title="Preserve a local practice", text="Use the practice only where it applies.",
            tags=tags, supersedes=[made["key"]], seen=[made["key"]])
        assert made["ok"]
    version = memory.version
    code = ("import json,sys; from pygim.enact import Enact; m=Enact(sys.argv[1]); "
            "print(json.dumps({'memory':m.show(sys.argv[2]),'version':m.version}))")
    result = subprocess.run([sys.executable, "-c", code, str(store), made["key"]],
                            capture_output=True, text=True, timeout=30, check=True)
    after = json.loads(result.stdout)
    assert "task=restore" not in after["memory"]["tags"]
    assert after["version"] == version  # reopening must not append an automatic link
