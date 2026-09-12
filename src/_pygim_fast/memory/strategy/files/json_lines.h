// memory/strategy/files/json_lines.h — rows, usage and receipts as JSON lines.
//
// One line per record, written with a small escaper and read back through
// rapidyaml's JSON parser. A row's fields are grouped by key — one value as a
// string, several as an array — so a line reads the way 03 §3.2 shows it; the
// id does not depend on that grouping, because canonical() sorts fields.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../../pathlike/adapter/third_party/rapidyaml/ryml_all.hpp"
#include "../../core/knowledge.h"
#include "../../core/provenance.h"
#include "../../core/retrieval.h"
#include "yaml_taxonomy.h"

namespace pygim::memory::strategy::files {

inline void json_string(std::string& out, std::string_view s) {
    constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 0xf]);
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}

inline void json_strings(std::string& out, const std::vector<std::string>& v) {
    out.push_back('[');
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out.push_back(',');
        json_string(out, v[i]);
    }
    out.push_back(']');
}

/// A row as one line, without the newline.
[[nodiscard]] inline std::string row_line(const row& r) {
    std::string out = "{\"id\":";
    json_string(out, r.id.hex());
    out += ",\"parents\":";
    std::vector<std::string> parents;
    for (const auto& p : r.parents) parents.push_back(p.hex());
    json_strings(out, parents);
    out += ",\"clone\":";
    json_string(out, r.clone);
    out += ",\"seq\":" + decimal(r.seq) + ",\"time\":";
    json_string(out, r.time);
    out += ",\"op\":";
    json_string(out, r.op);
    out += ",\"fields\":{";
    std::vector<std::string> keys;
    for (const auto& [k, v] : r.fields)
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i) out.push_back(',');
        json_string(out, keys[i]);
        out.push_back(':');
        std::vector<std::string> vals;
        for (const auto& [k, v] : r.fields)
            if (k == keys[i]) vals.push_back(v);
        if (vals.size() == 1) json_string(out, vals[0]);
        else json_strings(out, vals);
    }
    out += "}}";
    return out;
}

namespace json_detail {

struct parsed {
    ryml::EventHandlerTree handler;
    ryml::Parser parser{&handler};
    ryml::Tree tree;
    explicit parsed(std::string_view line) {
        yaml_detail::ensure_throwing_callbacks();
        ryml::parse_json_in_arena(&parser, ryml::csubstr(line.data(), line.size()), &tree);
    }
};

[[nodiscard]] inline std::string str(ryml::ConstNodeRef n) {
    if (!n.readable() || !n.has_val()) return {};
    return std::string(n.val().str ? n.val().str : "", n.val().len);
}

[[nodiscard]] inline std::vector<std::string> strs(std::optional<ryml::ConstNodeRef> n) {
    std::vector<std::string> out;
    if (!n) return out;
    if (n->is_seq()) {
        for (ryml::ConstNodeRef c : n->children()) out.push_back(str(c));
    } else if (n->has_val()) {
        out.push_back(str(*n));
    }
    return out;
}

[[nodiscard]] inline std::string field(ryml::ConstNodeRef root, std::string_view key) {
    const auto n = yaml_detail::child(root, key);
    return n ? str(*n) : std::string();
}

[[nodiscard]] inline std::uint64_t number(std::optional<ryml::ConstNodeRef> n) {
    std::uint64_t v = 0;
    if (!n) return v;
    for (const char c : str(*n))
        if (c >= '0' && c <= '9') v = v * 10 + static_cast<std::uint64_t>(c - '0');
    return v;
}

}  // namespace json_detail

/// A row from one line, with its id checked against its content; nullopt
/// when the line is not a row or the id does not match (a hand-edited row).
[[nodiscard]] inline std::optional<row> parse_row(std::string_view line, std::string* problem = nullptr) {
    using namespace json_detail;
    using yaml_detail::child;
    try {
        parsed p(line);
        const auto root = p.tree.crootref();
        row r;
        const auto id = digest::from_hex(field(root, "id"));
        for (const auto& s : strs(child(root, "parents")))
            if (auto d = digest::from_hex(s)) r.parents.push_back(*d);
        if (const auto c = child(root, "clone")) r.clone = str(*c);
        r.seq = number(child(root, "seq"));
        if (const auto t = child(root, "time")) r.time = str(*t);
        if (const auto o = child(root, "op")) r.op = str(*o);
        if (const auto f = child(root, "fields"); f && f->is_map()) {
            for (ryml::ConstNodeRef kv : f->children()) {
                const std::string key(kv.key().str, kv.key().len);
                for (auto& v : strs(kv)) r.add(key, std::move(v));
            }
        }
        seal(r);
        if (!id || *id != r.id) {
            if (problem) *problem = "its id does not match its content — edited by hand";
            return std::nullopt;
        }
        return r;
    } catch (const std::exception& e) {
        if (problem) *problem = std::string("not a row: ") + e.what();
        return std::nullopt;
    }
}

[[nodiscard]] inline std::string usage_line(const usage_record& u) {
    std::string out = "{\"memory\":";
    json_string(out, u.memory.hex());
    out += ",\"tag\":";
    json_string(out, u.tag);
    out += ",\"kind\":";
    json_string(out, u.kind);
    out += ",\"time\":";
    json_string(out, u.time);
    out += ",\"session\":" + decimal(u.session) + ",\"reason\":";
    json_string(out, u.reason);
    out += "}";
    return out;
}

[[nodiscard]] inline std::optional<usage_record> parse_usage(std::string_view line) {
    using namespace json_detail;
    using yaml_detail::child;
    try {
        parsed p(line);
        const auto root = p.tree.crootref();
        usage_record u;
        const auto m = digest::from_hex(field(root, "memory"));
        if (!m) return std::nullopt;
        u.memory = *m;
        if (const auto t = child(root, "tag")) u.tag = str(*t);
        if (const auto k = child(root, "kind")) u.kind = str(*k);
        if (const auto t = child(root, "time")) u.time = str(*t);
        u.session = number(child(root, "session"));
        if (const auto r = child(root, "reason")) u.reason = str(*r);
        return u;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] inline std::string receipt_line(const receipt& r) {
    std::string out = "{\"snapshot\":";
    json_string(out, r.snapshot.hex());
    out += ",\"version\":" + decimal(r.version) + ",\"taxonomy\":";
    json_string(out, r.taxonomy.hex());
    out += ",\"hard\":";
    json_strings(out, r.asked.hard);
    out += ",\"soft\":";
    json_strings(out, r.asked.soft);
    out += ",\"max\":" + decimal(r.asked.max_memories) + ",\"budget\":" + decimal(r.asked.budget) + ",\"keys\":";
    std::vector<std::string> keys;
    for (const auto& k : r.keys) keys.push_back(k.hex());
    json_strings(out, keys);
    out += ",\"time\":";
    json_string(out, r.time);
    out += ",\"session\":" + decimal(r.session) + "}";
    return out;
}

[[nodiscard]] inline std::optional<receipt> parse_receipt(std::string_view line) {
    using namespace json_detail;
    using yaml_detail::child;
    try {
        parsed p(line);
        const auto root = p.tree.crootref();
        receipt r;
        const auto s = digest::from_hex(field(root, "snapshot"));
        const auto t = digest::from_hex(field(root, "taxonomy"));
        if (!s || !t) return std::nullopt;
        r.snapshot = *s;
        r.taxonomy = *t;
        r.version = number(child(root, "version"));
        r.asked.hard = strs(child(root, "hard"));
        r.asked.soft = strs(child(root, "soft"));
        r.asked.max_memories = static_cast<std::uint32_t>(number(child(root, "max")));
        r.asked.budget = static_cast<std::uint32_t>(number(child(root, "budget")));
        for (const auto& k : strs(child(root, "keys")))
            if (auto d = digest::from_hex(k)) r.keys.push_back(*d);
        if (const auto tm = child(root, "time")) r.time = str(*tm);
        r.session = number(child(root, "session"));
        return r;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace pygim::memory::strategy::files
