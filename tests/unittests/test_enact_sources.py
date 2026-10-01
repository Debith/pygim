# -*- coding: utf-8 -*-
"""A store's sources: the inventory of documents its vocabulary and memories cite, and the citations.

Every defect here was found on 2026-09-23 by fresh review sessions on `_pygim/_mcp/_packs.py` — a
file no memory described — with the review procedure delivered at the request (global #39 has how
they were run). Each test fails on the code as it stood then.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from pygim.enact import Enact, digest
from _pygim._mcp import _packs

README = "one\ntwo\nthree\n"                     # three lines, and the newline that ends the last


@pytest.fixture
def project(tmp_path):
    root = tmp_path / "proj"
    root.mkdir()
    (root / "README.md").write_text(README, encoding="utf-8")
    (root / "OTHER.md").write_text("alpha\nbeta\n", encoding="utf-8")
    return root


@pytest.fixture
def store(tmp_path, project):
    root = tmp_path / "store"
    Enact.init(str(root))
    (root / "sources").mkdir(exist_ok=True)
    (root / "sources" / "inventory.yaml").write_text(
        "readme:\n  kind: text\n  path: README.md\nother:\n  kind: text\n  path: OTHER.md\n", encoding="utf-8")
    return root


class TestALineMustExist:
    """A file that ends with a newline split on "\\n" yields one more element than it has lines. That
    empty element was accepted as a line, and every such line digests to digest(b"") — so a locator
    to it verified against any document at all."""

    def test_the_line_after_the_last_is_not_a_line(self, project, store):
        assert _packs.cite(project, "README.md", 3)["text"] == "three"
        with pytest.raises(ValueError, match="has 3 lines"):
            _packs.cite(project, "README.md", 4)
        with pytest.raises(ValueError, match="has 3 lines"):
            _packs.passage(project, store, "readme:L4")

    def test_line_zero_and_an_empty_span_are_refused(self, project, store):
        with pytest.raises(ValueError):
            _packs.passage(project, store, "readme:L0")
        for lines in (0, -1):
            with pytest.raises(ValueError):
                _packs.cite(project, "README.md", 2, lines=lines)

    def test_a_blank_passage_is_refused_because_it_matches_everything(self, project):
        (project / "GAPS.md").write_text("a\n\nb\n", encoding="utf-8")
        with pytest.raises(ValueError, match="blank"):
            _packs.cite(project, "GAPS.md", 2)

    def test_a_pack_citing_a_blank_passage_is_warned_about(self, project, store):
        blank = {"doc": "readme", "line": 2, "lines": 1, "passage": digest(b""), "tag": "artifact=page"}
        (project / "README.md").write_text("one\n\nthree\n", encoding="utf-8")
        warnings = _packs._locator_warnings([blank], store, store / "none.yaml", project)
        assert warnings and "blank" in warnings[0]


class TestTheInventoryIsYaml:
    """It was read with two regular expressions. pygim ships a YAML engine, and the reviews showed the
    hand reader getting four of five valid forms wrong."""

    def test_every_valid_form_reads_as_yaml_says(self, tmp_path):
        listed = tmp_path / "inventory.yaml"
        listed.write_text(
            "# a comment\n"
            "readme:\n  kind: text\n  path: README.md   # the readme\n"
            "\"my doc\":\n  kind: text\n  path: \"docs/a b.md\"\n"
            "flow: {kind: text, path: FLOW.md}\n"
            "nested:\n  kind: text\n  path: REAL.md\n  structure:\n    path: derived.json\n"
            "next_line:\n  kind: text\n  path:\n    NEXT.md\n", encoding="utf-8")
        assert _packs.inventory(listed) == {"readme": "README.md", "my doc": "docs/a b.md", "flow": "FLOW.md",
                                            "nested": "REAL.md", "next_line": "NEXT.md"}

    def test_a_missing_or_empty_inventory_lists_nothing(self, tmp_path):
        assert _packs.inventory(tmp_path / "none.yaml") == {}
        (tmp_path / "empty.yaml").write_text("# nothing yet\n", encoding="utf-8")
        assert _packs.inventory(tmp_path / "empty.yaml") == {}


class TestAMergeKeepsWhatWasThere:
    def test_an_inventory_without_a_final_newline_keeps_its_last_entry(self, store, tmp_path):
        live = store / "sources" / "inventory.yaml"
        live.write_text("readme:\n  kind: text\n  path: README.md", encoding="utf-8")      # no final newline
        drafted = tmp_path / "draft" / "inventory.yaml"
        drafted.parent.mkdir()
        drafted.write_text("guide:\n  kind: text\n  path: GUIDE.md\n", encoding="utf-8")
        added, kept = _packs._merge_inventory(store, drafted)
        assert added == ["guide"] and kept == []
        assert _packs.inventory(live) == {"readme": "README.md", "guide": "GUIDE.md"}

    def test_a_merged_entry_is_written_as_valid_yaml_whatever_its_path(self, store, tmp_path):
        drafted = tmp_path / "draft" / "inventory.yaml"
        drafted.parent.mkdir()
        drafted.write_text("odd:\n  kind: text\n  path: 'docs/x: y #z.md'\n", encoding="utf-8")
        _packs._merge_inventory(store, drafted)
        assert _packs.inventory(store / "sources" / "inventory.yaml")["odd"] == "docs/x: y #z.md"


class TestTheCheckAnswersForWhatTheAcceptWillDo:
    """`accept` keeps the store's path for a document it already lists — a pack must not move another
    pack's documents — but `check` resolved the draft's path, so it warned about the wrong file in one
    direction and passed a dead one in the other. The check now answers for what the accept does."""

    def page(self, project, doc="readme"):
        return {"doc": doc, "line": 1, "lines": 1, "tag": "artifact=page",
                "passage": _packs.cite(project, "README.md", 1)["source"]["passage"]}

    def test_the_stores_live_path_is_what_is_checked(self, project, store, tmp_path):
        drafted = tmp_path / "draft" / "inventory.yaml"
        drafted.parent.mkdir()
        drafted.write_text("readme:\n  kind: text\n  path: docs/GONE.md\n", encoding="utf-8")
        warnings = _packs._locator_warnings([self.page(project)], store, drafted, project)
        assert not any("not found" in w for w in warnings)                  # the live README.md is checked
        assert any("the store keeps README.md" in w for w in warnings)      # and the ignored path is named

    def test_a_dead_path_the_store_keeps_is_reported_even_when_the_draft_has_a_live_one(self, project, store, tmp_path):
        (store / "sources" / "inventory.yaml").write_text("readme:\n  kind: text\n  path: GONE.md\n", encoding="utf-8")
        drafted = tmp_path / "draft" / "inventory.yaml"
        drafted.parent.mkdir()
        drafted.write_text("readme:\n  kind: text\n  path: README.md\n", encoding="utf-8")
        warnings = _packs._locator_warnings([self.page(project)], store, drafted, project)
        assert any("GONE.md was not found" in w for w in warnings)


class TestADocumentKeepsItsOwnId:
    def test_two_paths_that_flatten_to_one_id_get_two_ids(self, project, store):
        (project / "docs" / "a").mkdir(parents=True)
        (project / "docs" / "a-b.md").write_text("first\n", encoding="utf-8")
        (project / "docs" / "a" / "b.md").write_text("second\n", encoding="utf-8")
        (store / "sources" / "inventory.yaml").write_text("docs-a-b:\n  kind: text\n  path: docs/a-b.md\n",
                                                          encoding="utf-8")
        assert _packs.cite(project, "docs/a-b.md", 1, store=store)["source"]["doc"] == "docs-a-b"
        other = _packs.cite(project, "docs/a/b.md", 1, store=store)["source"]["doc"]
        assert other != "docs-a-b" and other.startswith("docs-a-b")


class TestTheStoreOwnsItsBytes:
    """Accepting a pack copied it into `taxonomy/` and appended to the inventory from outside the
    store — past the commit lock every other write takes, and past any strategy that changes how
    bytes are stored. The store writes them itself now, and only over what the caller read."""

    def test_a_pack_is_written_when_nothing_changed_since_it_was_read(self, store):
        memory = Enact(str(store))
        assert memory.write_file("taxonomy/pack-x.yaml", "pack: x\n", "")
        assert (store / "taxonomy" / "pack-x.yaml").read_text(encoding="utf-8") == "pack: x\n"

    def test_a_file_changed_since_it_was_read_is_not_written_over(self, store):
        memory = Enact(str(store))
        live = store / "sources" / "inventory.yaml"
        read = digest(live.read_bytes())
        live.write_text(live.read_text(encoding="utf-8") + "late:\n  path: LATE.md\n", encoding="utf-8")
        assert memory.write_file("sources/inventory.yaml", "readme:\n  path: README.md\n", read) is False
        assert "late:" in live.read_text(encoding="utf-8")                      # the other writer's entry stands

    @pytest.mark.parametrize("relative", ["memories/x.md", "taxonomy/../x.yaml",
                                          "/tmp/x.yaml", "sources/other.yaml"])
    def test_nothing_else_is_written_this_way(self, store, relative):
        with pytest.raises(Exception, match="only its base vocabulary, packs and source inventory"):
            Enact(str(store)).write_file(relative, "x", "")

    def test_the_stores_own_reader_sees_every_id_yaml_does(self, store):
        (store / "sources" / "inventory.yaml").write_text(
            '"my doc":\n  path: A.md\nflow: {path: B.md}\nplain:\n  path: C.md\n', encoding="utf-8")
        coverage = Enact(str(store)).read(["task=design"], [])["coverage"]
        assert set(coverage["not_cited"]) == {"my doc", "flow", "plain"}
