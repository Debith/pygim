#pragma once
// pathlike/markdown/slug.h — heading anchors: how a heading's text becomes an id.
//
// Pybind-free and constexpr. There is no one standard, so the rule is a
// policy (#81). Each has `slugify(text, out)`, and `anchors`: the ids one
// document has handed out, which makes a repeated slug unique by that rule's
// own reference — the second "Usage" heading is "usage-1" on GitHub and
// "usage_1" in Python-Markdown's toc, and the two differ beyond the separator.
//
//     text                    github_slug           toc_slug
//     "Hello, World!"         "hello-world"         "hello-world"
//     "Ünïcode  café"         "ünïcode--café"       "unicode-cafe"
//     "C++ & C#"              "c--c"                "c-c"
//     "2.1 Read a file"       "21-read-a-file"      "21-read-a-file"
//
//     repeats                 github_slug           toc_slug
//     "a", "a", "a"           a, a-1, a-2           a, a_1, a_2
//     "a_1", "a_1"            a_1, a_1-1            a_1, a_2
//     "", ""                  "", -1                _1, _2
//
// github_slug follows GitHub's anchors (github-slugger): lower-case; drop
// punctuation and symbols except `-` and `_`; every space a `-`, runs kept.
// toc_slug follows Python-Markdown's toc extension, which `oo docs serve`
// renders with today: NFKD to ASCII, drop what is not a word character,
// space or `-`, lower-case, collapse runs of space and `-` into one `-`.
//
// The ids handed out live in a RegistryCore over the open-addressing engine:
// hashed, so N headings cost N lookups, and constexpr, so the proofs run it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../../mapping/open_storage.h"
#include "../../wiring/registry/core.h"
#include "syntax.h"
#include "unicode.h"

namespace pygim::pathlike::markdown {

/// What a document's anchors remember, by id.
template <class V>
using anchor_table = ::pygim::core::RegistryCore<std::string, V, ::pygim::mapping::open_storage<std::string, V>,
                                                 ::pygim::core::NoHooks<std::string, V, V>, V>;

/// GitHub's heading anchors.
struct github_slug {
    static constexpr std::string_view name = "github";

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

    /// github-slugger's rule: a repeat of `s` becomes s-1, s-2, ..., the
    /// counter kept per original slug — so a later repeat starts where the
    /// last one stopped, and N repeats cost N steps.
    class anchors {
    public:
        [[nodiscard]] constexpr std::string next(std::string s) {
            const std::string original = s;
            while (m_seen.contains(s)) {
                std::uint32_t& n = *m_seen.try_get(original);
                s = original + "-" + decimal(++n);
            }
            m_seen.register_value(s, 0);
            return s;
        }

    private:
        anchor_table<std::uint32_t> m_seen;   // id -> how many repeats of it so far
    };
};

/// Python-Markdown's toc extension (its default `slugify`).
struct toc_slug {
    static constexpr std::string_view name = "toc";

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

    /// Python-Markdown's `unique(id, ids)`: while the id is taken or empty,
    /// `x_N` becomes `x_(N+1)` (N as an integer: `x_007` -> `x_8`) and any
    /// other id `x_1`. That walk is the same chain every time it starts from
    /// the same id, so each walk records where it ended on every id it
    /// passed, and a later walk jumps there: N repeats cost N steps, not N².
    class anchors {
    public:
        [[nodiscard]] constexpr std::string next(std::string id) {
            std::vector<std::string> walked;
            while (id.empty() || m_ended.contains(id)) {
                if (!id.empty()) {
                    walked.push_back(id);
                    const std::string& end = *m_ended.try_get_const(id);   // taken, like every id before it
                    if (end != id) {
                        id = end;
                        continue;
                    }
                }
                id = successor(id);
            }
            m_ended.register_value(id, id);
            for (const std::string& w : walked) m_ended.upsert_value(w, id);
            return id;
        }

    private:
        anchor_table<std::string> m_ended;   // id -> where the latest walk through it ended

        /// `^(.*)_([0-9]+)$` -> prefix + "_" + (number + 1); otherwise id + "_1".
        [[nodiscard]] static constexpr std::string successor(const std::string& id) {
            std::size_t d = id.size();
            while (d > 0 && id[d - 1] >= '0' && id[d - 1] <= '9') --d;
            if (d == id.size() || d == 0 || id[d - 1] != '_') return id + "_1";
            std::size_t z = d;   // int() drops leading zeros
            while (z + 1 < id.size() && id[z] == '0') ++z;
            std::string n = id.substr(z);
            std::size_t k = n.size();
            while (k > 0 && n[k - 1] == '9') n[--k] = '0';   // + 1, as long as it needs to be
            if (k == 0) n.insert(n.begin(), '1');
            else ++n[k - 1];
            return id.substr(0, d) + n;
        }
    };
};

template <class S>
concept SlugPolicy = requires(std::string_view t, std::string& out, typename S::anchors a, std::string s) {
    { S::name } -> std::convertible_to<std::string_view>;
    S::slugify(t, out);
    { a.next(s) } -> std::same_as<std::string>;
};

/// A slug of `text` under policy S.     slug<github_slug>("A b") -> "a-b"
template <SlugPolicy S>
[[nodiscard]] constexpr std::string slug(std::string_view text) {
    std::string out;
    S::slugify(text, out);
    return out;
}

}  // namespace pygim::pathlike::markdown
