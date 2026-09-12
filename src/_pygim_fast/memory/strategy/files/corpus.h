// memory/strategy/files/corpus.h — hand-written memory files (Feature 4).
//
// A block starts with `## <slug>`, then header lines `key: value[, value…]`
// — `title`, or a dimension — then a blank line and the text, up to the next
// block. Anything before the first block is a preamble and is skipped. A
// corpus file is not a source: its blocks are memories (overview §4.3).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "../../core/service.h"

namespace pygim::memory::strategy::files {

struct corpus_parse {
    std::vector<corpus_block> blocks;
    std::vector<std::string> errors;  // "notes/spells.md:7: bad header line …"
};

[[nodiscard]] inline corpus_parse parse_corpus(std::string_view text, std::string_view name) {
    corpus_parse out;
    auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    };
    std::vector<std::string_view> lines;
    for (std::size_t pos = 0; pos <= text.size();) {
        const auto nl = text.find('\n', pos);
        const auto end = nl == std::string_view::npos ? text.size() : nl;
        lines.push_back(text.substr(pos, end - pos));
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    corpus_block* cur = nullptr;
    bool in_text = false;
    std::vector<std::string_view> body;
    auto finish = [&] {
        if (!cur) return;
        std::size_t a = 0, b = body.size();
        while (a < b && trim(body[a]).empty()) ++a;
        while (b > a && trim(body[b - 1]).empty()) --b;
        std::string t;
        for (std::size_t i = a; i < b; ++i) {
            if (i > a) t.push_back('\n');
            t.append(body[i]);
        }
        cur->text = std::move(t);
        if (cur->title.empty()) out.errors.push_back(std::string(name) + ":" + decimal(cur->line) + ": " + cur->slug + " has no title");
        if (cur->text.empty()) out.errors.push_back(std::string(name) + ":" + decimal(cur->line) + ": " + cur->slug + " has no text");
        body.clear();
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (line.starts_with("## ")) {
            finish();
            out.blocks.push_back({});
            cur = &out.blocks.back();
            cur->slug = std::string(trim(line.substr(3)));
            cur->line = i + 1;
            in_text = false;
            continue;
        }
        if (!cur) continue;  // the preamble
        if (in_text) {
            body.push_back(line);
            continue;
        }
        if (trim(line).empty()) {
            in_text = cur->title.size() || !cur->tags.empty();
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            out.errors.push_back(std::string(name) + ":" + decimal(i + 1) + ": bad header line in " + cur->slug + ": " + std::string(line));
            continue;
        }
        const std::string key(trim(line.substr(0, colon)));
        const std::string_view value = trim(line.substr(colon + 1));
        if (key == "title") {
            cur->title = std::string(value);
            continue;
        }
        for (std::size_t p = 0; p <= value.size();) {
            const auto comma = value.find(',', p);
            const auto v = trim(value.substr(p, comma == std::string_view::npos ? std::string_view::npos : comma - p));
            if (!v.empty()) cur->tags.push_back(key + "=" + std::string(v));
            if (comma == std::string_view::npos) break;
            p = comma + 1;
        }
    }
    finish();
    // blocks with errors are still returned; the service refuses what does not fit the vocabulary
    return out;
}

}  // namespace pygim::memory::strategy::files
