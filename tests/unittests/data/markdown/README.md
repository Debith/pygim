# Markdown conformance data

The examples `tests/unittests/test_pathlike_markdown.py` holds pathlike's
markdown engine to. Each file is vendored unchanged except as noted.

| File | What | Source | License |
|---|---|---|---|
| `commonmark-0.31.2.json` | the 652 examples of the CommonMark spec 0.31.2: markdown in, reference HTML out (only the `example`, `section`, `markdown` and `html` keys kept) | <https://spec.commonmark.org/0.31.2/spec.json> | CC BY-SA 4.0, Copyright (C) 2014-16 John MacFarlane |
| `gfm-0.29-extensions.json` | the 12 examples of the GFM spec's table, strikethrough and task list extensions, extracted from its `spec.txt` (`→` read as a tab) | <https://github.com/github/cmark-gfm/blob/master/test/spec.txt> | CC BY-SA 4.0, Copyright (C) 2014-16 John MacFarlane, GitHub |
| `spec_normalize.py` | the spec's HTML normaliser, which decides when two renderings are the same | `test/normalize.py` of <https://github.com/commonmark/commonmark-spec> at tag 0.31.2 | BSD 2-Clause, Copyright (c) 2014 John MacFarlane |

Changes to `spec_normalize.py`: two regular expressions are raw strings
(Python 3.12 warns on `'\s'`), and `normalize_html`'s docstring is shortened
— its examples would run as doctests here, and one prints a code point it
spells as an escape.

The GFM autolink and tag-filter extensions are not implemented, so their
examples are not here (see `docs/design/pathlike_markdown.md`).
