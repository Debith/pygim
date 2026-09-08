# -*- coding: utf-8 -*-
"""Tests for ``oo docs serve`` — the static docs server with the ✎ review layer."""

import json
import threading
import urllib.error
import urllib.request

import click
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
        assert _commenter.inject("no body tag").endswith(_commenter.COMMENTER)


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
        assert status == 302 and headers["Location"] == "/design.html"
        generated = (site / "design.html").read_text(encoding="utf-8")
        assert "generated from design.md by oo docs serve" in generated
        assert "<h1" in generated and "<em>text</em>" in generated and "<table>" in generated
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
        assert "<h1" in (site / "note.html").read_text(encoding="utf-8") and "one" in (site / "note.html").read_text(encoding="utf-8")
        stamp = os.path.getmtime(first)
        _docs_serve.materialize_markdown(root / "note.md")
        assert os.path.getmtime(first) == stamp                        # fresh: untouched
        time.sleep(0.05)
        md.write_text("# two\n", encoding="utf-8")
        os.utime(md, None)
        _docs_serve.materialize_markdown(root / "note.md")
        assert "two" in (site / "note.html").read_text(encoding="utf-8")   # stale: regenerated

    def test_a_hand_written_html_is_never_overwritten(self, site):
        (site / "mine.md").write_text("# from markdown\n", encoding="utf-8")
        (site / "mine.html").write_text("<body>hand-written</body>", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        out = _docs_serve.materialize_markdown(root / "mine.md")
        assert out.name == "mine.html" and (site / "mine.html").read_text(encoding="utf-8") == "<body>hand-written</body>"

    def test_markdown_links_point_at_generated_pages(self, site):
        (site / "a.md").write_text("see [b](b.md#part) and [ext](https://x.y/z.md) and [raw](/abs.md)\n", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        html = (site / _docs_serve.materialize_markdown(root / "a.md").name).read_text(encoding="utf-8")
        assert 'href="b.html#part"' in html and 'href="https://x.y/z.md"' in html and 'href="/abs.md"' in html

    def test_mermaid_fence_becomes_a_live_diagram(self, site):
        (site / "diagram.md").write_text("```mermaid\nclassDiagram\n  A --> B\n```\n", encoding="utf-8")
        root = _docs_serve.pygim.path(site, store=PathStore())
        html = (site / _docs_serve.materialize_markdown(root / "diagram.md").name).read_text(encoding="utf-8")
        assert '<pre class="mermaid">' in html and "A --> B" in html and "mermaid.esm.min.mjs" in html

    def test_markdown_index_stands_in_for_a_missing_index_html(self, temp_dir):
        (temp_dir / "README.md").write_text("# Home\n", encoding="utf-8")
        httpd = _docs_serve.make_server(temp_dir, port=0, host="127.0.0.1")
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{httpd.server_address[1]}"
            status, headers, _ = _get(base + "/", follow=False)
            assert status == 302 and headers["Location"] == "/README.html"
            body = _get(base + "/")[2].decode("utf-8")
            assert "<h1" in body and 'id="cmt-tab"' in body and (temp_dir / "README.html").is_file()
        finally:
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_pages_list_html_and_unconverted_markdown_only(self, server, site):
        (site / "notes.md").write_text("x", encoding="utf-8")
        (site / "done.md").write_text("y", encoding="utf-8")
        assert _get(server + "/done.md", follow=False)[0] == 302          # generates done.html
        pages = json.loads(_get(server + "/pages")[2])
        assert "/notes.md" in pages and "/done.html" in pages and "/done.md" not in pages

    def test_startup_generates_every_markdown_page(self, temp_dir):
        (temp_dir / "a.md").write_text("# a\n", encoding="utf-8")
        (temp_dir / "sub").mkdir()
        (temp_dir / "sub" / "b.md").write_text("# b\n", encoding="utf-8")
        (temp_dir / "__notes__").mkdir()
        (temp_dir / "__notes__" / "n.md").write_text("# n\n", encoding="utf-8")
        httpd = _docs_serve.make_server(temp_dir, port=0, host="127.0.0.1")
        try:
            assert (temp_dir / "a.html").is_file() and (temp_dir / "sub" / "b.html").is_file()
            assert not (temp_dir / "__notes__" / "n.html").exists()          # notes are not pages
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

    def test_free_text_goes_to_ai(self):
        result = CliRunner().invoke(cli_oo, ["explain", "this"])
        assert result.exit_code == 0
        assert "AI is not implemented yet!" in result.output

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
