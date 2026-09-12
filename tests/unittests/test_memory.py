"""pygim.memory — the problem-space memory over the files store.

Each test class follows a part of docs/design/memory: the vocabulary (02), the
four checks of a write (overview §4.5), the read (04), learning and curating,
merges, and what survives a restart or a git merge (03).
"""
from __future__ import annotations

import shutil
import textwrap

import pytest

from pygim.memory import Memory, VocabularyError

PACK = textwrap.dedent("""\
    pack: dnd
    entry:
      brief: Dungeons and Dragons, 2024 rules.
      when: The knowledge is about designing for D&D.
      when_not: Not other tabletop games.
      example: Spell design notes.
    dimensions:
      purpose:
        role: soft
        weight: 2.0
        entry:
          brief: What the thing is for.
          full: The functional intent of a spell or feature.
          when: The knowledge concerns an intent.
          when_not: Not the mechanism that achieves it.
          example: Shield protects the caster.
        values:
          defensive: {entry: {brief: Protects., when: It prevents harm., when_not: Not when it harms., example: Shield.}}
          offensive: {entry: {brief: Harms., when: It deals harm., when_not: Not when it protects., example: Fireball.}}
          control:   {entry: {brief: Limits enemies., when: It imposes a condition., when_not: Not a damage rider., example: Web.}}
      action_economy:
        role: soft
        weight: 1.5
        entry:
          brief: What it costs to use.
          full: The action type the thing consumes.
          when: The knowledge depends on the action spent.
          when_not: Not the spell slot.
          example: Shield is a reaction.
        values:
          reaction: {entry: {brief: A reaction., when: It is used as a reaction., when_not: Not an action., example: Shield.}}
    extends:
      artifact:
        spell:   {entry: {brief: A spell., when: About a spell., when_not: Not a monster., example: Shield.}}
        monster: {entry: {brief: A monster., when: About a monster., when_not: Not a spell., example: A mephit.}}
      task:
        balance: {entry: {brief: Weighing power., when: Judging power for its cost., when_not: Not inventing it., example: Is it worth the slot?}}
      tier:
        low: {entry: {brief: Levels 1 to 4., when: Holds at those levels., when_not: Not above level 4., example: A 3rd-level character.}}
        mid: {entry: {brief: Levels 5 to 10., when: Holds at those levels., when_not: Not below level 5., example: A 5th-level character.}}
    """)

DESIGN = ["domain=dnd", "artifact=spell", "task=design"]


@pytest.fixture
def root(tmp_path):
    path = tmp_path / "repo"
    Memory.init(str(path))
    (path / "taxonomy" / "pack-dnd.yaml").write_text(PACK, encoding="utf-8")
    return path


@pytest.fixture
def mem(root):
    return Memory(str(root))


def write(mem, title, text, tags, **kw):
    r = mem.remember(title=title, text=text, tags=tags, **kw)
    assert r["ok"], r
    return r


def seed(mem):
    """Scenario 2.1's three memories: the procedure, the yardstick, Frost Ward."""
    p = write(mem, "Creating a spell", "1 read the space\n2 name it\n3 find its yardstick", DESIGN + ["kind=procedure"])
    y = write(mem, "Shield is the yardstick", "A defensive reaction earns its slot only if it beats Shield per slot.",
              DESIGN + ["purpose=defensive", "action_economy=reaction", "kind=principle", "tier=low", "tier=mid"],
              seen=[p["memory"]])
    f = write(mem, "Frost Ward", "Typed resistance until the start of your next turn; a niche, not an upgrade.",
              DESIGN + ["purpose=defensive", "action_economy=reaction", "kind=example", "tier=mid"],
              seen=[p["memory"], y["memory"]])
    return p, y, f


class TestRepository:
    def test_init_lays_out_the_repository(self, root):
        for part in ("taxonomy/base.yaml", "objects", "audit", "memories", ".gitignore", ".gitattributes"):
            assert (root / part).exists(), part
        assert "local/" in (root / ".gitignore").read_text()

    def test_opening_a_plain_folder_says_how_to_start(self, tmp_path):
        with pytest.raises(RuntimeError, match="oo memory init"):
            Memory(str(tmp_path))

    def test_init_twice_is_refused(self, root):
        with pytest.raises(RuntimeError, match="already a memory repository"):
            Memory.init(str(root))

    def test_first_open_records_the_vocabulary(self, mem):
        assert mem.version == 1
        assert len(mem.head) == 32


class TestVocabulary:
    def test_base_and_pack(self, mem):
        dims = {d["name"]: d for d in mem.vocabulary()["dimensions"]}
        assert list(dims)[:5] == ["domain", "artifact", "task", "kind", "tier"]
        assert dims["purpose"]["weight"] == "2.0" and dims["purpose"]["pack"] == "dnd"
        tags = {v["tag"] for d in dims.values() for v in d["values"]}
        assert {"domain=dnd", "task=balance", "kind=procedure", "tier=mid"} <= tags

    def test_hard_dimensions_get_any_and_soft_ones_do_not(self, mem):
        dims = {d["name"]: {v["tag"] for v in d["values"]} for d in mem.vocabulary()["dimensions"]}
        assert "task=any" in dims["task"] and "domain=any" in dims["domain"]
        assert not any(t.endswith("=any") for t in dims["tier"] | dims["purpose"])

    def test_every_failure_is_reported_with_file_and_line(self, root):
        broken = PACK.replace("when_not: Not other tabletop games.", "").replace("weight: 1.5", "weight: 1.5555")
        (root / "taxonomy" / "pack-dnd.yaml").write_text(broken, encoding="utf-8")
        with pytest.raises(VocabularyError) as info:
            Memory(str(root))
        msg = str(info.value)
        assert "taxonomy/pack-dnd.yaml:" in msg and "no when_not" in msg
        assert "1.5555" in msg and "three decimals" in msg

    def test_an_entry_that_repeats_its_name_is_refused(self, root):
        (root / "taxonomy" / "pack-dnd.yaml").write_text(PACK.replace("brief: Protects.", "brief: defensive"), encoding="utf-8")
        with pytest.raises(VocabularyError, match="brief only repeats the name"):
            Memory(str(root))

    def test_a_hand_edit_is_recorded_at_the_next_open(self, root, mem):
        v = mem.version
        path = root / "taxonomy" / "pack-dnd.yaml"
        path.write_text(path.read_text().replace("Spell design notes.", "Notes on designing spells."), encoding="utf-8")
        again = Memory(str(root))
        assert again.version == v + 1
        assert again.vocabulary()["version"] != mem.vocabulary()["version"]


class TestTheFourChecks:
    def test_unknown_tag_is_refused_with_suggestions(self, mem):
        r = mem.remember(title="t", text="x", tags=DESIGN + ["purpose=sneaky"])
        assert r["refused"] == "unknown tag" and "purpose=defensive" in r["facts"]

    def test_every_hard_question_must_be_answered(self, mem):
        r = mem.remember(title="t", text="x", tags=["domain=dnd", "artifact=spell"])
        assert r["refused"] == "unanswered" and "task=any" in r["message"]

    def test_a_new_memory_must_follow_a_read(self, mem):
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"])
        r = mem.remember(title="Shield", text="the yardstick", tags=DESIGN + ["kind=principle"])
        assert r["refused"] == "unread" and r["facts"][0].startswith(p["memory"])
        assert mem.remember(title="Shield", text="the yardstick", tags=DESIGN + ["kind=principle"], seen=[p["memory"]])["ok"]

    def test_the_space_is_the_hard_tags_only(self, mem):
        write(mem, "Monster note", "about monsters", ["domain=dnd", "artifact=monster", "task=design", "kind=principle"])
        assert mem.remember(title="Spell note", text="about spells", tags=DESIGN + ["kind=principle"])["ok"]

    def test_identical_text_is_a_learn_not_a_write(self, mem):
        p, y, f = seed(mem)
        r = mem.remember(title="again", text="A defensive reaction earns its slot only if it beats Shield per slot.",
                         tags=DESIGN + ["kind=principle"], supersedes=[f["memory"]])
        assert r["refused"] == "identical"

    def test_only_a_head_can_be_superseded(self, mem):
        p, y, f = seed(mem)
        v2 = write(mem, "Frost Ward, again", "Corrected text.", DESIGN + ["kind=example"], supersedes=[f["memory"]])
        r = mem.remember(title="stale", text="Another text.", tags=DESIGN + ["kind=example"], supersedes=[f["memory"]])
        assert r["refused"] == "not a head" and r["facts"][0].startswith(v2["memory"])

    def test_a_proposal_needs_a_complete_entry(self, mem):
        r = mem.remember(title="t", text="x", tags=DESIGN + ["kind=principle"],
                         proposals=[{"concept": "corruption", "dimension": "purpose", "brief": "b", "when": "w"}])
        assert r["refused"] == "proposal" and "no when_not" in r["message"]


class TestReading:
    def test_the_procedure_comes_first_and_only_once(self, mem):
        p, y, f = seed(mem)
        r = mem.read(DESIGN, ["purpose=defensive", "action_economy=reaction", "tier=mid"])
        assert r["procedure"]["memory"] == p["memory"]
        assert [m["memory"] for m in r["memories"]] == [y["memory"], f["memory"]]
        assert r["memories"][0]["score"] == "4.5" and r["memories"][1]["score"] == "4.5"   # tie: the older wins

    def test_the_slot_empties_when_the_pair_is_not_determined(self, mem):
        seed(mem)
        r = mem.read(["domain=dnd", "artifact=spell", "artifact=monster", "task=design"])
        assert r["procedure"] is None and "2 artifact values" in r["procedure_note"]

    def test_every_inclusion_is_explained(self, mem):
        p, y, f = seed(mem)
        m = mem.read(DESIGN, ["purpose=offensive", "tier=mid"])["memories"][0]
        assert m["hard_matched"] == DESIGN and m["soft_matched"] == ["tier=mid"] and m["soft_missed"] == ["purpose=offensive"]

    def test_a_budget_skips_what_does_not_fit(self, mem):
        seed(mem)
        full = mem.read(DESIGN)
        r = mem.read(DESIGN, budget=full["procedure"]["tokens"] + full["memories"][0]["tokens"])
        assert len(r["memories"]) == 1 and len(r["skipped"]) == 1
        assert mem.read(DESIGN, budget=1)["over_budget"]

    def test_any_is_found_by_every_value_and_refused_in_a_query(self, mem):
        a = write(mem, "Triggers must be exact", "Every reaction names its trigger.",
                  ["domain=dnd", "artifact=spell", "task=any", "kind=principle"])
        r = mem.read(["domain=dnd", "artifact=spell", "task=balance"])
        assert [m["memory"] for m in r["memories"]] == [a["memory"]]
        assert r["memories"][0]["hard_matched"] == ["domain=dnd", "artifact=spell", "task=any"]
        assert mem.read(["task=any"])["refused"] == "any in a query"

    def test_a_read_needs_a_hard_tag(self, mem):
        assert mem.read([], ["tier=mid"])["refused"] == "no hard tags"

    def test_a_superseded_memory_is_not_a_candidate(self, mem):
        p, y, f = seed(mem)
        v2 = write(mem, "The Ward family", "Frost Ward and Ember Ward.", DESIGN + ["kind=example"], supersedes=[f["memory"]])
        ids = [m["memory"] for m in mem.read(DESIGN)["memories"]]
        assert v2["memory"] in ids and f["memory"] not in ids
        assert mem.show(f["memory"])["superseded_by"] == [v2["memory"]]


class TestLearningAndCurating:
    def test_three_reports_promote_a_tag(self, mem):
        p, y, f = seed(mem)
        for n in range(2):
            assert not mem.learn(f["memory"], tag="task=balance", reason="needed")["promoted"]
        assert mem.learn(f["memory"], tag="task=balance", reason="needed")["promoted"]
        assert "task=balance" in mem.show(f["memory"])["tags"]
        assert [m["memory"] for m in mem.read(["domain=dnd", "artifact=spell", "task=balance"])["memories"]] == [f["memory"]]

    def test_learn_on_a_procedure_records_its_steps_held(self, mem):
        p, _, _ = seed(mem)
        assert mem.learn(p["memory"])["message"] == "recorded steps_held"

    def test_link_unlink_retire(self, mem):
        p, y, f = seed(mem)
        assert mem.link(y["memory"], "task=balance", reason="always needed")["ok"]
        assert mem.link(y["memory"], "task=balance", reason="again")["refused"] == "already carried"
        assert mem.unlink(y["memory"], "task=balance", reason="not after all")["ok"]
        assert "task=balance" not in mem.show(y["memory"])["tags"]
        assert mem.retire(f["memory"], reason="obsolete")["ok"]
        assert f["memory"] not in [m["memory"] for m in mem.read(DESIGN)["memories"]]
        assert not (mem.show(f["memory"])["head"])

    def test_a_proposal_is_pending_until_the_pack_has_it(self, root, mem):
        p, y, f = seed(mem)
        w = write(mem, "Spreading a sign", "Grow the sign by whole cubes.", DESIGN + ["purpose=control", "kind=principle"],
                  seen=[p["memory"], y["memory"], f["memory"]],
                  proposals=[{"concept": "corruption", "dimension": "purpose", "brief": "Spreads corruption.",
                              "when": "It creates a sign of corruption.", "when_not": "Plain necrotic damage.",
                              "example": "Spreading Blight."}])
        assert [(x["concept"], x["asked_by"]) for x in mem.proposals()] == [("corruption", [w["memory"]])]
        path = root / "taxonomy" / "pack-dnd.yaml"
        path.write_text(path.read_text().replace(
            "      control:",
            "      corruption: {entry: {brief: Spreads corruption., when: It creates a sign., when_not: Plain necrotic damage., example: Spreading Blight.}}\n      control:"),
            encoding="utf-8")
        again = Memory(str(root))
        assert again.proposals() == []
        assert "purpose=corruption" in again.show(w["memory"])["tags"]   # the asker is linked, source proposed


class TestMerging:
    def test_a_merge_supersedes_its_sources_and_names_the_failure(self, mem):
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"])
        a = write(mem, "Cap the rider", "A rider that scales with damage taken must be capped.", DESIGN + ["kind=principle"],
                  seen=[p["memory"]])
        b = write(mem, "Cap it", "Cap a damage rider.", ["domain=dnd", "artifact=spell", "task=critique", "kind=principle"])
        r = mem.merge([a["memory"], b["memory"]], title="Cap the rider", text="Cap a rider that grows with damage taken.",
                      reason="one point, written twice")
        assert r["ok"] and r["verdict"].startswith("classification mismatch")
        tags = mem.show(r["memory"])["tags"]
        assert "task=design" in tags and "task=critique" in tags
        assert not mem.show(a["memory"])["head"] and not mem.show(b["memory"])["head"]

    def test_a_merge_after_reading_is_a_judgement_error(self, mem):
        a = write(mem, "One", "first text", DESIGN + ["kind=principle"])
        b = write(mem, "Two", "second text", DESIGN + ["kind=principle"], seen=[a["memory"]])
        r = mem.merge([a["memory"], b["memory"]], title="One", text="joined", reason="same")
        assert r["verdict"].startswith("judgement error")


class TestRestartsAndReruns:
    def test_a_reopened_store_is_the_same_snapshot(self, root, mem):
        seed(mem)
        again = Memory(str(root))
        assert (again.head, again.version) == (mem.head, mem.version)

    def test_a_receipt_reruns_to_the_same_answer_after_the_store_moved_on(self, root, mem):
        p, y, f = seed(mem)
        receipt = mem.read(DESIGN, ["purpose=defensive"])["receipt"]
        write(mem, "Newer", "a newer principle", DESIGN + ["purpose=defensive", "kind=principle"],
              seen=[p["memory"], y["memory"], f["memory"]])
        again = Memory(str(root))
        rerun = again.rerun(receipt)
        assert rerun["same"] and rerun["version"] == receipt["version"]
        assert len(again.read(DESIGN, ["purpose=defensive"])["memories"]) == 3

    def test_two_sessions_on_one_clone_rerun_the_checks_under_the_lock(self, root, mem):
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"])
        other = Memory(str(root))                               # a second process on the same clone
        assert other.remember(title="Mine", text="written first", tags=DESIGN + ["kind=principle"], seen=[p["memory"]])["ok"]
        r = mem.remember(title="Yours", text="written second", tags=DESIGN + ["kind=principle"], seen=[p["memory"]])
        assert r["refused"] == "unread" and "Mine" in r["facts"][0]

    def test_git_merging_two_clones_gives_one_snapshot_whoever_merges(self, tmp_path, root, mem):
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"])
        theirs = tmp_path / "theirs"
        shutil.copytree(root, theirs)
        (theirs / "local" / "clone").unlink()                  # a different person's clone
        mine_w = write(mem, "Mine", "my principle", DESIGN + ["kind=principle"], seen=[p["memory"]])
        their_mem = Memory(str(theirs))
        their_w = write(their_mem, "Theirs", "their principle", DESIGN + ["kind=principle"], seen=[p["memory"]])
        mine_clone = (root / "local" / "clone").read_text().strip()
        their_clone = (theirs / "local" / "clone").read_text().strip()
        for src, dst, clone in ((theirs, root, their_clone), (root, theirs, mine_clone)):   # what git brings each way
            shutil.copy2(src / "audit" / f"{clone}.jsonl", dst / "audit" / f"{clone}.jsonl")  # each clone appends only to its own file
            shutil.copytree(src / "objects", dst / "objects", dirs_exist_ok=True)           # content-addressed: never conflicts
        a, b = Memory(str(root)), Memory(str(theirs))
        assert a.head == b.head and a.version == b.version
        titles = {m["title"] for m in a.read(DESIGN)["memories"]}
        assert {"Mine", "Theirs"} <= titles


class TestIngestion:
    CORPUS = textwrap.dedent("""\
        # a colleague's notes
        ## shield-baseline
        title: Shield is the benchmark
        domain: dnd
        artifact: spell
        task: design, balance
        purpose: defensive
        kind: principle

        Shield is the reference point for any reaction that reduces incoming harm.

        ## bad-block
        title: A block with a wrong tier
        domain: dnd
        artifact: spell
        task: design
        tier: tier2

        Text.
        """)

    def test_blocks_land_and_a_bad_one_is_named_by_file_and_line(self, tmp_path, mem):
        path = tmp_path / "spells.md"
        path.write_text(self.CORPUS, encoding="utf-8")
        r = mem.ingest(str(path))
        assert r["added"] == 1 and len(r["refused"]) == 1
        assert r["refused"][0].startswith("spells.md:12: bad-block") and "tier=tier2" in r["refused"][0]
        assert mem.ingest(str(path))["unchanged"] == 1

    def test_an_edited_block_supersedes_its_head(self, tmp_path, mem):
        path = tmp_path / "spells.md"
        path.write_text(self.CORPUS, encoding="utf-8")
        mem.ingest(str(path))
        path.write_text(self.CORPUS.replace("incoming harm.", "incoming harm, less so by tier 3."), encoding="utf-8")
        assert mem.ingest(str(path))["superseded"] == 1
        texts = [m["text"] for m in mem.read(DESIGN)["memories"]]
        assert texts == ["Shield is the reference point for any reaction that reduces incoming harm, less so by tier 3."]
