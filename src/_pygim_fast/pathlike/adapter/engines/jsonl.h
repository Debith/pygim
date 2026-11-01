#pragma once
// pathlike/adapter/engines/jsonl.h — JSON Lines (ndjson) via simdjson's document stream.
//
// One file IS one engine: the descriptor at the bottom is what the build
// discovers into the registry (see ../../registry.h).
//
// A .jsonl file is a sequence of JSON documents, one per line; it reads as a
// Python list (one item per document) and writes from a list (one line per
// item) or from one document (one line). Reading uses simdjson's parse_many(),
// so the separator rule is simdjson's: documents are delimited by whitespace,
// newlines included — every well-formed JSONL/ndjson file qualifies. Writing emits each item through the
// shared ryml JSON emitter (engine_yaml.h), which is single-line by
// construction; that property is asserted, never patched up after the fact.

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "../third_party/simdjson/simdjson.h"
#include "../../core.h"
#include "../common.h"
#include "json.h"
#include "yaml.h"

namespace pygim::pathlike::detail {

// 1-based line of byte offset `at` in `text` (for error messages).
[[nodiscard]] inline std::size_t line_of(std::string_view text, std::size_t at) noexcept {
    const std::size_t end = std::min(at, text.size());
    return 1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() + end, '\n'));
}

/// The documents of `source` (padded for simdjson) as a list, parsed in batches of `batch` bytes;
/// nullopt when a document is larger than a batch (simdjson's CAPACITY), so the caller parses again.
[[nodiscard]] inline std::optional<py::list> jsonl_rows(std::string_view source, std::string_view origin, std::size_t batch,
                                                        KeyCache& keys) {
    simdjson::dom::parser parser;   // must outlive the elements it returns; the documents point into `source`
    simdjson::dom::document_stream docs;
    {
        py::gil_scoped_release nogil;
        // parse_many() skips a UTF-8 BOM and yields zero documents for empty
        // input (both documented), so there is nothing to special-case here.
        if (auto err = parser.parse_many(source.data(), source.size(), batch).get(docs); err) {
            if (err == simdjson::CAPACITY) return std::nullopt;
            throw std::invalid_argument("JSONL parse error (" + std::string(origin) + "): " +
                                        simdjson::error_message(err));
        }
    }
    py::list out;
    for (auto it = docs.begin(); it != docs.end(); ++it) {
        simdjson::dom::element el;
        if (auto err = (*it).get(el); err) {
            if (err == simdjson::CAPACITY) return std::nullopt;
            throw std::invalid_argument("JSONL parse error (" + std::string(origin) + ", line " +
                                        std::to_string(line_of(source, it.current_index())) + "): " +
                                        simdjson::error_message(err));
        }
        out.append(json_to_py(el, keys));
    }
    // parse_many leaves an incomplete last document out of the stream and reports it only through
    // truncated_bytes(); without this, a file cut off mid-write reads back as its complete rows,
    // silently. truncated_bytes() reads stage 1's indexes, which exist only once input with
    // content was scanned — for an empty file they are uninitialised — so it is asked only then,
    // and the bytes it names must hold more than whitespace.
    static constexpr std::string_view blank = " \t\r\n\xEF\xBB\xBF";   // whitespace, and a BOM
    if (source.find_first_not_of(blank) != std::string_view::npos) {
        if (const std::size_t cut = docs.truncated_bytes(); cut > 0 && cut <= source.size()) {
            const std::size_t at = source.find_first_not_of(blank, source.size() - cut);
            if (at != std::string_view::npos) {
                throw std::invalid_argument("JSONL parse error (" + std::string(origin) + ", line " +
                                            std::to_string(line_of(source, at)) + "): the last document is incomplete");
            }
        }
    }
    return out;
}

/// JSON Lines text as a list, one item per document; `origin` names it in an error (a file's path,
/// or "<text>"). Parsed in simdjson's batch first, its sweet spot; a document larger than a batch
/// has the input parsed again in one batch its size, which holds any document in it. Found
/// 2026-10-31 decoding code_oracle's answers: a line over 1 MB was a CAPACITY error.
[[nodiscard]] inline py::object decode_jsonl(std::string padded, std::string_view origin, KeyCache& keys) {
    {
        py::gil_scoped_release nogil;
        require_utf8(padded, origin);
        pad_for_simdjson(padded);
    }
    const std::string_view source(padded.data(), padded.size());
    const std::size_t batch = simdjson::dom::DEFAULT_BATCH_SIZE;
    if (std::optional<py::list> rows = jsonl_rows(source, origin, batch, keys)) return std::move(*rows);
    if (source.size() > batch) {
        if (std::optional<py::list> rows = jsonl_rows(source, origin, source.size(), keys)) return std::move(*rows);
    }
    throw std::invalid_argument("JSONL parse error (" + std::string(origin) + "): " + simdjson::error_message(simdjson::CAPACITY));
}

// One compact JSON document a line. A list or tuple is the documents, one a line — JSONL *is* a
// sequence; any other value is one document, one line (Debith on PR #39, 2026-11-01: "for jsonl, I
// would accept single record directly without putting it to a list"). So one list written as one
// document goes inside a list: [[1, 2]].
/// `obj` as JSON Lines text — a list's or tuple's items, or `obj` itself — each line ended by "\n".
[[nodiscard]] inline std::string encode_jsonl(py::handle obj) {
    std::string text;
    const auto add_line = [&text](py::handle document) {
        ryml::Tree tree;
        py_to_node(tree, tree.rootref(), document, /*json_mode=*/true);
        std::string line;
        ryml::emitrs_json(tree, tree.root_id(), &line);
        if (line.find('\n') != std::string::npos) {
            throw std::runtime_error("jsonl: emitter produced a multi-line document");
        }
        text += line;
        text += '\n';
    };
    if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
        for (auto item : obj.cast<py::sequence>()) add_line(item);
    } else {
        add_line(obj);
    }
    return text;
}

}  // namespace pygim::pathlike::detail

// ── Registry entry ─────────────────────────────────────────────────────────
namespace pygim::pathlike::engines {

struct jsonl {
    static constexpr std::array<std::string_view, 2> exts{".jsonl", ".ndjson"};
    static constexpr std::array<std::string_view, 1> aliases{"ndjson"};
    static constexpr engine_info info{
        .name = "jsonl",
        .label = "simdjson-ndjson",
        .doc = "JSON Lines (ndjson) via simdjson's document stream: reads as a list, one item per document, "
               "and writes one compact document per line: a list's items, or one document.",
        .exts = exts,
        .aliases = aliases,
    };

    static py::object decode(std::string bytes, std::string_view origin, detail::KeyCache& keys) {
        return detail::decode_jsonl(std::move(bytes), origin, keys);
    }
    static std::string encode(py::handle obj) { return detail::encode_jsonl(obj); }
    /// Its documents are lines: write(..., append=True) adds them to a file's end.
    static constexpr bool appends = true;
};

}  // namespace pygim::pathlike::engines
