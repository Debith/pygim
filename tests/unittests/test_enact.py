"""pygim.enact — the problem-space memory over the files store.

Each test class follows a part of docs/design/memory: the vocabulary (02), the
four checks of a write (overview §4.5), the read (04), learning and curating,
merges, and what survives a restart or a git merge (03).
"""
from __future__ import annotations

import shutil
import textwrap

import pytest
from click.testing import CliRunner

from pygim.__main__ import cli_oo
from pygim.enact import Enact, VocabularyError

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
    Enact.init(str(path))
    (path / "taxonomy" / "pack-dnd.yaml").write_text(PACK, encoding="utf-8")
    return path


@pytest.fixture
def mem(root):
    return Enact(str(root))


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
        with pytest.raises(RuntimeError, match="oo enact setup"):
            Enact(str(tmp_path))

    def test_init_twice_is_refused(self, root):
        with pytest.raises(RuntimeError, match="already a memory repository"):
            Enact.init(str(root))

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
            Enact(str(root))
        msg = str(info.value)
        assert "taxonomy/pack-dnd.yaml:" in msg and "no when_not" in msg
        assert "1.5555" in msg and "three decimals" in msg

    def test_an_entry_that_repeats_its_name_is_refused(self, root):
        (root / "taxonomy" / "pack-dnd.yaml").write_text(PACK.replace("brief: Protects.", "brief: defensive"), encoding="utf-8")
        with pytest.raises(VocabularyError, match="brief only repeats the name"):
            Enact(str(root))

    def test_a_hand_edit_is_recorded_at_the_next_open(self, root, mem):
        v = mem.version
        path = root / "taxonomy" / "pack-dnd.yaml"
        path.write_text(path.read_text().replace("Spell design notes.", "Notes on designing spells."), encoding="utf-8")
        again = Enact(str(root))
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
        assert len(r["memories"]) == 1 and r["skipped"] == 1
        assert mem.read(DESIGN, budget=1)["over_budget"]

    def test_any_is_found_by_every_value_and_refused_in_a_query(self, mem):
        a = write(mem, "Triggers must be exact", "Every reaction names its trigger.",
                  ["domain=dnd", "artifact=spell", "task=any", "kind=principle"])
        r = mem.read(["domain=dnd", "artifact=spell", "task=balance"])
        assert [m["memory"] for m in r["memories"]] == [a["memory"]]
        assert r["memories"][0]["hard_matched"] == ["domain=dnd", "artifact=spell", "task=any"]
        refused = mem.read(["task=any"])
        assert refused["refused"] == "any in a query" and "memories tagged `any` answer every value" in refused["message"]

    def test_a_read_needs_a_hard_tag(self, mem):
        assert mem.read([], ["tier=mid"])["refused"] == "no hard tags"

    def test_a_superseded_memory_is_not_a_candidate(self, mem):
        p, y, f = seed(mem)
        v2 = write(mem, "The Ward family", "Frost Ward and Ember Ward.", DESIGN + ["kind=example"], supersedes=[f["memory"]])
        ids = [m["memory"] for m in mem.read(DESIGN)["memories"]]
        assert v2["memory"] in ids and f["memory"] not in ids
        assert mem.show(f["memory"])["superseded_by"] == [v2["memory"]]


class TestADimensionThatOffersOnlyAny:
    """`any` is refused in a query, because it means "every value" and a query should name the value
    its work is. But a dimension can have nothing else: a store initialised from the base vocabulary
    has only `domain=any` and `artifact=any` until a pack lands, and the machine's global store stays
    that way for good, because nothing in it is about one project. The refusal lifts exactly there."""

    def test_a_fresh_store_can_be_read_on_the_dimensions_it_has_no_values_for(self, tmp_path):
        bare = tmp_path / "bare"
        Enact.init(str(bare))
        m = Enact(str(bare))
        m.remember(title="Explain in layers", text="the assumed words first", reason="a preference",
                   tags=["domain=any", "artifact=any", "task=explain", "kind=preference"])
        r = m.read(hard=["domain=any", "artifact=any", "task=explain"])
        assert "refused" not in r and r["candidates"] == 1
        assert [x["title"] for x in r["memories"]] == ["Explain in layers"]

    def test_the_slot_fills_on_any_where_any_is_the_only_artifact_there_is(self, tmp_path):
        bare = tmp_path / "bare"
        Enact.init(str(bare))
        m = Enact(str(bare))
        m.remember(title="How to explain", text="1 the words\n2 one line\n3 a table", reason="the procedure",
                   tags=["domain=any", "artifact=any", "task=explain", "kind=procedure"])
        r = m.read(hard=["domain=any", "artifact=any", "task=explain"])
        assert r["procedure"] is not None and r["procedure"]["title"] == "How to explain"

    def test_it_is_refused_again_as_soon_as_the_dimension_has_a_value_to_name(self, mem):
        """The dnd pack gives `domain` and `artifact` real values, so `any` stops being nameable."""
        seed(mem)
        r = mem.read(hard=["domain=any", "artifact=spell", "task=design"])
        assert r["refused"] == "any in a query" and "domain=any" in r["message"]
        assert "domain=dnd" in r["facts"]                     # and it names what to say instead


class TestWhichProcedureIsPlaced:
    """The slot holds one procedure, placed before everything else. Which one is chosen was, until
    2026-09-22, the lowest id among everything the hard tags admitted — so in a space with fifteen
    to forty procedures, one early narrow memory answered every question there. The D-D-2024 study
    measured it at four moments and reported it (ranking rules v2)."""

    def three_procedures(self, mem):
        first = write(mem, "Creating a spell", "1 read the space\n2 name it", DESIGN + ["kind=procedure"])
        frost = write(mem, "Warding against cold", "1 pick the damage type\n2 set the duration",
                      DESIGN + ["kind=procedure", "purpose=defensive"], seen=[first["memory"]])
        blast = write(mem, "Shaping a blast", "1 pick the area\n2 set the save",
                      DESIGN + ["kind=procedure", "purpose=offensive"],
                      seen=[first["memory"], frost["memory"]])
        return first, frost, blast

    def test_the_slot_is_ranked_now_and_not_the_oldest(self, mem):
        first, frost, _ = self.three_procedures(mem)
        plain = mem.read(DESIGN)
        assert plain["procedure"]["memory"] == first["memory"]      # nothing to rank by: age still decides
        asked = mem.read(DESIGN, ["purpose=defensive"])
        assert asked["procedure"]["memory"] == frost["memory"]      # a soft tag now reaches the slot
        assert "ranks first" in asked["procedure_note"]

    def test_the_term_narrows_the_slot_before_it_is_chosen(self, mem):
        first, _, blast = self.three_procedures(mem)
        out = mem.read(DESIGN, term="blast")
        assert out["procedure"]["memory"] == blast["memory"]
        assert first["memory"] != blast["memory"]

    def test_a_term_that_no_procedure_matches_falls_back_and_says_so(self, mem):
        """A procedure that does not mention the word can still be the steps to follow."""
        first, _, _ = self.three_procedures(mem)
        out = mem.read(DESIGN, term="Shield")
        assert out["procedure"]["memory"] == first["memory"]
        assert "no procedure matches the term" in out["procedure_note"]

    def test_an_accepted_generalisation_takes_the_slot_over_a_better_scoring_instance(self, mem):
        """A person accepted it for exactly this: it is the pattern its instances share."""
        first, frost, blast = self.three_procedures(mem)
        both = write(mem, "Shaping any spell", "1 name the space\n2 pick the shape\n3 weigh it",
                     DESIGN + ["kind=procedure", "purpose=defensive"],
                     generalises=[frost["memory"], blast["memory"]],
                     seen=[first["memory"], frost["memory"], blast["memory"]])
        assert mem.read(DESIGN, ["purpose=defensive"])["procedure"]["memory"] == frost["memory"]
        assert mem.accept(both["memory"], reason="it covers both")["ok"]
        assert mem.read(DESIGN, ["purpose=defensive"])["procedure"]["memory"] == both["memory"]

    def test_a_slot_too_large_for_the_budget_is_named_and_takes_nothing_down_with_it(self, mem):
        """It used to end the read: over_budget, nothing selected, every ranked match skipped — one
        long procedure in place of everything that was asked for."""
        long_steps = "\n".join(f"{n} do the {n}th thing, carefully and at length" for n in range(1, 40))
        steps = write(mem, "Creating a spell", long_steps, DESIGN + ["kind=procedure"])
        short = write(mem, "Shield is the yardstick", "It beats Shield, or it is a niche.",
                      DESIGN + ["kind=principle"], seen=[steps["memory"]])
        full = mem.read(DESIGN, budget=0)
        big, small = full["procedure"]["tokens"], full["memories"][0]["tokens"]
        assert small < big, "the case needs a procedure larger than a memory"

        out = mem.read(DESIGN, budget=big - 1)
        assert out["procedure"]["title"] == "Creating a spell"
        assert out["procedure"]["text"] == "" and out["procedure"]["over_budget"] is True
        assert "its title only" in out["procedure_note"] and out["over_budget"] is True
        assert [m["memory"] for m in out["memories"]] == [short["memory"]]   # the budget reached it
        assert out["tokens"] == small                                       # and the slot cost nothing

    def test_the_receipt_pins_the_rules_that_ordered_it(self, mem):
        """A receipt pinned the snapshot and the vocabulary, so the same question got the same
        answer — unless the rules turning candidates into an order had changed underneath it, which
        nothing recorded."""
        seed(mem)
        assert mem.read(DESIGN)["receipt"]["rules"] >= 2


class TestWhatAReadSays:
    """Beyond the ranked memories: counts instead of the list of the rest, the tags the candidates
    carry, the documents they cite, and a term to narrow them (04 §3.8)."""

    def test_the_rest_is_a_count_and_facets_say_what_the_candidates_carry(self, mem):
        seed(mem)
        r = mem.read(DESIGN, ["purpose=offensive", "tier=mid"], max=1)
        assert r["skipped"] == 1                                             # 2 ranked, 1 placed, the procedure apart
        facets = r["facets"]
        assert facets["purpose=offensive"] == 0                              # a soft tag nothing carries can only miss
        assert facets["kind=procedure"] == 1 and facets["tier=low"] == 1 and facets["tier=mid"] == 2
        assert "domain=dnd" not in facets                                    # every candidate carries it: says nothing

    def test_coverage_names_what_the_candidates_cite_and_what_none_does(self, root, mem):
        (root / "sources").mkdir(exist_ok=True)
        (root / "sources" / "inventory.yaml").write_text(
            "# documents\nphb-glossary:\n  kind: text\n  path: \"glossary\"\nphb-ch1:\n  kind: text\n  path: \"ch1\"\n",
            encoding="utf-8")
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"], cites=["phb-glossary:L10", "phb-glossary:L12-14"])
        write(mem, "Shield", "Shield is the yardstick.", DESIGN + ["kind=principle"], seen=[p["memory"]])
        coverage = mem.read(DESIGN)["coverage"]
        assert coverage == {"cited": {"phb-glossary": 1}, "uncited": 1, "not_cited": ["phb-ch1"]}

    def test_a_term_keeps_the_candidates_that_name_it_and_not_the_procedure_slot(self, mem):
        p, y, f = seed(mem)
        r = mem.read(DESIGN, term="SHIELD")                                  # the yardstick's title; Frost Ward's text does not say it
        assert r["procedure"]["memory"] == p["memory"]
        assert [m["memory"] for m in r["memories"]] == [y["memory"]]
        assert r["candidates"] == 3 and r["term_matched"] == 1
        by_text = mem.read(DESIGN, term="niche")
        assert [m["memory"] for m in by_text["memories"]] == [f["memory"]]
        assert mem.read(DESIGN, term="mounted")["memories"] == []

    def test_a_term_matches_a_word_start_not_inside_a_word(self, mem):
        p, y, f = seed(mem)
        write(mem, "Damage threshold", "Only damage above the amount breaks it.", DESIGN + ["kind=principle"],
              seen=[p["memory"], y["memory"], f["memory"]])
        assert mem.read(DESIGN, term="mount")["memories"] == []                  # not inside "amount"
        assert mem.read(DESIGN, term="yard")["memories"][0]["memory"] == y["memory"]   # a stem of "yardstick"
        assert [m["memory"] for m in mem.read(DESIGN, term="typed resistance")["memories"]] == [f["memory"]]

    def test_a_budget_says_how_many_it_had_no_room_for(self, mem):
        seed(mem)
        full = mem.read(DESIGN)
        tight = mem.read(DESIGN, budget=full["procedure"]["tokens"] + 1)
        assert tight["memories"] == [] and tight["skipped"] == 2 and tight["budget_dropped"] == 2
        assert mem.read(DESIGN, max=1)["budget_dropped"] == 0                    # max, not the budget

    def test_a_term_is_part_of_the_receipt_and_reruns_the_same(self, root, mem):
        p, y, f = seed(mem)
        receipt = mem.read(DESIGN, term="niche")["receipt"]
        assert receipt["term"] == "niche"
        write(mem, "Another niche", "a niche again", DESIGN + ["kind=principle"], seen=[p["memory"], y["memory"], f["memory"]])
        stored = Enact(str(root)).receipts()[-1]
        assert stored["term"] == "niche" and Enact(str(root)).rerun(stored)["same"]


class TestChangesReportTheirResult:
    def test_link_unlink_and_remember_return_the_tags_as_they_stand(self, mem):
        p, y, f = seed(mem)
        linked = mem.link(y["memory"], "task=balance", reason="needed")
        assert linked["memory"] == y["memory"] and "task=balance" in linked["tags"] and linked["head"]
        assert "task=balance" not in mem.unlink(y["memory"], "task=balance", reason="not after all")["tags"]
        assert set(DESIGN) <= set(write(mem, "N", "new", DESIGN + ["kind=principle"], seen=[p["memory"], y["memory"], f["memory"]])["tags"])
        assert mem.retire(f["memory"], reason="gone")["head"] is False

    def test_the_same_text_with_other_tags_points_to_link_and_unlink(self, mem):
        p, y, f = seed(mem)
        text = mem.show(f["memory"])["text"]
        retag = mem.remember(title="Frost Ward", text=text, tags=DESIGN + ["purpose=control", "kind=example"],
                             supersedes=[f["memory"]], seen=[p["memory"], y["memory"]])
        assert retag["refused"] == "identical" and "link and unlink" in retag["message"]
        assert "link purpose=control" in retag["facts"] and "unlink purpose=defensive" in retag["facts"]
        same = mem.remember(title="Frost Ward", text=text, tags=mem.show(f["memory"])["tags"], supersedes=[f["memory"]])
        assert same["refused"] == "identical" and "learn" in same["message"]


class TestSeeding:
    def test_a_seed_write_skips_the_unread_check(self, mem):
        seed(mem)
        written = mem.remember(title="From the book", text="A rule.", tags=DESIGN + ["kind=principle"], origin="seed")
        assert written["ok"] and mem.show(written["memory"])["origin"] == "seed"
        assert mem.remember(title="T", text="x", tags=DESIGN, origin="merged")["refused"] == "origin"

    def test_ingest_carries_cites(self, tmp_path, mem):
        path = tmp_path / "rules.md"
        path.write_text("## per-day\ntitle: Per Day\ndomain: dnd\nartifact: spell\ntask: design\ncites: phb-glossary:L717, phb-ch1:L1-3\n\n"
                        "Once per day means until a long rest.\n", encoding="utf-8")
        assert mem.ingest(str(path))["added"] == 1
        head = mem.read(DESIGN)["memories"][0]
        assert mem.show(head["memory"])["cites"] == ["phb-glossary:L717", "phb-ch1:L1-3"]


class TestAReplacedVocabulary:
    def test_history_that_named_a_removed_value_raises_no_review(self, root, mem):
        p, y, f = seed(mem)
        mem.unlink(f["memory"], "tier=mid", reason="retagging before the value goes")
        pack = root / "taxonomy" / "pack-dnd.yaml"
        pack.write_text(pack.read_text(encoding="utf-8").replace(
            "    mid: {entry: {brief: Levels 5 to 10., when: Holds at those levels., when_not: Not below level 5., example: A 5th-level character.}}\n", ""),
            encoding="utf-8")
        reviews = [r for r in Enact(str(root)).session()["reviews"] if r["kind"] == "unknown tag"]
        assert len(reviews) == 1 and reviews[0]["text"].startswith(y["memory"]) and "tier=mid" in reviews[0]["text"]


class TestLearningAndCurating:
    def test_three_reports_promote_a_tag(self, mem):
        p, y, f = seed(mem)
        for n in range(2):
            assert not mem.learn(f["memory"], tag="task=balance", reason="needed")["promoted"]
        assert mem.learn(f["memory"], tag="task=balance", reason="needed")["promoted"]
        assert "task=balance" in mem.show(f["memory"])["tags"]
        assert [m["memory"] for m in mem.read(["domain=dnd", "artifact=spell", "task=balance"])["memories"]] == [f["memory"]]

    def test_a_retrieval_that_misled_or_went_unused_is_recorded_too(self, mem):
        """The first seam of the end state (overview §4.13): without a verdict, a memory that wastes
        every context it enters looks exactly like one nobody has read."""
        p, y, f = seed(mem)
        assert mem.learn(f["memory"], verdict="not_needed", reason="the question was about tiers")["message"] == "recorded not_needed"
        assert mem.learn(f["memory"], verdict="misleading", reason="read as a rule, it is one case")["message"] == "recorded misleading"
        assert mem.learn(f["memory"], reason="the niche framing settled it")["message"] == "recorded useful"
        counters = mem.show(f["memory"])["counters"]
        assert (counters["useful"], counters["not_needed"], counters["misleading"]) == (1, 1, 1)
        assert mem.learn(f["memory"], verdict="helpful")["refused"] == "verdict"

    def test_only_usefulness_promotes_a_tag(self, mem):
        p, y, f = seed(mem)
        for _ in range(4):
            assert not mem.learn(f["memory"], tag="task=balance", verdict="misleading", reason="wrong here")["promoted"]
        assert "task=balance" not in mem.show(f["memory"])["tags"]      # four reports, none of them usefulness
        for _ in range(2):
            mem.learn(f["memory"], tag="task=balance", reason="needed")
        assert mem.learn(f["memory"], tag="task=balance", reason="needed")["promoted"]

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

    def test_a_new_dimension_proposal_settles_when_the_dimension_arrives(self, root, mem):
        """It names no value, so nothing could ever mark it accepted: `status` kept reporting
        dimensions as pending that the vocabulary already had (study feedback, 2026-09-21)."""
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"])
        write(mem, "Corruption spreads", "Blight grows by whole cubes.", DESIGN + ["kind=principle"], seen=[p["memory"]],
              proposals=[{"concept": "Sign", "dimension": "", "brief": "Whether a spell leaves a sign.",
                          "when": "The knowledge is about a lasting mark.", "when_not": "Not plain damage.",
                          "example": "Spreading Blight."}])
        assert [x["concept"] for x in mem.proposals()] == ["Sign"]
        path = root / "taxonomy" / "pack-dnd.yaml"
        path.write_text(path.read_text(encoding="utf-8").replace(
            "  action_economy:",
            "  sign:\n    role: soft\n    weight: 1.0\n"
            "    entry: {brief: Whether a lasting mark is left., full: Whether the thing leaves a sign behind.,"
            " when: About a lasting mark., when_not: Not plain damage., example: Spreading Blight.}\n"
            "    values:\n      lasting: {entry: {brief: A mark that stays., when: It lasts beyond the turn.,"
            " when_not: Not a momentary effect., example: Blight.}}\n  action_economy:"), encoding="utf-8")
        assert Enact(str(root)).proposals() == []                       # the dimension exists, so it is settled

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
        again = Enact(str(root))
        assert again.proposals() == []
        assert "purpose=corruption" in again.show(w["memory"])["tags"]   # the asker is linked, source proposed

    def test_an_accepted_proposal_reaches_the_memory_that_replaced_its_asker(self, root, mem):
        """The asker was superseded before its proposal was accepted: only heads are linked, the asker
        was not one, and its revision never asked — so the value joined the vocabulary and no memory
        carried it (global #65 asked for task=operate, #66 superseded it; 2026-09-30). What replaced
        the asker is where the tag belongs."""
        p, y, f = seed(mem)
        w = write(mem, "Spreading a sign", "Grow the sign by whole cubes.", DESIGN + ["purpose=control", "kind=principle"],
                  seen=[p["memory"], y["memory"], f["memory"]],
                  proposals=[{"concept": "corruption", "dimension": "purpose", "brief": "Spreads corruption.",
                              "when": "It creates a sign of corruption.", "when_not": "Plain necrotic damage.",
                              "example": "Spreading Blight."}])
        revised = write(mem, "Spreading a sign", "Grow the sign by whole cubes, never by half.",
                        DESIGN + ["purpose=control", "kind=principle"], supersedes=[w["memory"]],
                        seen=[p["memory"], y["memory"], f["memory"], w["memory"]])
        path = root / "taxonomy" / "pack-dnd.yaml"
        path.write_text(path.read_text().replace(
            "      control:",
            "      corruption: {entry: {brief: Spreads corruption., when: It creates a sign., when_not: Plain necrotic damage., example: Spreading Blight.}}\n      control:"),
            encoding="utf-8")
        again = Enact(str(root))
        assert again.proposals() == []
        assert "purpose=corruption" in again.show(revised["memory"])["tags"]


class TestMovingACitation:
    """A citation is evidence *about* a memory, not part of what it says, so it moves the way a tag
    does. Before this, correcting one line number meant superseding the whole memory and spending a
    number on it — the adventure-craft study left fifteen precision fixes unmade for that reason,
    and this store's own second memory was superseded to move two locators."""

    def test_a_locator_is_added_and_removed_without_a_new_version_of_the_memory(self, mem):
        p, _, _ = seed(mem)
        before = mem.show(p["memory"])
        assert before["cites"] == []
        assert mem.cite(p["memory"], "phb-ch1:L10-12", reason="where the steps come from")["ok"]
        after = mem.show(p["memory"])
        assert after["cites"] == ["phb-ch1:L10-12"]
        assert after["key"] == before["key"] and after["memory"] == before["memory"]  # same memory, same number
        assert mem.uncite(p["memory"], "phb-ch1:L10-12", reason="cited the wrong page")["ok"]
        assert mem.show(p["memory"])["cites"] == []

    def test_a_correction_replaces_one_locator_and_leaves_the_others_alone(self, mem):
        p, _, _ = seed(mem)
        for loc in ("phb-ch1:L10", "phb-ch2:L40-41", "phb-ch3:L7"):
            assert mem.cite(p["memory"], loc, reason="evidence")["ok"]
        assert mem.uncite(p["memory"], "phb-ch2:L40-41", reason="off by two lines")["ok"]
        assert mem.cite(p["memory"], "phb-ch2:L42-43", reason="the passage actually quoted")["ok"]
        assert mem.show(p["memory"])["cites"] == ["phb-ch1:L10", "phb-ch3:L7", "phb-ch2:L42-43"]

    def test_the_refusals_name_the_fact(self, mem):
        p, y, _ = seed(mem)
        assert mem.cite(p["memory"], "", reason="r")["refused"] == "no locator"
        assert mem.cite(p["memory"], "phb-ch1:L1", reason="r")["ok"]
        again = mem.cite(p["memory"], "phb-ch1:L1", reason="r")
        assert again["refused"] == "already cited" and "phb-ch1:L1" in again["message"]
        missing = mem.uncite(y["memory"], "phb-ch1:L1", reason="r")
        assert missing["refused"] == "not cited" and missing["facts"] == []
        assert mem.cite("#404", "phb-ch1:L1", reason="r")["refused"] == "unknown memory"

    def test_a_superseded_memory_keeps_the_citations_it_was_written_with(self, mem):
        p, _, _ = seed(mem)
        assert mem.cite(p["memory"], "phb-ch1:L10", reason="evidence")["ok"]
        newer = write(mem, "Creating a spell", "1 read the space\n2 name it\n3 weigh it", DESIGN + ["kind=procedure"],
                      supersedes=[p["memory"]], seen=[p["memory"]])
        old = mem.show(p["memory"])
        assert old["head"] is False and old["cites"] == ["phb-ch1:L10"]      # history is not rewritten
        refused = mem.cite(p["memory"], "phb-ch1:L11", reason="r")
        assert refused["refused"] == "not a head" and "current head" in refused["message"]
        assert mem.show(newer["memory"])["cites"] == []


class TestHeadViews:
    """memories/<slug>.md follows the index, whatever changed it (03 §3.3)."""

    @staticmethod
    def view(root, w):
        path = root / "memories" / (w["slug"] + ".md")
        return path.read_text(encoding="utf-8") if path.exists() else None

    def test_link_and_unlink_rewrite_the_tags(self, root, mem):
        p, y, f = seed(mem)
        mem.link(y["memory"], "task=balance", reason="always needed")
        assert "task=balance" in self.view(root, y)
        mem.unlink(y["memory"], "task=balance", reason="not after all")
        assert "task=balance" not in self.view(root, y)

    def test_a_promoted_tag_lands_in_the_view(self, root, mem):
        p, y, f = seed(mem)
        for _ in range(3):
            mem.learn(f["memory"], tag="task=balance", reason="needed")
        assert "task=balance" in self.view(root, f)

    def test_an_accepted_proposal_lands_in_the_askers_view(self, root, mem):
        p, y, f = seed(mem)
        w = write(mem, "Spreading a sign", "Grow the sign by whole cubes.", DESIGN + ["kind=principle"],
                  seen=[p["memory"], y["memory"], f["memory"]],
                  proposals=[{"concept": "corruption", "dimension": "purpose", "brief": "Spreads corruption.",
                              "when": "It creates a sign of corruption.", "when_not": "Plain necrotic damage.",
                              "example": "Spreading Blight."}])
        path = root / "taxonomy" / "pack-dnd.yaml"
        path.write_text(path.read_text().replace(
            "      control:",
            "      corruption: {entry: {brief: Spreads corruption., when: It creates a sign., when_not: Plain necrotic damage., example: Spreading Blight.}}\n      control:"),
            encoding="utf-8")
        Enact(str(root))
        assert "purpose=corruption" in self.view(root, w)

    def test_rows_from_another_process_reach_the_view(self, root, mem):
        p, y, f = seed(mem)
        other = Enact(str(root))
        other.link(y["memory"], "task=balance", reason="from elsewhere")
        (root / "memories" / (y["slug"] + ".md")).unlink()
        mem.unlink(f["memory"], "tier=mid", reason="catches up first")   # catching up rewrites y's view
        assert "task=balance" in self.view(root, y) and "tier=mid" not in self.view(root, f)

    def test_a_stale_or_missing_view_is_regenerated_on_open(self, root, mem):
        p, y, f = seed(mem)
        path = root / "memories" / (y["slug"] + ".md")
        path.write_text(path.read_text(encoding="utf-8").replace('"tier=mid"', '"tier=low"'), encoding="utf-8")
        (root / "memories" / (f["slug"] + ".md")).unlink()
        Enact(str(root))
        assert '"tier=mid"' in self.view(root, y) and self.view(root, f) is not None

    def test_a_head_whose_text_is_gone_is_reported_at_load(self, root, mem):
        """Deleting a content object empties a memory's text in `show` and in every read, and said
        nothing (03 §3.5): one of the two silent ways a store loses knowledge."""
        from pygim.enact import digest

        p, y, f = seed(mem)
        text = mem.show(y["memory"])["text"]
        d = digest(text.encode("utf-8"))
        (root / "objects" / d[:2] / d[2:]).unlink()
        again = Enact(str(root))
        missing = [r for r in again.session()["reviews"] if r["kind"] == "text missing"]
        assert len(missing) == 1 and y["memory"] in missing[0]["text"] and y["slug"] in missing[0]["text"]
        assert again.show(y["memory"])["text"] == ""                     # the loss itself is unchanged
        assert not [r for r in Enact(str(root)).session()["reviews"] if r["kind"] == "text missing"
                    and f["memory"] in r["text"]]                        # only the memory that lost its object

    def test_a_view_edited_by_hand_is_kept_and_reported_until_the_edit_is_written(self, root, mem):
        p, y, f = seed(mem)
        path = root / "memories" / (y["slug"] + ".md")
        edited = path.read_text(encoding="utf-8").replace("beats Shield per slot.", "beats Shield per slot, at every tier.")
        path.write_text(edited, encoding="utf-8")
        again = Enact(str(root))
        again.link(y["memory"], "task=balance", reason="a tag change does not clobber the edit")
        assert path.read_text(encoding="utf-8") == edited
        reviews = [r["text"] for r in again.session()["reviews"] if r["kind"] == "view edited"]
        assert len(reviews) == 1 and y["slug"] in reviews[0]
        taken = write(again, "Shield is the yardstick", "A defensive reaction earns its slot only if it beats Shield per slot, at every tier.",
                      DESIGN + ["kind=principle"], supersedes=[y["memory"]])
        assert "at every tier" in self.view(root, taken) and "task=design" in self.view(root, taken)
        assert not [r for r in again.session()["reviews"] if r["kind"] == "view edited"]

    def test_an_up_to_date_view_is_not_rewritten(self, root, mem):
        seed(mem)
        before = {p: p.stat().st_mtime_ns for p in (root / "memories").iterdir()}
        Enact(str(root))
        assert {p: p.stat().st_mtime_ns for p in (root / "memories").iterdir()} == before

    def test_retiring_removes_the_view_and_superseding_keeps_the_chains(self, root, mem):
        p, y, f = seed(mem)
        mem.retire(f["memory"], reason="obsolete")
        assert self.view(root, f) is None
        n = write(mem, "Shield is still the yardstick", "Beat Shield per slot, and name the niche.",
                  DESIGN + ["purpose=defensive", "kind=principle"], supersedes=[y["memory"]], seen=[p["memory"], y["memory"]])
        assert n["slug"] == y["slug"] and "name the niche" in self.view(root, n)


class TestTheMailbox:
    """Messages other sessions leave in the store (03 §3.6): their own stream, not memories."""

    def test_a_message_waits_until_something_resolves_it(self, mem):
        asked = mem.post("Rebase pathlike before touching PathSet.", kind="request", to="testing",
                         about="core/pathlike-improvements", session=4)
        told = mem.post("The report's coverage numbers are stale.", kind="feedback", session=4)
        assert asked["ok"] and asked["waiting"] == 1 and told["waiting"] == 2
        assert [m["kind"] for m in mem.mailbox()] == ["request", "feedback"]        # oldest first
        assert [m["seq"] for m in mem.mailbox()] == [1, 2]                          # even within one second
        done = mem.post("Rebased; safe to touch.", resolves=asked["message"], session=5)
        assert done["ok"] and [m["id"] for m in mem.mailbox()] == [told["message"]]
        assert len(mem.mailbox(all=True)) == 3                                      # nothing is deleted
        assert [m["id"] for m in mem.mailbox(mine="testing")] == [told["message"]]  # addressed, or to nobody

    def test_a_message_is_not_a_memory(self, root, mem):
        before = (mem.version, mem.head)
        posted = mem.post("A thought for later.", session=1)
        assert (mem.version, mem.head) == before                    # no row, so no receipt changes its answer
        assert Enact(str(root)).mailbox()[0]["id"] == posted["message"]   # but it survives a reopen
        assert mem.read(DESIGN)["corpus"] == 0                      # and it is not knowledge

    def test_what_a_message_must_say(self, mem):
        assert mem.post("")["refused"] == "empty"
        assert mem.post("x", kind="shout")["refused"] == "kind"
        assert mem.post("x", resolves="0123456789ab")["refused"] == "unknown message"
        assert mem.post("x", reply_to="0123456789ab")["refused"] == "unknown message"

    def test_the_session_says_what_is_waiting(self, mem):
        mem.post("Look at the release notes.", kind="request", session=2)
        assert [m["kind"] for m in mem.session()["mailbox"]] == ["request"]


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


def ember_ward(mem, p, y, f, **kw):
    """A second case of Frost Ward's point, for a generalisation to draw on (00a Feature 7)."""
    return write(mem, "Ember Ward", "Fire resistance until the start of your next turn; wins against breath, loses to weapons.",
                 DESIGN + ["purpose=defensive", "action_economy=reaction", "kind=example", "tier=mid"],
                 seen=[p["memory"], y["memory"], f["memory"]], **kw)


PATTERN = "A defensive reaction wins against one threat and loses against another; judge it against the encounter mix."


class TestGeneralising:
    """Overview §4.11: a generalisation states what several heads share and retires none of them."""

    def test_instances_stay_heads_and_point_at_their_generalisation(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["purpose=defensive", "kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        for case in (f, e):
            shown = mem.show(case["memory"])
            assert shown["head"] and shown["generalised_by"] == [g["memory"]]
        assert mem.show(g["memory"])["generalises"] == [f["memory"], e["memory"]]
        assert set(mem.show(g["memory"])["seen"]) >= {f["memory"], e["memory"]}   # naming an instance is having read it

    def test_nothing_folds_until_a_person_accepts(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        waiting = mem.read(DESIGN, max=10)
        assert waiting["folded"] == 0 and {f["memory"], e["memory"], g["memory"]} <= {m["memory"] for m in waiting["memories"]}
        assert mem.show(g["memory"])["accepted"] is False
        assert mem.accept(g["memory"], reason="the cases share it")["ok"]
        assert mem.show(g["memory"])["accepted"] is True and mem.read(DESIGN, max=10)["folded"] == 2

    def test_accept_takes_only_a_waiting_generalisation(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        assert mem.accept(f["memory"])["refused"] == "not a generalisation"
        assert mem.accept(g["memory"])["ok"]
        assert mem.accept(g["memory"])["refused"] == "already accepted"

    def test_the_session_report_lists_what_waits_and_what_was_learnt(self, root, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f, session=5)
        loose = write(mem, "Riposte", "Thunder damage to a melee attacker.", DESIGN + ["kind=example"], session=5,
                      seen=[p["memory"], y["memory"], f["memory"], e["memory"]])
        g = write(mem, "Reactions are niches", PATTERN, ["domain=dnd", "artifact=spell", "task=any", "kind=principle"],
                  session=5, generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"], loose["memory"]])
        path = root / "reviews" / "session-5.md"
        assert path.exists()                                               # a generalisation waits in its report at once
        r = mem.lessons(5, "## Gaps\n\nThe pattern drops Frost Ward's temp-HP rider.")
        assert r["ok"] and r["report"] == str(path)
        text = path.read_text(encoding="utf-8")
        key = mem.show(g["memory"])["key"][:12]
        assert "Waiting for your acceptance" in text and f"oo enact accept {key}" in text
        assert "Claims every value of `task=any`" in text
        assert "## Left as cases" in text and "Riposte" in text
        assert "temp-HP rider" in text
        mem.accept(g["memory"], reason="checked")
        assert "**Accepted**" in path.read_text(encoding="utf-8")

    def test_acceptance_survives_a_reopen_and_is_a_person_s_command(self, root, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        key = mem.show(g["memory"])["key"][:12]
        out = CliRunner().invoke(cli_oo, ["enact", "accept", key, "--reason", "checked", "--root", str(root)],
                                 input="\n")                       # it shows the pattern; Enter accepts
        assert out.exit_code == 0, out.output
        assert "Reactions are niches" in out.output and "Frost Ward" in out.output
        assert Enact(str(root)).read(DESIGN, max=10)["folded"] == 2

    def test_accepting_shows_what_it_is_before_asking(self, root, mem):
        """Debith, 2026-09-21: \"it is impossible to me to review it. Entry 930c278ad2ec means nothing
        to me.\" The one step reserved for a person showed only a key."""
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        waiting = mem.waiting_acceptance()
        assert [(w["memory"], w["title"], [x["title"] for x in w["folds"]]) for w in waiting] == [
            (g["memory"], "Reactions are niches", ["Frost Ward", "Ember Ward"])]
        assert PATTERN.splitlines()[0] in waiting[0]["text"] and "kind=principle" in waiting[0]["tags"]

        listed = CliRunner().invoke(cli_oo, ["enact", "accept", "--root", str(root)])
        assert listed.exit_code == 0 and "Reactions are niches" in listed.output and "folds 2" in listed.output
        walked = CliRunner().invoke(cli_oo, ["enact", "accept", "--all", "--root", str(root)], input="\n")
        assert walked.exit_code == 0, walked.output
        assert "Frost Ward" in walked.output and "accepted 1 of 1" in walked.output      # Enter accepts
        assert Enact(str(root)).waiting_acceptance() == []
        assert Enact(str(root)).read(DESIGN, max=10)["folded"] == 2

    def test_nothing_is_accepted_by_answering_no(self, root, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        out = CliRunner().invoke(cli_oo, ["enact", "accept", g["key"][:12], "--root", str(root)], input="n\n")
        assert out.exit_code == 0 and "left as it is" in out.output
        assert len(Enact(str(root)).waiting_acceptance()) == 1
        forced = CliRunner().invoke(cli_oo, ["enact", "accept", g["key"][:12], "--yes", "--root", str(root)])
        assert forced.exit_code == 0 and "fold from the next read" in forced.output       # scripts say so

    def test_a_read_folds_instances_under_their_generalisation(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        mem.accept(g["memory"])
        r = mem.read(DESIGN, max=10)
        placed = {m["memory"]: m for m in r["memories"]}
        assert f["memory"] not in placed and e["memory"] not in placed
        assert [x["memory"] for x in placed[g["memory"]]["evidence"]] == [f["memory"], e["memory"]]
        assert r["folded"] == 2 and r["candidates"] == 5                  # folded cases were still admitted
        ranks = sorted(m["rank"] for m in r["memories"])
        assert ranks == list(range(1, len(ranks) + 1)) and r["skipped"] == 0   # folded before ranking: no gaps

    def test_the_evidence_is_named_not_paid_for(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        mem.accept(g["memory"])
        full = mem.read(DESIGN, max=10)
        paid = full["procedure"]["tokens"] + sum(m["tokens"] for m in full["memories"])
        assert full["tokens"] == paid                                      # the folded cases cost nothing
        tight = mem.read(DESIGN, max=10, budget=paid)                      # exactly what the placed memories need
        placed = {m["memory"]: m for m in tight["memories"]}
        assert g["memory"] in placed and len(placed[g["memory"]]["evidence"]) == 2 and not tight["skipped"]

    def test_a_retired_generalisation_unfolds_its_instances(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        mem.accept(g["memory"])
        assert mem.read(DESIGN, max=10)["folded"] == 2
        assert mem.retire(g["memory"], reason="the pattern was wrong")["ok"]
        r = mem.read(DESIGN, max=10)
        assert {f["memory"], e["memory"]} <= {m["memory"] for m in r["memories"]} and r["folded"] == 0

    def test_one_case_is_not_a_pattern(self, mem):
        p, y, f = seed(mem)
        r = mem.remember(title="Too soon", text=PATTERN, tags=DESIGN + ["kind=principle"],
                         generalises=[f["memory"]], seen=[p["memory"], y["memory"]])
        assert r["refused"] == "one case"

    def test_it_must_cover_its_instances_and_any_covers_everything(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        b = write(mem, "Stone Skin", "Resists weapons, loses to casters.", DESIGN + ["task=balance", "kind=example"],
                  seen=[p["memory"], y["memory"], f["memory"], e["memory"]])
        narrow = mem.remember(title="Reactions are niches", text=PATTERN, tags=DESIGN + ["kind=principle"],
                              generalises=[f["memory"], b["memory"]], seen=[p["memory"], y["memory"], e["memory"]])
        assert narrow["refused"] == "not covered"
        assert narrow["facts"] == [b["memory"] + " " + mem.show(b["memory"])["key"][:8] + " Stone Skin answers task=balance"]
        wide = mem.remember(title="Reactions are niches", text=PATTERN, tags=["domain=dnd", "artifact=spell", "task=any", "kind=principle"],
                            generalises=[f["memory"], b["memory"]], seen=[p["memory"], y["memory"], e["memory"]])
        assert wide["ok"]   # the check cannot see overreach — review in the diff does (00a Scenario 7.2)

    def test_only_heads_and_never_what_it_supersedes(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        stale = mem.remember(title="Reactions are niches", text=PATTERN, tags=DESIGN + ["kind=principle"],
                             generalises=[f["memory"], e["memory"]], supersedes=[f["memory"]])
        assert stale["refused"] == "evidence"
        v2 = write(mem, "Frost Ward, again", "Cold resistance; a niche, not an upgrade.", DESIGN + ["kind=example"],
                   supersedes=[f["memory"]])
        old = mem.remember(title="Reactions are niches", text=PATTERN, tags=DESIGN + ["kind=principle"],
                           generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"], v2["memory"]])
        assert old["refused"] == "not a head" and old["facts"][0].startswith(v2["memory"])

    def test_review_lists_what_a_session_wrote(self, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f, session=5)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"], session=5,
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        written = mem.review(5)["written"]
        assert [w["title"] for w in written] == ["Ember Ward", "Reactions are niches"]
        assert written[0]["generalised_by"] == [g["memory"]] and written[1]["generalises"] == [f["memory"], e["memory"]]
        assert mem.review(6)["written"] == []

    def test_the_edge_survives_a_reopen_and_lands_in_the_head_view(self, root, mem):
        p, y, f = seed(mem)
        e = ember_ward(mem, p, y, f)
        g = write(mem, "Reactions are niches", PATTERN, DESIGN + ["kind=principle"],
                  generalises=[f["memory"], e["memory"]], seen=[p["memory"], y["memory"]])
        again = Enact(str(root))
        assert again.show(f["memory"])["generalised_by"] == [g["memory"]] and again.show(f["memory"])["head"]
        view = (root / "memories" / (g["slug"] + ".md")).read_text(encoding="utf-8")
        assert "generalises: " in view


class TestRestartsAndReruns:
    def test_a_reopened_store_is_the_same_snapshot(self, root, mem):
        seed(mem)
        again = Enact(str(root))
        assert (again.head, again.version) == (mem.head, mem.version)

    def test_a_receipt_reruns_to_the_same_answer_after_the_store_moved_on(self, root, mem):
        p, y, f = seed(mem)
        receipt = mem.read(DESIGN, ["purpose=defensive"])["receipt"]
        write(mem, "Newer", "a newer principle", DESIGN + ["purpose=defensive", "kind=principle"],
              seen=[p["memory"], y["memory"], f["memory"]])
        again = Enact(str(root))
        rerun = again.rerun(receipt)
        assert rerun["same"] and rerun["version"] == receipt["version"]
        assert len(again.read(DESIGN, ["purpose=defensive"])["memories"]) == 3

    def test_two_sessions_on_one_clone_rerun_the_checks_under_the_lock(self, root, mem):
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"])
        other = Enact(str(root))                               # a second process on the same clone
        assert other.remember(title="Mine", text="written first", tags=DESIGN + ["kind=principle"], seen=[p["memory"]])["ok"]
        r = mem.remember(title="Yours", text="written second", tags=DESIGN + ["kind=principle"], seen=[p["memory"]])
        assert r["refused"] == "unread" and "Mine" in r["facts"][0]

    def test_git_merging_two_clones_gives_one_snapshot_whoever_merges(self, tmp_path, root, mem):
        p = write(mem, "Creating a spell", "steps", DESIGN + ["kind=procedure"])
        theirs = tmp_path / "theirs"
        shutil.copytree(root, theirs)
        (theirs / "local" / "clone").unlink()                  # a different person's clone
        mine_w = write(mem, "Mine", "my principle", DESIGN + ["kind=principle"], seen=[p["memory"]])
        their_mem = Enact(str(theirs))
        their_w = write(their_mem, "Theirs", "their principle", DESIGN + ["kind=principle"], seen=[p["memory"]])
        mine_clone = (root / "local" / "clone").read_text().strip()
        their_clone = (theirs / "local" / "clone").read_text().strip()
        for src, dst, clone in ((theirs, root, their_clone), (root, theirs, mine_clone)):   # what git brings each way
            shutil.copy2(src / "audit" / f"{clone}.jsonl", dst / "audit" / f"{clone}.jsonl")  # each clone appends only to its own file
            shutil.copytree(src / "objects", dst / "objects", dirs_exist_ok=True)           # content-addressed: never conflicts
        a, b = Enact(str(root)), Enact(str(theirs))
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

    def test_a_corpus_saved_with_windows_line_endings_is_the_same_corpus(self, tmp_path, mem):
        """A checkout made with git's autocrlf holds \\r\\n on disk. The body kept the \\r of every
        line, so the same notes read on Windows were other memories, with other digests, than on
        Linux (first CI run on Windows, 2026-09-28)."""
        path = tmp_path / "spells.md"
        path.write_bytes(self.CORPUS.replace("\n", "\r\n").encode("utf-8"))
        assert mem.ingest(str(path))["added"] == 1
        assert [m["text"] for m in mem.read(DESIGN)["memories"]] == [
            "Shield is the reference point for any reaction that reduces incoming harm."]
        path.write_bytes(self.CORPUS.encode("utf-8"))
        assert mem.ingest(str(path))["unchanged"] == 1
