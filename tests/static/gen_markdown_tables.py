#!/usr/bin/env python
"""Generate src/_pygim_fast/pathlike/markdown/tables.h from this interpreter.

CommonMark leans on three Unicode facts and one HTML fact, and each one is a
table this script writes as constexpr data:

- punctuation: code points in the Unicode P or S general categories (the
  flanking rules of emphasis, CommonMark 0.31.2 §6.2);
- whitespace: the Zs category plus tab, line feed, form feed and carriage
  return (the same rules);
- case folding: the full Unicode case fold, which matches link labels
  (``[ẞ]`` matches a definition of ``[SS]``);
- entities: the HTML5 named character references that end in ``;`` (an
  entity reference is only recognised with its semicolon);
- ASCII folding: what Python-Markdown's heading ids keep of a character
  (NFKD, then only its ASCII part: ``é`` -> ``e``, ``ﬁ`` -> ``fi``), for the
  ``toc_slug`` policy;
- lower-casing: the full Unicode lower-case mapping (``İ`` -> ``i̇``), and
  the Cased and Case_Ignorable properties its Final_Sigma rule reads (``Σ``
  ending a word is ``ς``), for the ``github_slug`` policy — GitHub lower-cases
  with JavaScript's toLowerCase, which is this mapping, not case folding.

The interpreter is the oracle: ``unicodedata`` for the categories,
``str.casefold`` for folding, ``str.lower`` for lower-casing,
``html.entities.html5`` for entities and ``unicodedata.normalize("NFKD")`` for
ASCII folding. Python exposes neither Cased nor Case_Ignorable, so both are
read off ``str.lower`` itself, whose Final_Sigma rule is Unicode's: a ``Σ``
after a cased letter is ``σ`` when a cased letter follows (case-ignorable
code points skipped) and ``ς`` otherwise.
The header records this script's sha256 and the sha256 of the tables below
it, so tests/unittests/test_pathlike_markdown.py checks in a millisecond that
the committed file is this script's output and was not edited by hand,
without regenerating 1.1 million code points on every test run.

Run:  python tests/static/gen_markdown_tables.py
"""

from __future__ import annotations

import hashlib
import html.entities
import sys
import unicodedata
import pygim

HERE = pygim.path(__file__).resolve().parent
OUT = HERE.parents[1] / "src" / "_pygim_fast" / "pathlike" / "markdown" / "tables.h"


def cpp_bytes(s: str) -> str:
    """A C++ string literal of the UTF-8 bytes of s, every non-ASCII-printable byte as an octal escape."""
    out = []
    for b in s.encode("utf-8"):
        c = chr(b)
        if c in '"\\' or not (0x20 <= b < 0x7F):
            out.append(f"\\{b:03o}")
        else:
            out.append(c)
    return '"' + "".join(out) + '"'


def ranges(pred) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for cp in range(sys.maxunicode + 1):
        if pred(cp):
            if out and out[-1][1] == cp - 1:
                out[-1] = (out[-1][0], cp)
            else:
                out.append((cp, cp))
    return out


def _sigma(after: str) -> str:
    """What str.lower makes of a Sigma after a cased letter and before `after`."""
    return ("A\u03a3" + after).lower()[1]


def case_ignorable(cp: int) -> bool:
    # Skipped, the B that follows is cased; not skipped, the code point itself decides.
    return _sigma(chr(cp) + "B") == "\u03c3" and _sigma(chr(cp)) == "\u03c2"


def cased(cp: int) -> bool:
    # A cased code point after the Sigma keeps it medial (case-ignorable ones are skipped first).
    return _sigma(chr(cp)) == "\u03c3"


def render() -> str:
    punct = ranges(lambda cp: unicodedata.category(chr(cp))[0] in "PS")
    space = [cp for cp in range(sys.maxunicode + 1) if unicodedata.category(chr(cp)) == "Zs"]
    space = sorted(set(space) | {0x09, 0x0A, 0x0C, 0x0D})
    folds = [(cp, chr(cp).casefold()) for cp in range(sys.maxunicode + 1)
             if not 0xD800 <= cp <= 0xDFFF and chr(cp).casefold() != chr(cp)]
    lowers = [(cp, chr(cp).lower()) for cp in range(sys.maxunicode + 1)
              if not 0xD800 <= cp <= 0xDFFF and chr(cp).lower() != chr(cp)]
    ignorable = ranges(lambda cp: not 0xD800 <= cp <= 0xDFFF and case_ignorable(cp))
    is_cased = ranges(lambda cp: not 0xD800 <= cp <= 0xDFFF and cased(cp))
    entities = sorted((name[:-1], text) for name, text in html.entities.html5.items() if name.endswith(";"))
    ascii_folds = []
    for cp in range(0x80, sys.maxunicode + 1):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        folded = unicodedata.normalize("NFKD", chr(cp)).encode("ascii", "ignore").decode("ascii")
        if folded:
            ascii_folds.append((cp, folded))

    lines = [
        "#include <string_view>",
        "",
        "namespace pygim::pathlike::markdown::tables {",
        "",
        f'inline constexpr std::string_view unicode_version = "{unicodedata.unidata_version}";',
        "",
        "struct cp_range {",
        "    char32_t lo;",
        "    char32_t hi;",
        "};",
        "struct fold {",
        "    char32_t cp;",
        "    std::string_view to;",
        "};",
        "struct entity {",
        "    std::string_view name;",
        "    std::string_view text;",
        "};",
        "struct ascii_fold {",
        "    char32_t cp;",
        "    std::string_view to;",
        "};",
        "",
        f"// Unicode P and S general categories: {len(punct)} ranges.",
        "inline constexpr cp_range punctuation[] = {",
    ]
    for i in range(0, len(punct), 4):
        lines.append("    " + " ".join(f"{{0x{lo:X}, 0x{hi:X}}}," for lo, hi in punct[i:i + 4]))
    lines += ["};", "", f"// Zs plus tab, line feed, form feed, carriage return: {len(space)} code points.",
              "inline constexpr char32_t whitespace[] = {"]
    for i in range(0, len(space), 8):
        lines.append("    " + " ".join(f"0x{cp:X}," for cp in space[i:i + 8]))
    lines += ["};", "", f"// Full case folding (str.casefold): {len(folds)} code points that fold.",
              "inline constexpr fold casefold[] = {"]
    for i in range(0, len(folds), 4):
        lines.append("    " + " ".join(f"{{0x{cp:X}, {cpp_bytes(to)}}}," for cp, to in folds[i:i + 4]))
    lines += ["};", "", f"// Full lower-casing (str.lower, one code point at a time): {len(lowers)} code points that change.",
              "inline constexpr fold lowercase[] = {"]
    for i in range(0, len(lowers), 4):
        lines.append("    " + " ".join(f"{{0x{cp:X}, {cpp_bytes(to)}}}," for cp, to in lowers[i:i + 4]))
    lines += ["};", "", f"// Case_Ignorable, as str.lower's Final_Sigma rule reads it: {len(ignorable)} ranges.",
              "inline constexpr cp_range case_ignorable[] = {"]
    for i in range(0, len(ignorable), 4):
        lines.append("    " + " ".join(f"{{0x{lo:X}, 0x{hi:X}}}," for lo, hi in ignorable[i:i + 4]))
    lines += ["};", "", f"// Cased and not case-ignorable, as the same rule reads it: {len(is_cased)} ranges.",
              "inline constexpr cp_range cased[] = {"]
    for i in range(0, len(is_cased), 4):
        lines.append("    " + " ".join(f"{{0x{lo:X}, 0x{hi:X}}}," for lo, hi in is_cased[i:i + 4]))
    lines += ["};", "", f"// HTML5 named character references ending in ';' (name without '&' and ';'): {len(entities)}.",
              "inline constexpr entity entities[] = {"]
    for name, text in entities:
        lines.append(f"    {{{cpp_bytes(name)}, {cpp_bytes(text)}}},")
    lines += ["};", "", f"// NFKD's ASCII part of non-ASCII code points that have one: {len(ascii_folds)}.",
              "inline constexpr ascii_fold ascii_folds[] = {"]
    for i in range(0, len(ascii_folds), 4):
        lines.append("    " + " ".join(f"{{0x{cp:X}, {cpp_bytes(to)}}}," for cp, to in ascii_folds[i:i + 4]))
    lines += ["};", "", "}  // namespace pygim::pathlike::markdown::tables", ""]
    body = "\n".join(lines)
    generator = hashlib.sha256(pygim.path(__file__).read_bytes().replace(b"\r\n", b"\n")).hexdigest()   # CRLF read as LF (#31)
    head = [
        "#pragma once",
        "// pathlike/markdown/tables.h — GENERATED by tests/static/gen_markdown_tables.py; do not edit.",
        "//",
        f"// Unicode {unicodedata.unidata_version} (Python's unicodedata) and the HTML5 named character",
        "// references (html.entities.html5). Every table is sorted, so a lookup is a binary search",
        "// (unicode.h). Regenerate after a Python upgrade that changes the Unicode version.",
        f"// generator sha256 {generator}",
        f"// body sha256 {hashlib.sha256(body.encode('utf-8')).hexdigest()}",
    ]
    return "\n".join(head) + "\n\n" + body


if __name__ == "__main__":
    OUT.write_bytes(render().encode("utf-8"))   # LF on every platform: no text-mode translation
    print(f"wrote {OUT}")
