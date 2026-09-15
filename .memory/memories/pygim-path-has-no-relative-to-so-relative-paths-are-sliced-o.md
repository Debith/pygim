---
memory: c4a22bd2aa777eae3ed841cd0d3e4b05
title: "pygim.path has no relative_to, so relative paths are sliced out of strings"
origin: written
tags: ["domain=pygim","component=pathlike","artifact=api","task=design","kind=example","concern=public_api","concern=correctness","language=python"]
---
pathlib's `Path(p).relative_to(root)` gives `pkg/c.py`; `pygim.path` has no `relative_to` (nor `is_relative_to`), so callers cut the root's text off the front instead. Debith, 2026-09-15: "relative urls look weird".

Cases: the docs server builds page URLs as `"/" + os.fspath(p)[len(os.fspath(root)):]` with a separator replace (`_relative` in _pygim/_cli/_docs_serve.py); ad-hoc scripts slice the same way. Slicing breaks on a root given with a trailing separator, a different spelling of the same directory, or a path outside the root — where pathlib raises, slicing returns nonsense silently.

Other relative behaviour matches pathlib: `path('.').pathset('**/*')` yields `a.py, pkg, pkg/c.py`; `./a.py` normalises to `a.py`; `..` is kept. Work on it belongs on branch core/pathlike-improvements (worktree pygim-pathlike), cut from main.
