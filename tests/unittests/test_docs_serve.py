# -*- coding: utf-8 -*-
"""Tests for ``oo docs serve`` — the static docs server with the ✎ review layer."""

import io
import json
import os
import re
import shutil
import subprocess
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

import click
import pygim
import pytest
from click.testing import CliRunner

from _pygim._cli import _commenter, _docs_serve
from pygim.pathlike import PathStore, default_store
from _pygim._cli._cli_app import GimmicksCliApp
from pygim.__main__ import cli_oo


@pytest.fixture
def site(temp_dir):
    """A tiny docs tree: a root page, a nested page, a css file, a site/ landing page."""
    (temp_dir / "page.html").write_text("<html><body><h1>Hi</h1></body></html>", encoding="utf-8")
    (temp_dir / "nested").mkdir()
    (temp_dir / "nested" / "index.html").write_text("<p>nested</p>", encoding="utf-8")
    (temp_dir / "style.css").write_text("body{}", encoding="utf-8")
    (temp_dir / "site").mkdir()
    (temp_dir / "site" / "index.html").write_text("<body>landing</body>", encoding="utf-8")
    return temp_dir


def _mermaid_blocks_as_a_browser_reads_them(page):
    """Each `<pre class="mermaid">` as a browser parses it: its text with entities decoded — what
    Mermaid's `entityDecode(innerHTML)` yields — and how many elements the parser made inside it,
    which must be none."""
    from html.parser import HTMLParser

    class Reader(HTMLParser):
        def __init__(self):
            super().__init__(convert_charrefs=True)
            self.blocks, self.inside = [], False

        def handle_starttag(self, tag, attrs):
            if tag == "pre" and ("class", "mermaid") in attrs:
                self.inside = True
                self.blocks.append(["", 0])
            elif self.inside:
                self.blocks[-1][1] += 1

        def handle_endtag(self, tag):
            if tag == "pre":
                self.inside = False

        def handle_data(self, data):
            if self.inside:
                self.blocks[-1][0] += data

    reader = Reader()
    reader.feed(page)
    return [(text, elements) for text, elements in reader.blocks]


@pytest.fixture
def server(site):
    """A bound, running server on a free localhost port; yields its base URL."""
    httpd = _docs_serve.make_server(site, port=0, host="127.0.0.1")
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{httpd.server_address[1]}"
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=5)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):
        return None


def _get(url, *, follow=True):
    opener = urllib.request.build_opener() if follow else urllib.request.build_opener(_NoRedirect)
    try:
        with opener.open(url) as resp:
            return resp.status, resp.headers, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


def _post(url, payload, *, raw=None):
    data = raw if raw is not None else json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


class TestCommenterInjection:
    def test_html_page_gets_commenter(self, server):
        status, headers, body = _get(server + "/page.html")
        assert status == 200
        assert headers["Content-Type"].startswith("text/html")
        text = body.decode("utf-8")
        assert 'id="cmt-tab"' in text
        assert text.count('id="cmt-tab"') == 1
        assert "<h1>Hi</h1>" in text
        assert text.index("<h1>Hi</h1>") < text.index('id="cmt-tab"')  # appended before </body>

    def test_directory_index_gets_commenter(self, server):
        status, _, body = _get(server + "/nested/")
        assert status == 200
        assert 'id="cmt-tab"' in body.decode("utf-8")

    def test_non_html_untouched(self, server):
        status, _, body = _get(server + "/style.css")
        assert status == 200
        assert body == b"body{}"

    def test_inject_is_idempotent(self):
        once = _commenter.inject("<body>x</body>")
        assert _commenter.inject(once) == once
        assert _commenter.inject("no body tag").endswith(_commenter.COMMENTER + _commenter.READER + _commenter.DIAGRAMS)


class TestPages:
    def test_pages_lists_the_sites_html(self, server, site):
        (site / "__notes__").mkdir()
        (site / "__notes__" / "draft.html").write_text("<p>not a page</p>", encoding="utf-8")
        status, _, body = _get(server + "/pages")
        assert status == 200
        assert json.loads(body) == ["/nested/index.html", "/page.html", "/site/index.html"]

    def test_requests_intern_into_the_servers_store_not_the_default(self, site):
        rows = default_store().stats()["rows"]
        store = PathStore()
        httpd = _docs_serve.make_server(site, port=0, host="127.0.0.1", store=store)
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{httpd.server_address[1]}"
            for url in ("/page.html", "/nested/", "/nope/probe.html", "/pages"):
                _get(base + url)
            assert default_store().stats()["rows"] == rows           # untouched by request traffic
            assert store.stats()["rows"] > 0                          # it all went here
        finally:
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)


class TestMarkdown:
    def test_opening_markdown_generates_html_beside_it_and_redirects(self, server, site):
        (site / "design.md").write_text("# Title\n\nSome *text*.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n", encoding="utf-8")
        status, headers, _ = _get(server + "/design.md", follow=False)
        assert status == 302 and headers["Location"] == "/design.generated.html"
        generated = (site / "design.generated.html").read_text(encoding="utf-8")
        assert "generated from design.md by oo docs serve" in generated
        assert "<h1" in generated and "<em>text</em>" in generated and "<table" in generated
        assert 'id="cmt-tab"' not in generated                         # the commenter is injected when served, not written
        status, headers, body = _get(server + "/design.md")            # following the redirect: the HTML page, with the commenter
        assert status == 200 and 'id="cmt-tab"' in body.decode("utf-8") and "<title>design</title>" in body.decode("utf-8")

    def test_regenerated_only_when_the_markdown_is_newer(self, site):
        import os
        import time
        md = site / "note.md"
        md.write_text("# one\n", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        out = _docs_serve.materialize_markdown(root / "note.md")
        first = os.fspath(out)
        assert "<h1" in (site / "note.generated.html").read_text(encoding="utf-8") \
            and "one" in (site / "note.generated.html").read_text(encoding="utf-8")
        stamp = os.path.getmtime(first)
        _docs_serve.materialize_markdown(root / "note.md")
        assert os.path.getmtime(first) == stamp                        # fresh: untouched
        time.sleep(0.05)
        md.write_text("# two\n", encoding="utf-8")
        os.utime(md, None)
        _docs_serve.materialize_markdown(root / "note.md")
        assert "two" in (site / "note.generated.html").read_text(encoding="utf-8")   # stale: regenerated

    def test_a_generated_page_without_the_marker_is_never_overwritten(self, site):
        (site / "mine.md").write_text("# from markdown\n", encoding="utf-8")
        (site / "mine.generated.html").write_text("<body>hand-written</body>", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        out = _docs_serve.materialize_markdown(root / "mine.md")
        assert out.name == "mine.generated.html"
        assert (site / "mine.generated.html").read_text(encoding="utf-8") == "<body>hand-written</body>"

    def test_a_hand_written_html_is_left_alone_entirely(self, site):
        """The generated name is its own place, so ``x.html`` beside ``x.md`` is a page
        in its own right and the generator never contends with it."""
        (site / "mine.md").write_text("# from markdown\n", encoding="utf-8")
        (site / "mine.html").write_text("<body>hand-written</body>", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        _docs_serve.materialize_markdown(root / "mine.md")
        assert (site / "mine.html").read_text(encoding="utf-8") == "<body>hand-written</body>"
        assert "from markdown" in (site / "mine.generated.html").read_text(encoding="utf-8")

    def test_markdown_links_point_at_generated_pages(self, site):
        (site / "a.md").write_text("see [b](b.md#part) and [ext](https://x.y/z.md) and [raw](/abs.md)\n", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        html = (site / _docs_serve.materialize_markdown(root / "a.md").name).read_text(encoding="utf-8")
        assert 'href="b.generated.html#part"' in html and 'href="https://x.y/z.md"' in html and 'href="/abs.md"' in html

    def test_mermaid_fence_becomes_a_live_diagram(self, site):
        """Mermaid reads the block as the browser parsed it: `innerHTML`, entities decoded. The
        test asserted the raw HTML string instead, so the generator unescaped the block, and the
        browser then read a stereotype such as `<<strategy>>` as an HTML element — every class
        diagram with one showed "Syntax error in text" (ENACT 03 §7, 2026-09-23)."""
        source = "classDiagram\n  class A {\n    <<strategy>>\n    keeps rows\n  }\n  A <|.. B\n  A --> C : uses\n"
        (site / "diagram.md").write_text(f"```mermaid\n{source}```\n", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        html = (site / _docs_serve.materialize_markdown(root / "diagram.md").name).read_text(encoding="utf-8")
        assert "mermaid.esm.min.mjs" in html
        assert _mermaid_blocks_as_a_browser_reads_them(html) == [(source, 0)]

    def test_markdown_index_stands_in_for_a_missing_index_html(self, temp_dir):
        (temp_dir / "README.md").write_text("# Home\n", encoding="utf-8")
        httpd = _docs_serve.make_server(temp_dir, port=0, host="127.0.0.1")
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{httpd.server_address[1]}"
            status, headers, _ = _get(base + "/", follow=False)
            assert status == 302 and headers["Location"] == "/README.generated.html"
            body = _get(base + "/")[2].decode("utf-8")
            assert "<h1" in body and 'id="cmt-tab"' in body and (temp_dir / "README.generated.html").is_file()
        finally:
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_pages_list_html_and_unconverted_markdown_only(self, server, site):
        (site / "notes.md").write_text("x", encoding="utf-8")
        (site / "done.md").write_text("y", encoding="utf-8")
        assert _get(server + "/done.md", follow=False)[0] == 302          # generates done.generated.html
        pages = json.loads(_get(server + "/pages")[2])
        assert "/notes.md" in pages and "/done.generated.html" in pages and "/done.md" not in pages

    def test_startup_generates_every_markdown_page(self, temp_dir):
        (temp_dir / "a.md").write_text("# a\n", encoding="utf-8")
        (temp_dir / "sub").mkdir()
        (temp_dir / "sub" / "b.md").write_text("# b\n", encoding="utf-8")
        (temp_dir / "__notes__").mkdir()
        (temp_dir / "__notes__" / "n.md").write_text("# n\n", encoding="utf-8")
        httpd = _docs_serve.make_server(temp_dir, port=0, host="127.0.0.1")
        try:
            assert (temp_dir / "a.generated.html").is_file() and (temp_dir / "sub" / "b.generated.html").is_file()
            assert not (temp_dir / "__notes__" / "n.generated.html").exists()    # notes are not pages
            root = _docs_serve.pygim.path(temp_dir, store=PathStore())
            assert _docs_serve.pregenerate(root) == 0                         # everything fresh: nothing rewritten
        finally:
            httpd.server_close()

    def test_render_without_the_package_is_none(self, monkeypatch):
        import builtins
        real = builtins.__import__
        monkeypatch.setattr(builtins, "__import__",
                            lambda name, *a, **k: (_ for _ in ()).throw(ImportError()) if name == "markdown" else real(name, *a, **k))
        assert _docs_serve.render_markdown("# x", "x") is None


class TestRootRedirect:
    def test_root_redirects_to_site_index_when_root_has_none(self, server):
        status, headers, _ = _get(server + "/", follow=False)
        assert status == 302
        assert headers["Location"] == "/site/index.html"

    def test_root_served_directly_when_index_exists(self, site):
        (site / "index.html").write_text("<body>root</body>", encoding="utf-8")
        assert _docs_serve._pick_index(site, None) is None

    def test_explicit_index_wins(self, site):
        assert _docs_serve._pick_index(site, "/docs/x.html") == "docs/x.html"

    def test_no_candidate_means_no_redirect(self, temp_dir):
        assert _docs_serve._pick_index(temp_dir, None) is None


class TestComments:
    def test_comment_round_trip(self, server, site):
        status, body = _post(server + "/comment", {"page": "/page.html", "text": "fix this"})
        assert status == 200
        stored = json.loads(body)
        assert stored["text"] == "fix this" and stored["status"] == "open" and stored["id"]

        lines = (site / "__notes__" / "site-comments.jsonl").read_text(encoding="utf-8").splitlines()
        assert len(lines) == 1 and json.loads(lines[0])["id"] == stored["id"]

        _, _, listing = _get(server + "/comments?page=%2Fpage.html")
        assert [c["id"] for c in json.loads(listing)] == [stored["id"]]
        _, _, other = _get(server + "/comments?page=%2Fother.html")
        assert json.loads(other) == []
        _, _, everything = _get(server + "/comments")
        assert len(json.loads(everything)) == 1

    def test_page_query_is_compared_as_a_path(self, server):
        _, body = _post(server + "/comment", {"page": "/nested/./index.html", "text": "here"})
        cid = json.loads(body)["id"]
        _, _, listing = _get(server + "/comments?page=%2Fnested%2Findex.html")
        assert [c["id"] for c in json.loads(listing)] == [cid]        # spellings collapse to one page
        _, _, other = _get(server + "/comments?page=%2Fnested%2F")
        assert json.loads(other) == []

    def test_edit_then_delete(self, server, site):
        _, body = _post(server + "/comment", {"page": "/p", "text": "v1"})
        cid = json.loads(body)["id"]

        status, _ = _post(server + "/comment-edit", {"id": cid, "text": "v2"})
        assert status == 200
        [c] = json.loads(_get(server + "/comments")[2])
        assert c["text"] == "v2" and c["edited"]

        status, _ = _post(server + "/comment-delete", {"id": cid})
        assert status == 200
        assert json.loads(_get(server + "/comments")[2]) == []
        assert (site / "__notes__" / "site-comments.jsonl").read_text(encoding="utf-8") == ""

    def test_done_comments_are_hidden(self, server, site):
        notes = site / "__notes__"
        notes.mkdir()
        (notes / "site-comments.jsonl").write_text(
            json.dumps({"id": "a", "page": "/p", "text": "open"}) + "\n"
            + "\n"  # blank lines are fine
            + json.dumps({"id": "b", "page": "/p", "text": "done", "status": "done"}) + "\n",
            encoding="utf-8")
        assert [c["id"] for c in json.loads(_get(server + "/comments")[2])] == ["a"]

    def test_malformed_comments_file_is_reported_not_rewritten(self, server, site):
        notes = site / "__notes__"
        notes.mkdir()
        broken = json.dumps({"id": "a", "page": "/p", "text": "open"}) + "\nnot json\n"
        (notes / "site-comments.jsonl").write_text(broken, encoding="utf-8")
        status, _, body = _get(server + "/comments")
        assert status == 500 and b"line 2" in body
        assert _post(server + "/comment", {"page": "/p", "text": "new"})[0] == 500
        assert _post(server + "/comment-delete", {"id": "a"})[0] == 500
        assert (notes / "site-comments.jsonl").read_text(encoding="utf-8") == broken

    @pytest.mark.parametrize("payload", [{"page": "/p"}, {"text": "   "}, [1, 2]])
    def test_bad_comment_rejected(self, server, site, payload):
        status, _ = _post(server + "/comment", payload)
        assert status == 400
        assert not (site / "__notes__").exists()

    def test_oversized_comment_rejected(self, server):
        status, _ = _post(server + "/comment", None, raw=b"x" * (_docs_serve.MAX_COMMENT + 1))
        assert status == 413

    def test_edit_needs_id_and_text(self, server):
        assert _post(server + "/comment-edit", {"text": "x"})[0] == 400
        assert _post(server + "/comment-edit", {"id": "a", "text": " "})[0] == 400
        assert _post(server + "/comment-delete", {})[0] == 400

    def test_unknown_post_route(self, server):
        assert _post(server + "/nope", {})[0] == 404


class TestWhatChangedSinceRead:
    """A reader coming back to a page sees what changed since they marked it read, and when the page
    last changed (pygim memory #96: Debith's comment on ENACT 03, 2026-09-23)."""

    DOC = ("# Title\n\nFirst paragraph.\n\n- one\n\n- two\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
           "```text\nfenced\n\nstill fenced\n```\n\nLast paragraph.\n")

    def test_the_markdown_splits_into_the_blocks_a_reader_sees(self):
        """A loose list is one block, and so is a fence with a blank line inside it."""
        assert _docs_serve.blocks_of(self.DOC) == [
            "# Title", "First paragraph.", "- one\n\n- two", "| a | b |\n|---|---|\n| 1 | 2 |",
            "```text\nfenced\n\nstill fenced\n```", "Last paragraph."]

    def test_each_block_of_a_generated_page_carries_its_place_and_digest(self):
        page = _docs_serve.render_markdown(self.DOC, "t")
        tags = re.findall(r'<(\w+) data-block="(\d+)-[0-9a-f]{12}"', page)
        assert [(tag, int(n)) for tag, n in tags] == [("h1", 0), ("p", 1), ("ul", 2), ("table", 3), ("pre", 4), ("p", 5)]
        assert "<!--b:" not in page and page.count("<li>") == 2               # the list stayed one list

    def test_a_page_an_older_renderer_made_is_made_again(self, site):
        """A fix to the renderer reached no page until its Markdown changed: after the Mermaid fix
        of 2026-09-23 thirteen pages had to be rebuilt by hand."""
        (site / "note.md").write_text("# one\n", encoding="utf-8")
        (site / "note.generated.html").write_text(
            "<!doctype html>\n<!-- generated from note.md by oo docs serve; edit the Markdown, not this file -->old",
            encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        _docs_serve.materialize_markdown(root / "note.md")
        assert "data-block" in (site / "note.generated.html").read_text(encoding="utf-8")

    def test_a_generated_page_is_brought_up_to_date_when_it_is_opened(self, server, site):
        md = site / "note.md"
        md.write_text("# one\n", encoding="utf-8")
        _get(server + "/note.md")
        time.sleep(0.05)
        md.write_text("# two\n", encoding="utf-8")
        os.utime(md, None)
        _, _, body = _get(server + "/note.generated.html")
        assert b"two" in body

    def test_a_page_never_marked_read_says_so_and_when_it_changed(self, server, site):
        (site / "doc.md").write_text(self.DOC, encoding="utf-8")
        _get(server + "/doc.md")
        status, _, body = _get(server + "/read-state?page=%2Fdoc.generated.html")
        state = json.loads(body)
        assert status == 200 and state["read"] is None and state["source"] == "/doc.md"
        assert re.match(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d[+-]\d\d:\d\d$", state["modified"]["changed"])
        assert state["modified"]["commit"] is None and state["modified"]["uncommitted"] is None   # not a git checkout

    def test_changes_are_reported_against_the_page_as_it_was_when_marked_read(self, server, site):
        (site / "doc.md").write_text(self.DOC, encoding="utf-8")
        _get(server + "/doc.md")
        status, body = _post(server + "/read", {"page": "/doc.generated.html"})
        read = json.loads(body)["read"]
        assert status == 200 and read["marked"] and (read["added"], read["changed"], read["removed"]) == ([], [], [])
        kept = json.loads((site / "__notes__" / "read" / "doc.md.json").read_text(encoding="utf-8"))
        assert kept["text"] == self.DOC

        edited = (self.DOC.replace("First paragraph.", "First paragraph, reworded.")
                  .replace("| a | b |\n|---|---|\n| 1 | 2 |\n\n", "") + "\nAdded paragraph.\n")
        time.sleep(0.05)
        (site / "doc.md").write_text(edited, encoding="utf-8")
        os.utime(site / "doc.md", None)
        _, _, page = _get(server + "/doc.generated.html")
        _, _, body = _get(server + "/read-state?page=%2Fdoc.generated.html")
        read = json.loads(body)["read"]
        assert [token.split("-")[0] for token in read["added"]] == ["5"]
        assert [c["block"].split("-")[0] for c in read["changed"]] == ["1"]
        assert "First paragraph." in read["changed"][0]["before"]
        assert len(read["removed"]) == 1 and "<table>" in read["removed"][0]["before"]
        for token in read["added"] + [c["block"] for c in read["changed"]]:
            assert f'data-block="{token}"'.encode() in page                   # the page names the same blocks

    def test_a_line_ending_alone_is_not_a_change(self, server, site):
        """A checkout made with git's autocrlf holds \\r\\n on disk and \\n in the commit, so a page
        read as bytes differed from its last commit, and from its read mark, on every line (first CI
        run on Windows, 2026-09-28)."""
        (site / "doc.md").write_bytes(self.DOC.replace("\n", "\r\n").encode("utf-8"))
        _get(server + "/doc.md")
        _post(server + "/read", {"page": "/doc.generated.html"})
        kept = json.loads((site / "__notes__" / "read" / "doc.md.json").read_text(encoding="utf-8"))
        assert kept["text"] == self.DOC
        time.sleep(0.05)
        (site / "doc.md").write_bytes(self.DOC.encode("utf-8"))
        os.utime(site / "doc.md", None)
        _get(server + "/doc.generated.html")
        _, _, body = _get(server + "/read-state?page=%2Fdoc.generated.html")
        read = json.loads(body)["read"]
        assert (read["added"], read["changed"], read["removed"]) == ([], [], [])

    def test_a_reworded_block_is_paired_with_what_it_was_not_with_its_neighbour(self):
        """A paragraph reworded next to a table deleted is one replaced range to a diff; the table
        was reported as part of the paragraph's "before", and nothing as removed (found running the
        page in a browser, 2026-09-23)."""
        before = "# T\n\nFirst paragraph.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\nLast.\n"
        now = "# T\n\nFirst paragraph, reworded.\n\nLast.\n"
        changes = _docs_serve._changes(before, now)
        assert [c["block"].split("-")[0] for c in changes["changed"]] == ["1"] and changes["added"] == []
        assert "First paragraph." in changes["changed"][0]["before"] and "<table>" not in changes["changed"][0]["before"]
        assert len(changes["removed"]) == 1 and "<table>" in changes["removed"][0]["before"]

    def test_a_block_in_a_replaced_range_that_resembles_nothing_is_added(self):
        before = "# T\n\nOld words here.\n\nLast.\n"
        now = "# T\n\nCompletely different sentence about tables.\n\nLast.\n"
        changes = _docs_serve._changes(before, now)
        assert [a.split("-")[0] for a in changes["added"]] == ["1"] and changes["changed"] == []
        assert len(changes["removed"]) == 1 and "Old words here." in changes["removed"][0]["before"]

    def test_one_change_can_be_marked_read_and_the_others_stay_new(self, server, site):
        """Debith, 2026-09-25: "now marking it read marked all read, not the single entry"."""
        (site / "doc.md").write_text(self.DOC, encoding="utf-8")
        _get(server + "/doc.md")
        _post(server + "/read", {"page": "/doc.generated.html"})
        time.sleep(0.05)
        edited = (self.DOC.replace("First paragraph.", "First paragraph, reworded.")
                  .replace("Last paragraph.", "Last paragraph, reworded.") + "\nAdded paragraph.\n")
        (site / "doc.md").write_text(edited, encoding="utf-8")
        os.utime(site / "doc.md", None)
        _, _, body = _get(server + "/read-state?page=%2Fdoc.generated.html")
        read = json.loads(body)["read"]
        first = read["changed"][0]["block"]
        status, body = _post(server + "/read", {"page": "/doc.generated.html", "block": first})
        read = json.loads(body)["read"]
        assert status == 200 and first not in [c["block"] for c in read["changed"]]
        assert len(read["changed"]) == 1 and len(read["added"]) == 1          # the other two are still new
        added = read["added"][0]
        _, body = _post(server + "/read", {"page": "/doc.generated.html", "block": added})
        read = json.loads(body)["read"]
        assert read["added"] == [] and len(read["changed"]) == 1
        status, _ = _post(server + "/read", {"page": "/doc.generated.html", "block": "9-000000000000"})
        assert status == 400                                                    # not a change on this page

    @staticmethod
    def _long_page(reworded: bool, tables: int = 8, rows: int = 12) -> str:
        """A page of long tables; reworded, one value in every row changes, as owner-review's did."""
        parts = ["# A long review\n"]
        for t in range(tables):
            lines = [f"## Task {t}\n", "| Dimension | observed | proposed | run 1 | run 2 |", "|---|---|---|---|---|"]
            for r in range(rows):
                values = [f"value_{(t * 7 + r * 3 + c + (5 if reworded and c == r % 4 else 0)) % 41} — "
                          "what it means, in a sentence of its own" for c in range(4)]
                lines.append(f"| dimension {r} | " + " | ".join(values) + " |")
            parts.append("\n".join(lines) + "\n")
        return "\n".join(parts)

    def test_marking_one_change_read_on_a_long_rewritten_page_answers_at_once(self, server, site):
        """Debith, 2026-09-27: "Marking something read takes several seconds" — on owner-review, whose
        every table had been rewritten, each click compared every long block with every other twice
        (3.6 s a time), though the page's load had just compared the same pairs."""
        (site / "doc.md").write_text(self._long_page(reworded=False), encoding="utf-8")
        _get(server + "/doc.md")
        _post(server + "/read", {"page": "/doc.generated.html"})
        time.sleep(0.05)
        (site / "doc.md").write_text(self._long_page(reworded=True), encoding="utf-8")
        os.utime(site / "doc.md", None)
        _, _, body = _get(server + "/read-state?page=%2Fdoc.generated.html")       # the page's load
        changed = [c["block"] for c in json.loads(body)["read"]["changed"]]
        assert len(changed) == 8
        for block in changed[:2]:
            started = time.perf_counter()
            status, body = _post(server + "/read", {"page": "/doc.generated.html", "block": block})
            took = time.perf_counter() - started
            assert status == 200 and block not in [c["block"] for c in json.loads(body)["read"]["changed"]]
            assert took < 1.0, f"marking one change read took {took:.2f}s"

    def test_a_removed_block_says_where_it_stood_and_can_be_marked_read_alone(self):
        """Debith, 2026-09-25: "Removed can be red tint, or maybe grayed out, or strike-through. The
        button is not needed." A removed block is shown where it stood, after the block it followed."""
        before = "# T\n\nKeep one.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\nKeep two.\n\nAnother paragraph going.\n"
        now = "# T\n\nKeep one.\n\nKeep two.\n\nA brand new paragraph.\n"
        changes = _docs_serve._changes(before, now)
        tokens = _docs_serve._tokens(_docs_serve.blocks_of(now))
        table = next(r for r in changes["removed"] if "<table>" in r["before"])
        assert table["after"] == tokens[1] and table["block"].startswith("r")        # it stood after "Keep one."
        kept = _docs_serve._read_one(before, now, table["block"])
        again = _docs_serve._changes(kept, now)
        assert not any("<table>" in r["before"] for r in again["removed"])
        assert len(again["removed"]) + len(again["added"]) + len(again["changed"]) == \
            len(changes["removed"]) + len(changes["added"]) + len(changes["changed"]) - 1

    def test_a_hand_written_page_has_a_date_but_no_read_marks(self, server):
        status, _, body = _get(server + "/read-state?page=%2Fpage.html")
        assert status == 200 and json.loads(body)["read"] is False
        status, _ = _post(server + "/read", {"page": "/page.html"})
        assert status == 400

    @pytest.mark.parametrize("page", ["/../outside.md", "/missing.generated.html", "", "/style.css"])
    def test_a_page_that_is_not_a_page_here_is_refused(self, server, page):
        status, _, _ = _get(server + "/read-state?page=" + urllib.parse.quote(page))
        assert status == 404
        status, _ = _post(server + "/read", {"page": page})
        assert status == 404

    @pytest.mark.skipif(shutil.which("git") is None, reason="needs git")
    def test_git_says_whether_the_page_is_committed(self, temp_dir):
        def git(*args):
            subprocess.run(["git", "-C", str(temp_dir), "-c", "user.name=t", "-c", "user.email=t@example.com", *args],
                           check=True, capture_output=True)
        git("init", "-q")
        (temp_dir / "doc.md").write_text("# one\n", encoding="utf-8")
        git("add", "doc.md")
        git("commit", "-q", "-m", "one")
        root = _docs_serve.pygim.path(temp_dir, store=PathStore())
        committed = _docs_serve.history_of(root / "doc.md")
        assert committed["commit"] and committed["committed"] and committed["uncommitted"] is False
        (temp_dir / "doc.md").write_text("# two\n", encoding="utf-8")
        assert _docs_serve.history_of(root / "doc.md")["uncommitted"] is True

    def test_a_generated_page_keeps_a_line_under_its_title_for_its_date(self):
        """Debith, 2026-09-24, on the floating box: "I don't see much use for this box right now" and
        "I prefer that the document itself has some sort of color coding". When the page changed, and
        the way to mark it read, now sit in the document under its title; the line is there from the
        start, so filling it in moves nothing on the page."""
        page = _docs_serve.render_markdown("# Title\n\nText.\n", "t")
        assert re.search(r'<h1 [^>]*>Title</h1>\s*<p id="rd-meta" class="rd-meta"></p>', page)
        untitled = _docs_serve.render_markdown("Text only.\n", "t")
        assert re.search(r'<body>\s*<p id="rd-meta" class="rd-meta"></p>', untitled)

    def test_a_highlight_tints_a_block_over_its_own_background(self):
        """The first tint was a `background-color` at 7%: too faint to notice (Debith, 2026-09-24:
        "I hope that in the document, there are some changes to background color for things that
        are new"), and it replaced a code block's own grey, so a new code block looked paler than an
        unchanged one. A tint is now a layer over whatever background the block has."""
        for kind in ("rd-added", "rd-changed"):
            rule = re.search(r"\[data-block\]\." + kind + r"\{([^}]*)\}", _commenter.READER).group(1)
            assert "background-color" not in rule and "background-image:linear-gradient" in rule
            alpha = float(re.search(r"rgba\(\d+,\d+,\d+,(\.\d+)\)", rule).group(1))
            assert alpha >= 0.15, f"{kind}: a tint of {alpha} is too faint to see on the page"

    def test_dragging_a_zoomed_diagram_never_starts_a_text_selection(self):
        """Debith, 2026-09-26: "after zooming in the dragging stops working as it seems to be
        autoselecting content at the same time". Zoomed in, the drawing fills the screen, so every
        drag begins on its text, and the browser began a selection that took the pointer. Three
        guards, each enough alone in most browsers: nothing in the view can be selected, the drawing
        does not take pointer events (the view does), and a press cancels the browser's default."""
        css = re.search(r"#dg-view\{([^}]*)\}", _commenter.DIAGRAMS).group(1)
        assert "user-select:none" in css
        assert "pointer-events:none" in re.search(r"#dg-view \.stage\{([^}]*)\}", _commenter.DIAGRAMS).group(1)
        press = re.search(r'view\.addEventListener\("pointerdown",function\(e\)\{(.*?)\}\);', _commenter.DIAGRAMS, re.S).group(1)
        assert "e.preventDefault()" in press

    def test_every_served_page_asks_what_changed(self, server):
        _, _, body = _get(server + "/page.html")
        assert b"/read-state?page=" in body


class _Served:
    """A server over *root* for the length of a ``with``; yields its base URL."""

    def __init__(self, root):
        self.httpd = _docs_serve.make_server(root, port=0, host="127.0.0.1")
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return f"http://127.0.0.1:{self.httpd.server_address[1]}"

    def __exit__(self, *exc):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=5)


def _git_repo(root):
    """*root* as a git checkout; returns a function that runs git in it."""
    def git(*args):
        subprocess.run(["git", "-C", str(root), "-c", "user.name=t", "-c", "user.email=t@example.com", *args],
                       check=True, capture_output=True)
    git("init", "-q")
    return git


class TestAPageNeverMarkedReadShowsWhatDiffersFromTheLastCommit:
    """Until a page is first marked read, what is new is what differs from its last commit. Before,
    a page never marked showed nothing, and the first mark took in changes the reader never saw
    (Debith's "Is this now new?", 2026-09-24; chose this over leaving it)."""

    DOC = TestWhatChangedSinceRead.DOC

    @pytest.mark.skipif(shutil.which("git") is None, reason="needs git")
    def test_the_last_commit_is_the_baseline_until_the_first_mark(self, temp_dir):
        git = _git_repo(temp_dir)
        (temp_dir / "doc.md").write_text(self.DOC, encoding="utf-8")
        git("add", "doc.md")
        git("commit", "-q", "-m", "one")
        (temp_dir / "doc.md").write_text(self.DOC.replace("First paragraph.", "First paragraph, reworded.")
                                         + "\nAdded paragraph.\n", encoding="utf-8")
        with _Served(temp_dir) as base:
            _get(base + "/doc.md")
            _, _, body = _get(base + "/read-state?page=%2Fdoc.generated.html")
            read = json.loads(body)["read"]
            assert read["marked"] is None and read["baseline"] == "commit"
            assert [c["block"].split("-")[0] for c in read["changed"]] == ["1"]
            assert [a.split("-")[0] for a in read["added"]] == ["6"]
            _, body = _post(base + "/read", {"page": "/doc.generated.html"})
            read = json.loads(body)["read"]
            assert read["baseline"] == "read" and (read["added"], read["changed"]) == ([], [])

    @pytest.mark.skipif(shutil.which("git") is None, reason="needs git")
    def test_a_page_never_committed_is_new_all_through(self, temp_dir):
        git = _git_repo(temp_dir)
        (temp_dir / "other.md").write_text("# other\n", encoding="utf-8")
        git("add", "other.md")
        git("commit", "-q", "-m", "one")
        (temp_dir / "doc.md").write_text(self.DOC, encoding="utf-8")
        with _Served(temp_dir) as base:
            _get(base + "/doc.md")
            _, _, body = _get(base + "/read-state?page=%2Fdoc.generated.html")
            read = json.loads(body)["read"]
        assert read["baseline"] == "commit" and len(read["added"]) == len(_docs_serve.blocks_of(self.DOC))


def _row(listing: bytes, name: str) -> str:
    """The listing's row for the entry called *name*."""
    rows = re.findall(r"<li\b.*?</li>", listing.decode("utf-8"), re.S)
    return next(row for row in rows if f">{name}</a>" in row)


class TestTheServerRestartsItselfWhenItsCodeChanges:
    """Debith, 2026-09-25: "I want some sort of restart mechanism to server. Restarting manually is
    boring." Every change to the review layer needed a manual restart, and then a page reload."""

    def test_a_change_to_a_watched_file_is_noticed_once(self, temp_dir):
        code = temp_dir / "code.py"
        code.write_text("x = 1\n", encoding="utf-8")
        watch = _docs_serve.Watch([_docs_serve.pygim.path(code, store=PathStore())])
        assert watch.changed() is False
        time.sleep(0.05)
        code.write_text("x = 2\n", encoding="utf-8")
        os.utime(code, None)
        assert watch.changed() is True and watch.changed() is False

    def test_serve_stops_and_restarts_when_its_code_changes(self, site, temp_dir):
        code = temp_dir / "code.py"
        code.write_text("x = 1\n", encoding="utf-8")
        restarted = threading.Event()
        thread = threading.Thread(target=_docs_serve.serve, args=(site,), daemon=True, kwargs=dict(
            port=0, host="127.0.0.1", watch=[code], restart=restarted.set, interval=0.05))
        thread.start()
        time.sleep(0.4)
        assert not restarted.is_set()
        code.write_text("x = 2\n", encoding="utf-8")
        os.utime(code, None)
        assert restarted.wait(5), "a change to the server's code did not restart it"
        thread.join(5)
        assert not thread.is_alive()

    def test_an_open_page_can_tell_that_the_server_restarted(self, server):
        _, _, first = _get(server + "/alive")
        _, _, again = _get(server + "/alive")
        assert json.loads(first)["boot"] == json.loads(again)["boot"]           # the same run
        _, _, page = _get(server + "/page.html")
        assert b'fetch("/alive")' in page                                       # the page watches for a new one


class TestFolderListingMarksWhatChanged:
    """Debith, 2026-09-24: "when the server shows the file structure, is it possible to mark there on
    files what files have changed and what folders contains files that have changed?" """

    def _read_then_edit(self, server, site, rel):
        _get(server + "/" + rel)
        _post(server + "/read", {"page": "/" + rel.replace(".md", ".generated.html")})
        time.sleep(0.05)
        (site / rel).write_text((site / rel).read_text(encoding="utf-8") + "\nMore.\n", encoding="utf-8")
        os.utime(site / rel, None)

    def test_a_file_changed_since_it_was_marked_read_says_so(self, server, site):
        (site / "notes").mkdir()
        for name in ("a.md", "b.md", "x&y.md"):
            (site / "notes" / name).write_text("# one\n", encoding="utf-8")
        self._read_then_edit(server, site, "notes/a.md")
        _get(server + "/notes/b.md")
        _post(server + "/read", {"page": "/notes/b.generated.html"})              # read, and unchanged since
        status, headers, listing = _get(server + "/notes/")
        assert status == 200 and headers["Content-Type"].startswith("text/html")
        assert "changed since read" in _row(listing, "a.md")
        assert "changed since read" not in _row(listing, "b.md")
        assert "x&amp;y.md" in listing.decode("utf-8")                           # names are escaped
        assert "changed since read" not in _row(listing, "a.generated.html")     # the Markdown carries the mark
        assert b'id="cmt-tab"' in listing                                        # the review layer, as on any page

    def test_a_folder_counts_the_changed_files_anywhere_below_it(self, server, site):
        (site / "notes" / "deep" / "deeper").mkdir(parents=True)
        (site / "notes" / "deep" / "deeper" / "a.md").write_text("# one\n", encoding="utf-8")
        self._read_then_edit(server, site, "notes/deep/deeper/a.md")
        _, _, listing = _get(server + "/notes/")
        assert "1 changed since read" in _row(listing, "deep/")

    @pytest.mark.skipif(shutil.which("git") is None, reason="needs git")
    def test_files_git_has_not_committed_are_marked_and_counted(self, temp_dir):
        def git(*args):
            subprocess.run(["git", "-C", str(temp_dir), "-c", "user.name=t", "-c", "user.email=t@example.com", *args],
                           check=True, capture_output=True)
        git("init", "-q")
        (temp_dir / "kept.txt").write_text("same\n", encoding="utf-8")
        (temp_dir / "edited.txt").write_text("one\n", encoding="utf-8")
        git("add", ".")
        git("commit", "-q", "-m", "one")
        (temp_dir / "edited.txt").write_text("two\n", encoding="utf-8")
        (temp_dir / "sub").mkdir()
        (temp_dir / "sub" / "new.txt").write_text("new\n", encoding="utf-8")
        httpd = _docs_serve.make_server(temp_dir, port=0, host="127.0.0.1")
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            _, _, listing = _get(f"http://127.0.0.1:{httpd.server_address[1]}/")
        finally:
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)
        assert "not committed" in _row(listing, "edited.txt")
        assert "not committed" not in _row(listing, "kept.txt")
        assert "1 not committed" in _row(listing, "sub/")

    def test_outside_a_checkout_nothing_is_marked_not_committed(self, server, site):
        (site / "notes").mkdir()
        (site / "notes" / "a.txt").write_text("x\n", encoding="utf-8")
        _, _, listing = _get(server + "/notes/")
        assert "not committed" not in _row(listing, "a.txt")


class TestCrossReferencesAndTerms:
    """A site's Markdown pages define numbered sections and Term/Type tables; the
    generator turns a reference to either into hover text."""

    @staticmethod
    def _site(temp_dir):
        (temp_dir / "00_guide.md").write_text(
            "# Guide\n\n"
            "## 4. Knowledge\n\n"
            "| Term | Meaning |\n|---|---|\n"
            "| **memory** | a unit of knowledge |\n"
            "| `tag_id` | a dense id for one tag |\n\n"
            "### 4.8 Foundations\n\nWhat it rests on.\n",
            encoding="utf-8")
        (temp_dir / "01_model.md").write_text(
            "# Model\n\n"
            "## 2. Identity\n\n"
            "A `tag_id` is small, see overview \u00a74.8 and \u00a72 here.\n\n"
            "| memory | width |\n|---|---|\n| one | small |\n\n"
            "```text\nnot a link: \u00a74.8\n```\n",
            encoding="utf-8")
        root = pygim.path(temp_dir, store=PathStore()).resolve()
        return root, _docs_serve._SiteIndex.build(root)

    def _render(self, temp_dir, name):
        root, site = self._site(temp_dir)
        out = _docs_serve.materialize_markdown(root / name, site=site)
        return (temp_dir / out.name).read_text(encoding="utf-8")

    def test_index_collects_sections_and_terms(self, temp_dir):
        _, site = self._site(temp_dir)
        assert set(site.sections) == {"2", "4", "4.8"}
        assert site.terms["memory"] == "a unit of knowledge"
        assert site.terms["tag_id"] == "a dense id for one tag"

    def test_reference_to_another_page_links_there(self, temp_dir):
        html = self._render(temp_dir, "01_model.md")
        assert '<a class="xref" href="00_guide.generated.html#48-foundations"' in html
        assert 'title="4.8 Foundations"' in html

    def test_reference_to_this_page_stays_local(self, temp_dir):
        html = self._render(temp_dir, "01_model.md")
        assert '<a class="xref" href="#2-identity"' in html

    def test_reference_inside_a_code_block_is_left_alone(self, temp_dir):
        html = self._render(temp_dir, "01_model.md")
        fenced = html.split("<pre")[1]
        assert "xref" not in fenced

    def test_code_span_naming_a_term_gets_hover_text(self, temp_dir):
        html = self._render(temp_dir, "01_model.md")
        assert '<code class="term" title="a dense id for one tag">tag_id</code>' in html

    def test_bold_term_gets_hover_text(self, temp_dir):
        html = self._render(temp_dir, "00_guide.md")
        assert '<strong class="term" title="a unit of knowledge">memory</strong>' in html

    def test_column_header_naming_a_term_gets_hover_text(self, temp_dir):
        html = self._render(temp_dir, "01_model.md")
        assert '<th class="term" title="a unit of knowledge">memory</th>' in html
        assert "<th>width</th>" in html      # a header the site does not define is left alone

    def test_no_index_means_no_annotation(self, temp_dir):
        self._site(temp_dir)
        text = (temp_dir / "01_model.md").read_text(encoding="utf-8")
        body = _docs_serve.render_markdown(text, "model").split("<body>")[1]
        assert "xref" not in body and 'class="term"' not in body

    def test_reference_naming_its_page_goes_there(self, temp_dir):
        (temp_dir / "00_overview.md").write_text(
            "# Overview\n\n## 3. Top\n\n### 3.1 Overview bit\n\ntext\n", encoding="utf-8")
        (temp_dir / "01_model.md").write_text(
            "# Model\n\n## 3. Here\n\n### 3.1 Local\n\n"
            "Bare \u00a73.1, section 02 \u00a73.1, (02, \u00a73.1) and overview \u00a73.1.\n",
            encoding="utf-8")
        (temp_dir / "02_store.md").write_text(
            "# Store\n\n## 3. Store\n\n### 3.1 Commit\n\ntext\n", encoding="utf-8")
        root = pygim.path(temp_dir, store=PathStore()).resolve()
        site = _docs_serve._SiteIndex.build(root)
        out = _docs_serve.materialize_markdown(root / "01_model.md", site=site)
        body = (temp_dir / out.name).read_text(encoding="utf-8").split("<body>")[1]
        assert body.count('href="#31-local"') == 1
        assert body.count('href="02_store.generated.html#31-commit"') == 2
        assert body.count('href="00_overview.generated.html#31-overview-bit"') == 1
        assert "section 02 <a" in body     # the qualifier stays as text

    def test_reference_naming_a_page_without_that_section_stays_text(self, temp_dir):
        _, site = self._site(temp_dir)
        html = _docs_serve.render_markdown("See section 07 \u00a72.", "x", site=site, page="/01_model.generated.html")
        body = html.split("<body>")[1]
        assert "xref" not in body and "section 07 \u00a72" in body

    def test_unknown_reference_is_left_as_text(self, temp_dir):
        _, site = self._site(temp_dir)
        html = _docs_serve.render_markdown("See \u00a79.9 for that.", "x", site=site, page="/01_model.generated.html")
        body = html.split("<body>")[1]
        assert "xref" not in body and "\u00a79.9" in body


class TestErrorRepliesDrainTheBody:
    """An error reply reads the request body first; Windows resets a connection closed
    with unread bytes and the client then sees WinError 10053 instead of our status."""

    @staticmethod
    def _handler(site, body: bytes, *, length=None):
        httpd = _docs_serve.make_server(site, port=0, host="127.0.0.1")
        try:
            cls = httpd.RequestHandlerClass
        finally:
            httpd.server_close()
        h = cls.__new__(cls)
        h.headers = {"Content-Length": str(len(body) if length is None else length)}
        h.rfile = io.BytesIO(body)
        h._body_read = False
        return h

    def test_unread_body_is_consumed(self, site):
        h = self._handler(site, b"x" * 70_000)
        h._discard_body()
        assert h.rfile.tell() == 70_000

    def test_consumed_body_is_not_read_twice(self, site):
        h = self._handler(site, b"abcde")
        assert h._read_body(5) == b"abcde"
        h.rfile = io.BytesIO(b"next request")
        h._discard_body()
        assert h.rfile.tell() == 0

    def test_body_over_upload_limit_is_left_alone(self, site):
        h = self._handler(site, b"y", length=_docs_serve.MAX_BYTES + 1)
        h._discard_body()
        assert h.rfile.tell() == 0

    def test_unknown_route_with_large_body(self, server):
        assert _post(server + "/nope", None, raw=b"z" * 100_000)[0] == 404


class TestUpload:
    def test_image_lands_under_images(self, server, site):
        status, body = _post(server + "/upload?path=images/cast/hero.png", None, raw=b"PNGDATA")
        assert status == 200
        assert json.loads(body) == {"ok": True, "path": "images/cast/hero.png"}
        assert (site / "images" / "cast" / "hero.png").read_bytes() == b"PNGDATA"

    def test_legacy_name_query(self, server, site):
        status, _ = _post(server + "/upload?name=x.jpg", None, raw=b"J")
        assert status == 200
        assert (site / "images" / "x.jpg").read_bytes() == b"J"

    @pytest.mark.parametrize("path", [
        "images/../../etc/passwd.png",  # traversal
        "notes/hero.png",  # not under images/
        "images/hero.txt",  # not an image
        "hero.png",  # no directory
        "images/we%00ird/hero.png",  # bad segment
    ])
    def test_bad_paths_rejected(self, server, site, path):
        status, _ = _post(server + "/upload?path=" + path, None, raw=b"X")
        assert status == 400
        assert not (site / "images").exists()

    def test_empty_body_rejected(self, server, site):
        status, _ = _post(server + "/upload?path=images/a.png", None, raw=b"")
        assert status == 413
        assert not (site / "images").exists()


class TestMakeServer:
    def test_missing_root(self, temp_dir):
        with pytest.raises(FileNotFoundError):
            _docs_serve.make_server(temp_dir / "missing", port=0, host="127.0.0.1")

    def test_port_in_use_hint(self, site):
        first = _docs_serve.make_server(site, port=0, host="127.0.0.1")
        try:
            port = first.server_address[1]
            _docs_serve.socketserver.TCPServer.allow_reuse_address = False
            with pytest.raises(_docs_serve.ServeError) as info:
                _docs_serve.make_server(site, port=port, host="127.0.0.1")
            assert f"--port {port + 1}" in str(info.value)
        finally:
            first.server_close()

    def test_unresolvable_host_hint(self, site):
        with pytest.raises(_docs_serve.ServeError) as info:
            _docs_serve.make_server(site, port=0, host="no.such.host.invalid")
        assert "--host 127.0.0.1" in str(info.value)

    def test_host_env_default(self, site, monkeypatch):
        monkeypatch.setenv(_docs_serve.HOST_ENV, "127.0.0.1")
        httpd = _docs_serve.make_server(site, port=0)
        try:
            assert httpd.server_address[0] == "127.0.0.1"
        finally:
            httpd.server_close()


class TestRebuild:
    def test_rebuild_reports_the_pages_it_added_and_removed(self, site):
        added, removed = _docs_serve.rebuild(site, "touch new.html && rm page.html")
        assert added == ["/new.html"] and removed == ["/page.html"]

    def test_rebuild_failure_names_the_exit_code(self, site):
        with pytest.raises(_docs_serve.ServeError, match="exit 3"):
            _docs_serve.rebuild(site, "exit 3")


class TestCli:
    def test_docs_serve_is_registered(self):
        result = CliRunner().invoke(cli_oo, ["docs", "serve", "--help"])
        assert result.exit_code == 0
        assert "--rebuild" in result.output and "--index" in result.output

    def test_an_unknown_command_fails_and_says_so(self):
        """`oo memory reload` — renamed away, still named in stored knowledge — printed a sentence
        and exited 0. A command that does not exist must fail where a script can see it."""
        result = CliRunner().invoke(cli_oo, ["memory", "reload"])
        assert result.exit_code == 2
        assert "No such command 'memory'" in result.output

    def test_a_near_miss_is_offered_the_command_it_probably_meant(self):
        result = CliRunner().invoke(cli_oo, ["enakt", "status"])
        assert result.exit_code == 2 and "Did you mean 'enact'?" in result.output

    def test_no_args_shows_help(self):
        result = CliRunner().invoke(cli_oo, [])
        assert result.exit_code == 0
        assert "docs" in result.output

    def test_missing_dir_is_a_usage_error(self, temp_dir):
        result = CliRunner().invoke(cli_oo, ["docs", "serve", "--dir", str(temp_dir / "nope")])
        assert result.exit_code == 2

    def test_failing_rebuild_aborts_before_serving(self, site, monkeypatch):
        monkeypatch.setattr(_docs_serve, "serve", lambda *a, **k: pytest.fail("must not serve"))
        with pytest.raises(click.ClickException) as info:
            GimmicksCliApp().docs_serve(directory=str(site), rebuild="exit 3")
        assert "exit 3" in str(info.value)

    def test_rebuild_runs_in_served_dir_then_serves(self, site, monkeypatch):
        calls = []
        monkeypatch.setattr(_docs_serve, "serve", lambda root, **k: calls.append((root, k)))
        GimmicksCliApp().docs_serve(directory=str(site), port=1234, host="127.0.0.1",
                                    rebuild="touch built.marker")
        assert (site / "built.marker").exists()
        [(root, kw)] = calls
        assert root == site and kw["port"] == 1234 and kw["host"] == "127.0.0.1" and kw["index"] is None
        assert isinstance(kw["store"], PathStore)                       # one store for rebuild and serving
