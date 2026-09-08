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
* **Image drops.** Dropping an image on a page POSTs it to
  ``/upload?path=images/<sub>/<file>`` and it is written straight into
  ``<root>/images/<sub>/<file>`` — no Downloads round-trip. Uploads land only
  under an ``images`` directory and only with an image extension.

File handling is pathlike (``pygim.path``); Python here is routing glue.

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
from _pygim._cli import _commenter

__all__ = ["ServeError", "make_server", "serve"]

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
)

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


def _pick_index(root, index: str | None) -> str | None:
    """The root-relative page ``/`` redirects to, or None to serve the root as-is."""
    if index:
        return index.replace("\\", "/").lstrip("/")
    if (root / "index.html").is_file():
        return None
    return next((c for c in INDEX_CANDIDATES if (root / c).is_file()), None)


def _make_handler(root, index: str | None):
    """A SimpleHTTPRequestHandler bound to *root* with the comment and upload endpoints."""
    comments = root / COMMENTS_REL  # a pathlike jsonlfile: read() -> list, write(list)

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
                page = urllib.parse.parse_qs(parsed.query).get("page", [None])[0]
                self._guarded(lambda: self._send_json([
                    c for c in load_comments()
                    if c.get("status", "open") == "open" and (page is None or c.get("page") == page)]))
                return
            if self.path in ("/", "/index.html") and index and not (root / "index.html").is_file():
                self.send_response(302)
                self.send_header("Location", "/" + index)
                self.end_headers()
                return
            # every served HTML page gets the ✎ commenter
            page = pygim.path(self.translate_path(parsed.path))
            if page.is_dir():
                page = page / "index.html"
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


def make_server(doc_root, *, port: int = 8000, host: str | None = None,
                index: str | None = None) -> socketserver.TCPServer:
    """Build (and bind) the server for *doc_root* without running it.

    Raises :class:`ServeError` if it cannot bind, and ``FileNotFoundError`` if
    *doc_root* is not a directory. ``port=0`` picks a free port (tests)."""
    root = pygim.path(doc_root).resolve()
    if not root.is_dir():
        raise FileNotFoundError(f"doc root not found: {root}")

    # Bind all interfaces by default so the page is reachable via the WSL IP even
    # when Windows→WSL localhost forwarding hiccups (a common "can't connect").
    # NB: PYGIM_HOST, not HOST — interactive shells often set $HOST to the machine
    # name, which would try to bind an unresolvable hostname (Errno -3).
    host = host or os.environ.get(HOST_ENV, "0.0.0.0")
    socketserver.TCPServer.allow_reuse_address = True
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
          index: str | None = None) -> None:
    """Serve *doc_root* on *port* until Ctrl-C. See :func:`make_server` for errors."""
    httpd = make_server(doc_root, port=port, host=host, index=index)
    port = httpd.server_address[1]
    ip = _lan_ip()
    print("Docs are UP. Open EITHER url in your browser:")
    print(f"    http://localhost:{port}/")
    if ip:
        print(f"    http://{ip}:{port}/      (use this one if localhost won't connect)")
    print(f"\nServing {pygim.path(doc_root).resolve()}")
    print(f"Comments (✎) land in {COMMENTS_REL}; image drops write under images/. Ctrl-C to stop.")
    print("(Bound on all interfaces for WSL reachability — it's LAN-visible while running.)\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        httpd.server_close()
