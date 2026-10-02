#pragma once
// pathlike/markdown/slug.h — heading anchors: how a heading's text becomes an id.
//
// Pybind-free and constexpr. There is no one standard, so the rule is a
// policy (#81). Each has `slugify(text, out)` and the separator its
// de-duplication uses: the second "Usage" heading is "usage-1" on GitHub
// and "usage_1" in Python-Markdown's toc.
//
//     text                    github_slug           toc_slug
//     "Hello, World!"         "hello-world"         "hello-world"
//     "Ünïcode  café"         "ünïcode--café"       "unicode-cafe"
//     "C++ & C#"              "c--c"                "c-c"
//     "2.1 Read a file"       "21-read-a-file"      "21-read-a-file"
//
// github_slug follows GitHub's anchors (github-slugger): lower-case; drop
// punctuation and symbols except `-` and `_`; every space a `-`, runs kept.
// toc_slug follows Python-Markdown's toc extension, which `oo docs serve`
// renders with today: NFKD to ASCII, drop what is not a word character,
// space or `-`, lower-case, collapse runs of space and `-` into one `-`.

#include <cstddef>
#include <string>
#include <string_view>

#include "syntax.h"
#include "unicode.h"

namespace pygim::pathlike::markdown {

/// GitHub's heading anchors.
struct github_slug {
    static constexpr std::string_view name = "github";
    static constexpr std::string_view separator = "-";

    static constexpr void slugify(std::string_view text, std::string& out) {
        for (std::size_t i = 0; i < text.size();) {
            const code_point c = decode(text, i);
            i += c.len;
            if (c.cp == ' ') {
                out.push_back('-');
            } else if (c.cp == '-' || c.cp == '_') {
                out.push_back(static_cast<char>(c.cp));
            } else if (c.cp < 0x20 || (c.cp >= 0x7F && c.cp < 0xA0) || is_whitespace(c.cp) || is_punctuation(c.cp)) {
                continue;
            } else {
                std::string folded;
                append_fold(c.cp, folded);
                // lower-case, not case-fold: keep the letter when folding would expand it (ß -> ss)
                if (decode(folded, 0).len == folded.size()) out += folded;
                else encode(c.cp, out);
            }
        }
    }
};

/// Python-Markdown's toc extension (its default `slugify`).
struct toc_slug {
    static constexpr std::string_view name = "toc";
    static constexpr std::string_view separator = "_";

    static constexpr void slugify(std::string_view text, std::string& out) {
        std::string ascii;   // NFKD, ASCII part only
        for (std::size_t i = 0; i < text.size();) {
            const code_point c = decode(text, i);
            i += c.len;
            if (c.cp < 0x80) ascii.push_back(static_cast<char>(c.cp));
            else ascii += ascii_fold(c.cp);
        }
        std::string kept;   // [^\w\s-] removed, lower-cased
        for (char c : ascii) {
            if (is_ascii_alnum(c) || c == '_' || c == '-' || is_ascii_whitespace(c)) kept.push_back(ascii_lower(c));
        }
        const std::string_view s = trim_whitespace(kept);
        bool run = false;   // [-\s]+ -> "-"
        for (char c : s) {
            if (c == '-' || is_ascii_whitespace(c)) {
                run = true;
                continue;
            }
            if (run) out.push_back('-');
            run = false;
            out.push_back(c);
        }
    }
};

template <class S>
concept SlugPolicy = requires(std::string_view t, std::string& out) {
    { S::name } -> std::convertible_to<std::string_view>;
    { S::separator } -> std::convertible_to<std::string_view>;
    S::slugify(t, out);
};

/// A slug of `text` under policy S.     slug<github_slug>("A b") -> "a-b"
template <SlugPolicy S>
[[nodiscard]] constexpr std::string slug(std::string_view text) {
    std::string out;
    S::slugify(text, out);
    return out;
}

}  // namespace pygim::pathlike::markdown
