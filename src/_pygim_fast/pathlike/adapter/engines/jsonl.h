#pragma once
// pathlike/adapter/engines/jsonl.h — JSON Lines (ndjson) via simdjson's document stream.
//
// One file IS one engine: the descriptor at the bottom is what the build
// discovers into the registry (see ../../registry.h).
//
// A .jsonl file is a sequence of JSON documents, one per line; it reads as a
// Python list (one item per document) and writes from a list (one line per
// item). Reading uses simdjson's parse_many(), so the separator rule is
// simdjson's: documents are delimited by whitespace, newlines included — every
// well-formed JSONL/ndjson file qualifies. Writing emits each item through the
// shared ryml JSON emitter (engine_yaml.h), which is single-line by
// construction; that property is asserted, never patched up after the fact.

#include <algorithm>
#include <array>
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

[[nodiscard]] inline py::object load_jsonl(const file& f, KeyCache& keys) {
    simdjson::dom::parser parser;   // must outlive the elements it returns
    std::string padded;   // the file, simdjson's padding past it: the documents point into it
    simdjson::dom::document_stream docs;
    {
        py::gil_scoped_release nogil;
        padded = read_padded_for_simdjson(f);
        require_utf8(padded, f.fspath());
        // parse_many() skips a UTF-8 BOM and yields zero documents for empty
        // input (both documented), so there is nothing to special-case here.
        if (auto err = parser.parse_many(padded).get(docs); err) {
            throw std::invalid_argument("JSONL parse error (" + f.fspath() + "): " +
                                        simdjson::error_message(err));
        }
    }
    py::list out;
    const std::string_view source(padded.data(), padded.size());
    for (auto it = docs.begin(); it != docs.end(); ++it) {
        simdjson::dom::element el;
        if (auto err = (*it).get(el); err) {
            throw std::invalid_argument("JSONL parse error (" + f.fspath() + ", line " +
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
                throw std::invalid_argument("JSONL parse error (" + f.fspath() + ", line " +
                                            std::to_string(line_of(source, at)) + "): the last document is incomplete");
            }
        }
    }
    return out;
}

// One compact JSON document per line. The root must be a list/tuple — JSONL
// *is* a sequence — and each item may be any JSON value.
inline void write_jsonl(const file& f, py::handle obj) {
    if (!py::isinstance<py::list>(obj) && !py::isinstance<py::tuple>(obj)) {
        throw std::invalid_argument("jsonl: the root must be a list or tuple (one document per line), got " +
                                    py::str(py::type::of(obj)).cast<std::string>());
    }
    std::string text;
    for (auto item : obj.cast<py::sequence>()) {
        ryml::Tree tree;
        py_to_node(tree, tree.rootref(), item, /*json_mode=*/true);
        std::string line;
        ryml::emitrs_json(tree, tree.root_id(), &line);
        if (line.find('\n') != std::string::npos) {
            throw std::runtime_error("jsonl: emitter produced a multi-line document");
        }
        text += line;
        text += '\n';
    }
    py::gil_scoped_release nogil;
    write_text_file(f, text);
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
               "and writes one compact document per line from a list root.",
        .exts = exts,
        .aliases = aliases,
    };

    static py::object load(const file& f, detail::KeyCache& keys) { return detail::load_jsonl(f, keys); }
    static void write(const file& f, py::handle obj) { detail::write_jsonl(f, obj); }
};

}  // namespace pygim::pathlike::engines
