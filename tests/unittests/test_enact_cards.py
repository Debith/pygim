# -*- coding: utf-8 -*-
"""A memory is a card and a body.

The card — the title, which is the rule, and `when`, `not`, `do`, `why` — is what reaches a session
without being asked for, so it is small. The body keeps the detail and is one `show` away. Standing
knowledge was 22.8 KB of full texts, delivered twice per session; as cards it is a quarter of that
(global memory "Write a memory as a card and a body").
"""
from __future__ import annotations

import json

import pytest

from _pygim._mcp import _cards
from _pygim._mcp.enact import build
from pygim.enact import Enact

TAGS = ["domain=any", "artifact=any", "task=design", "kind=principle"]


class TestTheRecord:
    def test_a_card_and_a_body_survive_being_written_and_read_back(self):
        text = _cards.compose({"when": "Reviewing code.", "not": "Throwaway scripts.",
                               "do": "Run `oo inventory`.", "why": "What you have is invisible at the call site."},
                              "Example: pathlib where pathlike ships.\n\nEvidence: 2026-09-23.")
        card = _cards.parse(text)
        assert (card.when, card.not_, card.do, card.why) == (
            "Reviewing code.", "Throwaway scripts.", "Run `oo inventory`.", "What you have is invisible at the call site.")
        assert card.body.startswith("Example: pathlib") and "Evidence: 2026-09-23." in card.body
        assert not card.legacy and card.missing() == []

    def test_a_procedure_keeps_its_steps_in_order_and_its_request_words(self):
        text = _cards.compose({"when": "Asked to review.", "why": "Frames are missed.", "asked": "review, analyse, audit",
                               "steps": ["Discover the project — check: you can name what it ships.",
                                         "Read the store — check: global too."]})
        card = _cards.parse(text)
        assert card.steps == ["Discover the project — check: you can name what it ships.",
                              "Read the store — check: global too."]
        assert card.words() == ["review", "analyse", "audit"]
        assert card.missing("procedure") == []

    def test_a_long_field_may_wrap_onto_an_indented_line(self):
        card = _cards.parse("When: reviewing code\n  or a design.\nWhy: frames get missed.\n\nBody.")
        assert card.when == "reviewing code or a design." and card.body == "Body."

    def test_text_written_before_the_format_is_all_body_and_says_so(self):
        card = _cards.parse("Debith, 2026-09-16: \"a quote\".\n\nOne line: explain in layers.")
        assert card.legacy and card.body.startswith("Debith")
        assert _cards.summary(card.body) == "explain in layers."          # its own one-line, when it has one

    def test_a_card_missing_its_when_or_why_says_which(self):
        assert _cards.parse("When: x\n\nbody").missing() == ["why"]
        assert _cards.parse("Why: y").missing("procedure") == ["when", "steps"]


class TestTheCardAsDelivered:
    def test_a_card_is_the_rule_and_one_line_of_when_not_do_why(self):
        memory = {"memory": "#65", "title": "Run `oo enact reload` without asking",
                  "text": _cards.compose({"when": "the server must pick up new code", "not": "a server you do not own",
                                          "why": "a stale server answers with old behaviour"}, "Debith, 2026-09-21.")}
        shown = _cards.render(memory)
        assert shown.splitlines() == [
            "#65 Run `oo enact reload` without asking",
            "    when: the server must pick up new code · not: a server you do not own · "
            "why: a stale server answers with old behaviour"]
        assert "Debith, 2026-09-21." not in shown                               # the body stays behind

    def test_a_procedure_card_names_its_steps_without_spending_them(self):
        memory = {"memory": "#3", "title": "Review what exists", "text": _cards.compose(
            {"when": "asked to review", "why": "frames get missed", "asked": "review, analyse",
             "steps": ["one", "two", "three"]})}
        assert "3 steps, asked as: review, analyse" in _cards.render(memory, "global")
        assert _cards.render(memory, "global").startswith("#3 (global) Review what exists")

    def test_a_memory_not_yet_a_card_is_delivered_as_a_line_and_says_so(self):
        memory = {"memory": "#4", "title": "A smiley is a joke", "text": "A smiley means banter. Answer in kind."}
        assert _cards.render(memory).splitlines()[1] == "    A smiley means banter. — not yet a card; `show` for the rest"


class TestTheToolEnforcesIt:
    """The template lives in the `remember` schema, so every writing agent sees it — and the server
    refuses a write without it, because a rule that is only described is a rule that is skipped
    (global memory #20: the check, not the telling)."""

    @pytest.fixture
    def server(self, tmp_path):
        from _pygim import _config

        root = tmp_path / "store"
        Enact.init(str(root))
        where = _config.Environment(cwd=tmp_path, home=tmp_path / "home", user_data=tmp_path / "data",
                                    global_root=tmp_path / "no-global")
        return build(where, Enact(str(root)))

    def call(self, server, name, **arguments):
        out = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                             "params": {"name": name, "arguments": arguments}})["result"]
        return json.loads(out["content"][0]["text"])

    def test_a_write_without_a_card_is_refused_with_what_to_add(self, server):
        said = self.call(server, "remember", title="A rule", text="It holds.", tags=TAGS)
        assert said["refused"] == "template"
        assert [fact.split(":")[0] for fact in said["facts"]] == ["when", "why"]

    def test_named_fields_become_the_labelled_head_of_the_stored_text(self, server):
        made = self.call(server, "remember", title="Prefer what the project ships", tags=TAGS,
                         when="Reaching for a library", why="What you have is invisible at the call site.",
                         text="Example: pathlib where pathlike ships.")
        assert made["ok"], made
        shown = self.call(server, "show", memory=made["memory"])
        assert shown["text"].startswith("When: Reaching for a library\nWhy: What you have is invisible")
        assert shown["text"].endswith("Example: pathlib where pathlike ships.")

    def test_a_shell_may_write_the_labels_inside_the_text_instead(self, server):
        made = self.call(server, "remember", title="A rule", tags=TAGS,
                         text="When: always.\nWhy: because.\n\nThe body.")
        assert made["ok"], made

    def test_a_procedure_without_steps_is_refused(self, server):
        said = self.call(server, "remember", title="Reviewing", when="asked to review", why="frames get missed",
                         tags=["domain=any", "artifact=any", "task=critique", "kind=procedure"])
        assert said["refused"] == "template" and said["facts"][0].startswith("steps:")

    def test_a_merge_keeps_to_the_same_template(self, server):
        a = self.call(server, "remember", title="One", tags=TAGS, when="w", why="y", text="a")
        b = self.call(server, "remember", title="Two", tags=TAGS, when="w", why="y", text="b", seen=[a["memory"]])
        said = self.call(server, "merge", memories=[a["memory"], b["memory"]], title="Both", text="ab",
                         reason="one thing")
        assert said["refused"] == "template"


class TestTheBudget:
    """25 full cards rendered to 14,457 characters, and with the project map the session-start text
    reached 16,127 — past the ~13 KB at which the host moves a hook's output to a file behind a 2 KB
    preview. That is the failure the cards were written to end, so they are rendered to a budget."""

    def card(self, n, kind="preference"):
        fields = {"when": "w" * 90, "not": "n" * 90, "do": "d" * 150, "why": "y" * 120}
        if kind == "procedure":
            fields.update(asked="review, analyse", steps=["a step — check: a question?"] * 5)
        return {"memory": f"#{n}", "title": "A rule stated in the imperative " + "x" * 50,
                "text": _cards.compose(fields)}

    def test_everything_fits_at_full_length_when_there_is_room(self):
        prefs, procs, left_out = _cards.standing([(self.card(1), "global")], [], budget=10_000)
        assert left_out == [] and "do: " + "d" * 150 in prefs[0]

    def test_too_many_cards_drop_fields_in_order_and_say_which(self):
        prefs = [(self.card(n), "project") for n in range(20)]
        procs = [(self.card(100 + n, "procedure"), "global") for n in range(8)]
        pref_cards, proc_cards, left_out = _cards.standing(prefs, procs, budget=7_500)
        assert sum(len(c) + 1 for c in pref_cards + proc_cards) <= 7_500
        assert left_out[:2] == ["not", "do"]                         # what a `show` restores most cheaply
        assert all("when: " in c and "why: " in c for c in pref_cards)   # the rule, when and why survive
        assert all("asked as: review, analyse" in c for c in proc_cards)  # a procedure keeps its trigger

    def test_when_nothing_fits_the_titles_still_arrive_and_it_says_so(self):
        prefs = [(self.card(n), "project") for n in range(200)]
        pref_cards, _, left_out = _cards.standing(prefs, [], budget=1_000)
        assert left_out == ["everything but the titles"] and pref_cards[0].startswith("#0 A rule")


class TestAReasonIsGivenUpOneCardAtATime:
    """Dropping `why` from every card at once meant two new procedure cards cost fifteen preferences
    their reasons (2026-09-23). So the last step drops it from the longest first, only until it fits."""

    def card(self, n, why_length):
        return {"memory": f"#{n}", "title": f"Rule {n}",
                "text": _cards.compose({"when": "w" * 40, "not": "n" * 60, "do": "d" * 60, "why": "y" * why_length})}

    def test_only_as_many_reasons_go_as_the_budget_needs_longest_first(self):
        prefs = [(self.card(n, 60 + n * 10), "project") for n in range(10)]
        when_and_why = [_cards.render(m, s, ("when", "why")) for m, s in prefs]
        needed = sum(len(c) + 1 for c in when_and_why) - 150       # a little too short for every reason
        cards, _, left_out = _cards.standing(prefs, [], budget=needed)
        kept = [c for c in cards if "why: " in c]
        assert 0 < len(kept) < 10                                  # some kept, some given up
        assert "why: " not in cards[9] and "why: " in cards[0]     # the longest went first
        assert left_out[-1] == f"why on {10 - len(kept)} of 10 preferences"
