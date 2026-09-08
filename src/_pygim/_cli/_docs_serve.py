# -*- coding: utf-8 -*-
"""Static docs server with a review layer (``oo docs serve``).

Serves a directory of HTML/CSS/JS and adds two things a plain
``python -m http.server`` lacks:

* **Comments.** Every served ``.html`` page gets the ✎ commenter fragment
  (``_commenter.py``). Comments POST to ``/comment`` and live, one JSON
  document per line, in ``<root>/__notes__/site-comments.jsonl`` — read and
  written by pathlike's JSONL engine. ``GET /comments`` lists the open ones
  (``?page=<path>`` filters); ``/comment-edit`` and ``/comment-delete`` change
  them in place. A malformed comments file is reported (HTTP 500 with the
  file and line), never silently rewritten.
* **Markdown.** Opening ``x.md`` GENERATES ``x.html`` beside it (the ``markdown``
  package; Mermaid fences become live diagrams; ``.md`` links are rewritten to
  their ``.html``) and redirects there, so the commenter works on the HTML page
  and comments key on it — the same shape as a built site. The file is
  regenerated when the Markdown is newer and carries a marker; a hand-written
  ``x.html`` without the marker is never overwritten. ``README.md`` / ``index.md``
  stand in for a missing ``index.html``.
* **Image drops.** Dropping an image on a page POSTs it to
  ``/upload?path=images/<sub>/<file>`` and it is written straight into
  ``<root>/images/<sub>/<file>`` — no Downloads round-trip. Uploads land only
  under an ``images`` directory and only with an image extension.

File handling is pathlike (``pygim.path``); Python here is routing glue. The
server owns a ``PathStore``: every path it makes — the root, the comments file,
each request's translated path, the pages inventory — is a row of that one
table, so the module's default store never sees request traffic and the whole
table goes when the server is dropped. ``GET /pages`` lists the site's HTML
pages (a ``PathSet`` of ``root.pathset("**/*.html")``), ``?page=`` is compared
as a path (spellings collapse), and ``rebuild()`` reports what a rebuild added
and removed as a set difference.

LOCAL USE: binds all interfaces by default (so the page is reachable via the WSL
IP when Windows→WSL localhost forwarding hiccups) and only ever writes under the
served root. Don't expose it to an untrusted network.
"""

from __future__ import annotations

import datetime
import errno
import http.server
import json
import os
import re
import socket
import socketserver
import urllib.parse

import pygim
from pygim.pathlike import PathStore
from _pygim._cli import _commenter

__all__ = ["ServeError", "make_server", "materialize_markdown", "rebuild", "render_markdown", "serve", "site_pages"]

SEG_RE = re.compile(r"^[a-z0-9][a-z0-9 ._-]*$", re.IGNORECASE)  # one path segment, no traversal (spaces ok)
EXT_OK = (".jpg", ".jpeg", ".png", ".webp", ".gif")
MAX_BYTES = 25 * 1024 * 1024  # 25 MB per image
MAX_COMMENT = 64 * 1024  # 64 KB per comment payload

# Site annotations land here (relative to the served root): one JSON object per line.
COMMENTS_REL = "__notes__/site-comments.jsonl"

# Where ``/`` goes when the served root has no ``index.html`` of its own: the
# first of these that exists (common generated-docs layouts).
INDEX_CANDIDATES = (
    "site/index.html",
    "docs/index.html",
    "build/html/index.html",
    "docs/_build/html/index.html",
    "_build/html/index.html",
    "README.md",
    "index.md",
    "00_overview.md",
)
PAGE_SUFFIXES = (".html", ".md")

MARKDOWN_STYLE = """<style>
body{max-width:72ch;margin:2rem auto;padding:0 1rem;font:16px/1.55 system-ui,sans-serif;color:#1d242b;background:#fbfbfa}
pre,code{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.92em}
pre{background:#f0f1ee;padding:.8rem 1rem;overflow-x:auto;border-radius:4px}
code{background:#f0f1ee;padding:.1em .3em;border-radius:3px}
pre code{background:none;padding:0}
table{border-collapse:collapse;margin:1rem 0}
th,td{border-bottom:1px solid #d3d7d2;padding:.35rem .6rem;text-align:left;vertical-align:top}
th{color:#5f6a72;font-size:.85em;letter-spacing:.04em;text-transform:uppercase}
h1,h2,h3{line-height:1.2}
blockquote{border-left:3px solid #b06e14;margin:1rem 0;padding:.2rem 1rem;color:#5f6a72}
.mermaid{background:none}
</style>"""
GENERATED_MARK = "<!-- generated from {src} by oo docs serve; edit the Markdown, not this file -->"
MERMAID_SCRIPT = ("<script type=\"module\">import mermaid from "
                  "\"https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs\";"
                  "mermaid.initialize({startOnLoad:true});</script>")

HOST_ENV = "PYGIM_HOST"


class ServeError(RuntimeError):
    """The server could not start — the message carries the user-facing hint block."""


def _now() -> str:
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def _upload_target(root, qs):
    """The safe absolute path for an ``?path=<root-relative>`` (or legacy
    ``?name=<file>``) upload query, or None. The path must pass through an
    ``images`` directory and end in an image extension; every segment is
    validated (no ``..``), and the resolved target must stay under *root*."""
    rel = qs.get("path", [None])[0]
    if rel is None:
        rel = "images/" + pygim.path(qs.get("name", [""])[0]).name
    segs = [s for s in rel.replace("\\", "/").split("/") if s not in ("", ".")]
    if len(segs) < 2 or "images" not in segs[:-1] or not all(SEG_RE.match(s) for s in segs):
        return None
    if not segs[-1].lower().endswith(EXT_OK):
        return None
    dest = root.joinpath(*segs).resolve()
    return (dest, "/".join(segs)) if root in dest.parents else None


def site_pages(root):
    """The site's pages as a PathSet over *root*'s table: every ``*.html`` and
    ``*.md`` under the root except the notes directory. Two calls on the same
    root are two sets over one table, so they subtract and intersect at bit
    speed."""
    notes = COMMENTS_REL.split("/")[0]
    pages = root.pathset("**/*.html") | root.pathset("**/*.md")
    return pages - (root.pathset(notes + "/**/*.html") | root.pathset(notes + "/**/*.md"))


def listed_pages(root):
    """The pages a reader can open: every HTML page, plus Markdown pages that have
    no generated HTML yet (a bit test per page against the same set)."""
    pages = site_pages(root)
    return [p for p in pages if not (p.suffix == ".md" and p.with_suffix(".html") in pages)]


def render_markdown(text: str, title: str) -> str | None:
    """*text* (Markdown) as a complete HTML page, or None when the ``markdown``
    package is not installed (the file is then served as it is). Fenced
    ``mermaid`` blocks become ``<pre class="mermaid">`` with the Mermaid
    script, so diagrams render in the browser."""
    try:
        import markdown
    except ImportError:
        return None
    import html as _html

    body = markdown.markdown(text, extensions=["fenced_code", "tables", "toc"])
    # relative links to Markdown point at the pages generated for them
    body = re.sub(r'(href=")(?![a-z][a-z0-9+.-]*:|/|#)([^"#]+)\.md(#[^"]*)?"', lambda m: f'{m.group(1)}{m.group(2)}.html{m.group(3) or ""}"', body)
    mermaid = ""
    if 'class="language-mermaid"' in body:
        body = re.sub(
            r'<pre><code class="language-mermaid">(.*?)</code></pre>',
            lambda m: '<pre class="mermaid">' + _html.unescape(m.group(1)) + "</pre>",
            body, flags=re.S)
        mermaid = MERMAID_SCRIPT
    return (f"<!doctype html><html><head><meta charset=\"utf-8\"><title>{_html.escape(title)}</title>"
            f"{MARKDOWN_STYLE}</head><body>{body}{mermaid}</body></html>")


def pregenerate(root) -> int:
    """Generate the HTML of every Markdown page under *root* that is missing or
    stale, so no first open pays for a render (a fresh page costs two stats).
    Also imports the renderer once. Returns how many pages were (re)generated."""
    try:
        import markdown  # noqa: F401  — import once here, not inside the first request
    except ImportError:
        return 0
    count = 0
    for page in site_pages(root):
        if page.suffix != ".md":
            continue
        out = page.with_suffix(".html")
        stale = not out.is_file() or os.path.getmtime(os.fspath(out)) < os.path.getmtime(os.fspath(page))
        try:
            if materialize_markdown(page) is not None and stale:
                count += 1
        except (OSError, RuntimeError, UnicodeDecodeError):
            continue   # an unreadable page is reported when it is opened, not at startup
    return count


def materialize_markdown(md):
    """The HTML page for the Markdown file *md*, generated beside it as ``<stem>.html``
    when missing or older than the Markdown, and left alone when it exists without
    the generated marker (a hand-written page wins). Returns the HTML path, or None
    when the ``markdown`` package is not installed."""
    out = md.with_suffix(".html")
    src_name = md.name
    if out.is_file():
        try:
            head = out.read_bytes()[:400].decode("utf-8", "replace")
        except RuntimeError:
            head = ""
        if "generated from" not in head or "oo docs serve" not in head:
            return out                                   # not ours: never overwritten
        if os.path.getmtime(os.fspath(out)) >= os.path.getmtime(os.fspath(md)):
            return out                                   # fresh
    html = render_markdown(md.read_bytes().decode("utf-8"), md.stem)
    if html is None:
        return None
    mark = GENERATED_MARK.format(src=src_name)
    out.write_bytes(html.replace("<!doctype html>", "<!doctype html>\n" + mark, 1).encode("utf-8"))
    return out


def _relative(root, p) -> str:
    """*p* as the root-relative URL path (forward slashes, leading slash)."""
    rel = os.fspath(p)[len(os.fspath(root)):].replace("\\", "/")
    return "/" + rel.lstrip("/")


def _page_key(root, page: str | None):
    """A ``?page=`` value (a URL path) as a path over *root*'s table, so
    ``/a/./b.html`` and ``/a/b.html`` are the same page; None stays None."""
    if page is None:
        return None
    return root / page.lstrip("/")


def _pick_index(root, index: str | None) -> str | None:
    """The root-relative page ``/`` redirects to, or None to serve the root as-is."""
    if index:
        return index.replace("\\", "/").lstrip("/")
    if any((root / c).is_file() for c in ("index.html", "README.md", "index.md")):
        return None
    return next((c for c in INDEX_CANDIDATES if (root / c).is_file()), None)


def _make_handler(root, index: str | None):
    """A SimpleHTTPRequestHandler bound to *root* with the comment, pages and upload endpoints.
    *root* carries the server's store; every path made here derives from it."""
    store = root.store
    comments = root / COMMENTS_REL  # a pathlike jsonlpath: read() -> list, write(list)

    def load_comments() -> list:
        return comments.read() if comments.is_file() else []

    def save_comments(rows: list) -> None:
        comments.parent.mkdir(parents=True, exist_ok=True)
        comments.write(rows)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(root), **kw)

        # ---- GET -------------------------------------------------------
        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            route = parsed.path.rstrip("/")
            if route == "/comments":
                wanted = _page_key(root, urllib.parse.parse_qs(parsed.query).get("page", [None])[0])
                self._guarded(lambda: self._send_json([
                    c for c in load_comments()
                    if c.get("status", "open") == "open"
                    and (wanted is None or _page_key(root, c.get("page")) == wanted)]))
                return
            if route == "/pages":
                self._send_json(sorted(_relative(root, p) for p in listed_pages(root)))
                return
            if self.path in ("/", "/index.html") and index and not (root / "index.html").is_file() \
                    and not (root / "README.md").is_file() and not (root / "index.md").is_file():
                self.send_response(302)
                self.send_header("Location", "/" + index)
                self.end_headers()
                return
            # a Markdown page is generated as HTML beside its source and served from there
            page = pygim.path(self.translate_path(parsed.path), store=store)   # a row of the server's table
            if page.is_dir():
                page = next((page / c for c in ("index.html", "README.md", "index.md") if (page / c).is_file()),
                            page / "index.html")
            if page.suffix.lower() == ".md" and page.is_file():
                html = materialize_markdown(page)
                if html is not None:
                    self.send_response(302)
                    self.send_header("Location", urllib.parse.quote(_relative(root, html)))
                    self.end_headers()
                    return
            # every served HTML page gets the ✎ commenter
            if page.suffix.lower() == ".html" and page.is_file():
                try:
                    text = page.read_bytes().decode("utf-8")
                except (RuntimeError, UnicodeDecodeError):
                    super().do_GET()
                    return
                body = _commenter.inject(text).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            super().do_GET()

        # ---- helpers ---------------------------------------------------
        def _guarded(self, action):
            """Run *action*; a comments-file decode error becomes a 500 that names the file and line."""
            try:
                action()
            except RuntimeError as exc:
                self.send_error(500, str(exc))

        def _send_json(self, obj, status=200):
            body = json.dumps(obj).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _body_length(self, limit):
            try:
                length = int(self.headers.get("Content-Length", 0))
            except ValueError:
                length = 0
            return length if 0 < length <= limit else None

        def _read_json_body(self, limit=MAX_COMMENT):
            length = self._body_length(limit)
            if length is None:
                return None
            try:
                obj = json.loads(self.rfile.read(length).decode("utf-8"))
            except (ValueError, UnicodeDecodeError):
                return None
            return obj if isinstance(obj, dict) else None

        # ---- POST ------------------------------------------------------
        def do_POST(self):
            parsed = urllib.parse.urlparse(self.path)
            route = parsed.path.rstrip("/")
            if route == "/comment":
                self._guarded(self._post_comment)
            elif route in ("/comment-edit", "/comment-delete"):
                self._guarded(lambda: self._post_comment_change(editing=route == "/comment-edit"))
            elif route == "/upload":
                self._post_upload(parsed)
            else:
                self.send_error(404, "not found")

        def _post_comment(self):
            if self._body_length(MAX_COMMENT) is None:
                self.send_error(413, "bad size")
                return
            obj = self._read_json_body()
            if obj is None or not str(obj.get("text", "")).strip():
                self.send_error(400, "bad comment")
                return
            obj["stored"] = _now()
            obj.setdefault("id", obj["stored"] + "-" + str(os.getpid() % 1000))
            obj.setdefault("status", "open")
            save_comments(load_comments() + [obj])
            print(f"  comment on {obj.get('page', '?')}: {str(obj['text'])[:60]}")
            self._send_json(obj)

        def _post_comment_change(self, *, editing: bool):
            obj = self._read_json_body()
            if obj is None or not obj.get("id"):
                self.send_error(400, "bad request")
                return
            if editing and not str(obj.get("text", "")).strip():
                self.send_error(400, "empty text")
                return
            rows = load_comments()
            for c in rows:
                if c.get("id") == obj["id"] and editing:
                    c["text"], c["edited"] = obj["text"], _now()
            save_comments(rows if editing else [c for c in rows if c.get("id") != obj["id"]])
            self._send_json({"ok": True})

        def _post_upload(self, parsed):
            target = _upload_target(root, urllib.parse.parse_qs(parsed.query))
            if not target:
                self.send_error(400, "bad path")
                return
            dest, rel = target
            length = self._body_length(MAX_BYTES)
            if length is None:
                self.send_error(413, "bad size")
                return
            data = self.rfile.read(length)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            print(f"  wrote {rel}  ({len(data)} bytes)")
            self._send_json({"ok": True, "path": rel})

        def log_message(self, fmt, *args):
            pass  # quiet the per-request GET spam; POSTs print their own line

    return Handler


def _lan_ip():
    """Best-effort LAN IP (the WSL address), or None."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
    except OSError:
        return None
    return ip


def rebuild(doc_root, command: str, *, store=None):
    """Run *command* in *doc_root* and report what it changed: the root-relative
    URLs of pages added and pages removed, as two sorted lists (a set
    difference over the site's pages before and after). Raises
    :class:`ServeError` with the exit code when the command fails."""
    import subprocess

    root = pygim.path(doc_root, store=store or PathStore()).resolve()
    before = site_pages(root)
    rc = subprocess.run(command, shell=True, cwd=os.fspath(root), check=False).returncode
    if rc:
        raise ServeError(f"rebuild command failed (exit {rc}): {command}")
    after = site_pages(root)
    added = sorted(_relative(root, p) for p in after - before)
    removed = sorted(_relative(root, p) for p in before - after)
    return added, removed


def make_server(doc_root, *, port: int = 8000, host: str | None = None,
                index: str | None = None, store=None) -> socketserver.TCPServer:
    """Build (and bind) the server for *doc_root* without running it.

    *store* is the PathStore every path the server makes lives in (default: a
    fresh one, so the module's default store never sees request traffic).
    Raises :class:`ServeError` if it cannot bind, and ``FileNotFoundError`` if
    *doc_root* is not a directory. ``port=0`` picks a free port (tests)."""
    root = pygim.path(doc_root, store=store or PathStore()).resolve()
    if not root.is_dir():
        raise FileNotFoundError(f"doc root not found: {root}")
    pregenerate(root)   # every Markdown page has its HTML before the first request

    # Bind all interfaces by default so the page is reachable via the WSL IP even
    # when Windows→WSL localhost forwarding hiccups (a common "can't connect").
    # NB: PYGIM_HOST, not HOST — interactive shells often set $HOST to the machine
    # name, which would try to bind an unresolvable hostname (Errno -3).
    host = host or os.environ.get(HOST_ENV, "0.0.0.0")
    # POSIX: reuse the address so a restart does not wait out TIME_WAIT. Windows:
    # SO_REUSEADDR there lets a SECOND server bind the same port (no error, split
    # traffic), so leave it off — Windows does not have the TIME_WAIT bind problem.
    socketserver.TCPServer.allow_reuse_address = os.name != "nt"
    handler = _make_handler(root, _pick_index(root, index))
    try:
        return socketserver.TCPServer((host, port), handler)
    except OSError as e:
        lines = [f"Could NOT start on {host}:{port} — {e}", ""]
        if getattr(e, "errno", None) == errno.EADDRINUSE:
            lines += [
                "That port is already in use. Either:",
                "  • stop the old server:  pkill -f 'oo docs serve'   (then run this again), or",
                f"  • use another port:     oo docs serve --port {port + 1}",
            ]
        else:
            lines += [
                f"Binding to host {host!r} failed (a name-resolution / interface error, not the port).",
                "Force the interface explicitly:",
                f"  oo docs serve --host 0.0.0.0 --port {port}      # all interfaces (localhost + LAN IP)",
                f"  oo docs serve --host 127.0.0.1 --port {port}    # localhost only",
            ]
        raise ServeError("\n".join(lines)) from e


def serve(doc_root, *, port: int = 8000, host: str | None = None,
          index: str | None = None, store=None) -> None:
    """Serve *doc_root* on *port* until Ctrl-C. See :func:`make_server` for errors."""
    httpd = make_server(doc_root, port=port, host=host, index=index, store=store)
    port = httpd.server_address[1]
    ip = _lan_ip()
    print("Docs are UP. Open EITHER url in your browser:")
    print(f"    http://localhost:{port}/")
    if ip:
        print(f"    http://{ip}:{port}/      (use this one if localhost won't connect)")
    root = pygim.path(doc_root, store=store or PathStore()).resolve()
    print(f"\nServing {root}  ({len(site_pages(root))} pages; GET /pages lists them)")
    print(f"Comments (✎) land in {COMMENTS_REL}; image drops write under images/. Ctrl-C to stop.")
    print("(Bound on all interfaces for WSL reachability — it's LAN-visible while running.)\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        httpd.server_close()
