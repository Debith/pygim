#pragma once
// pathlike/markdown/tree.h — a parsed document: blocks, the lines they cover, their content.
//
// Pybind-free. The tree never copies the source: every block names the
// physical lines it covers (a byte span and 1-based line numbers) and its
// content as SEGMENTS — byte ranges of those lines with the container
// prefixes (`> `, list indentation) and markers already stripped. That is
// what makes an edit lossless: replacing a block splices its byte span, and
// every other byte of the file stays as it was.
//
// Worked example — the source
//
//     1  # Title
//     2
//     3  > quoted
//     4  > text
//
// parses into
//
//     #0 document     lines 1-4  children #1 #2
//     #1 heading      lines 1-1  level 1, segment "Title" (bytes 2..7)
//     #2 quote        lines 3-4  children #3
//     #3 paragraph    lines 3-4  segments "quoted" (line 3), "text" (line 4)
//
// Line 2 belongs to no block: blank lines between blocks are kept by the
// splice, never owned by a block.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../../wiring/registry/core.h"   // StaticRegistryCore: the label table is a registry (ENACT #131)

namespace pygim::pathlike::markdown {

inline constexpr std::uint32_t none = 0xFFFFFFFFu;

enum class kind : std::uint8_t {
    document,
    front_matter,    // `---` YAML or `+++` TOML at the very start
    heading,         // ATX (`# x`) or setext (underlined)
    paragraph,
    code,            // fenced or indented
    html,            // an HTML block (seven start conditions)
    thematic_break,
    quote,           // `>` block quote
    list,
    item,
    table,           // GFM
    definition,      // a link reference definition: `[label]: /url "title"`
};

[[nodiscard]] constexpr std::string_view kind_name(kind k) noexcept {
    switch (k) {
        case kind::document: return "document";
        case kind::front_matter: return "front_matter";
        case kind::heading: return "heading";
        case kind::paragraph: return "paragraph";
        case kind::code: return "code";
        case kind::html: return "html";
        case kind::thematic_break: return "thematic_break";
        case kind::quote: return "quote";
        case kind::list: return "list";
        case kind::item: return "item";
        case kind::table: return "table";
        case kind::definition: return "definition";
    }
    return "?";
}

/// A GFM table column's alignment.
enum class align : std::uint8_t { none, left, center, right };

/// Content: source bytes [begin, end) of one line, preceded by `pad`
/// synthetic spaces (what remains of a tab that indentation only partly consumed).
struct segment {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint32_t line = 0;   // 1-based
    std::uint32_t pad = 0;
};

/// One block. Children form a doubly linked list through `first`/`next`.
/// The fields after `nseg` are meaningful only for the kinds named beside them.
struct block {
    kind type = kind::document;
    std::uint32_t parent = none, first = none, last = none, prev = none, next = none;
    std::uint32_t begin = 0, end = 0;               // whole physical lines, end past the last line ending
    std::uint32_t first_line = 0, last_line = 0;    // 1-based, inclusive
    std::uint32_t seg = 0, nseg = 0;                // content segments
    std::uint32_t level = 0;                        // heading: 1-6
    bool setext = false;                            // heading: underlined
    bool fenced = false;                            // code
    char marker = 0;                                // code: fence char; list/item: bullet or delimiter; front_matter: '-' or '+'
    std::uint32_t fence_length = 0, fence_offset = 0;   // code (fenced)
    segment info{};                                 // code (fenced): the raw info string
    std::uint32_t html_type = 0;                    // html: start condition 1-7
    bool ordered = false, tight = true;             // list, item
    std::uint32_t start = 1;                        // list, item (ordered)
    std::uint32_t marker_offset = 0, padding = 0;   // item
    std::int8_t task = -1;                          // item (GFM): -1 none, 0 unchecked, 1 checked
    std::uint32_t columns = 0, rows = 0, align = 0; // table: rows include the header; aligns[align..+columns)
    std::uint32_t def = none;                       // definition: index into tree::definitions
    bool open = true;                               // while parsing
};

/// A link reference definition, its label folded (unicode.h fold_label).
struct definition {
    std::string label;
    std::string destination;
    std::string title;
    std::uint32_t block = none;
};

/// The parse result. `blocks[0]` is the document; a removed block stays in
/// the vector, unlinked (its parent is `none`).
struct tree {
    std::vector<block> blocks;
    std::vector<segment> segments;
    std::vector<align> aligns;
    std::vector<definition> definitions;           // document order
    /// Folded label -> its FIRST definition: a registry over the flat (sorted)
    /// engine, filled once at the end of the parse in label order, so every
    /// insert appends (O(n log n) in all) and register_value's "keep what is
    /// there" is CommonMark's "the first definition of a label wins".
    ::pygim::core::StaticRegistryCore<std::string, std::uint32_t> labels;
    std::vector<std::uint32_t> line_starts;        // line n starts at line_starts[n - 1]
    std::uint32_t size = 0;                        // source bytes

    /// The first definition of a folded label, or nullptr. O(log n).
    [[nodiscard]] constexpr const definition* lookup(const std::string& label) const {
        const std::uint32_t* d = labels.try_get_const(label);
        return d ? &definitions[*d] : nullptr;
    }

    /// The byte offset where line `n` (1-based) starts; the source size past the last line.
    [[nodiscard]] constexpr std::uint32_t line_start(std::uint32_t n) const noexcept {
        return n >= 1 && n <= line_starts.size() ? line_starts[n - 1] : size;
    }
};

}  // namespace pygim::pathlike::markdown
