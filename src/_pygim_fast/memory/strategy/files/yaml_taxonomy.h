// memory/strategy/files/yaml_taxonomy.h — the vocabulary from YAML (02 §1–2).
//
// One file for the base and one per pack. Base first, packs by file name,
// entries in file order: the same files always intern to the same ids (02
// §2.1). Every failure is collected and reported with its file and line.
// rapidyaml must be defined once per extension (RYML_SINGLE_HDR_DEFINE_NOW in
// bindings.cpp) before this header is included there.
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../../../pathlike/adapter/third_party/rapidyaml/ryml_all.hpp"
#include "../../core/service.h"

namespace pygim::memory::strategy::files {

struct taxonomy_file {
    std::string name;  // as reported in messages: "taxonomy/pack-dnd.yaml"
    std::string text;
};

namespace yaml_detail {

struct parse_failure : std::runtime_error {
    std::size_t line;
    parse_failure(std::string m, std::size_t l) : std::runtime_error(std::move(m)), line(l) {}
};

[[noreturn]] inline void throw_on_error(const char* msg, std::size_t len, ryml::Location loc, void*) {
    throw parse_failure(std::string(msg, len), loc.line + 1);
}

inline void ensure_throwing_callbacks() {
    static const bool installed = [] {
        ryml::Callbacks cb = ryml::get_callbacks();
        cb.m_error = &throw_on_error;
        ryml::set_callbacks(cb);
        return true;
    }();
    (void)installed;
}

[[nodiscard]] inline std::string text(ryml::csubstr s) {
    std::string out(s.str ? s.str : "", s.len);
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\r')) out.pop_back();
    return out;
}

[[nodiscard]] inline std::optional<ryml::ConstNodeRef> child(ryml::ConstNodeRef n, std::string_view key) {
    if (!n.readable() || !n.is_map()) return std::nullopt;
    for (ryml::ConstNodeRef c : n.children())
        if (c.has_key() && std::string_view(c.key().str, c.key().len) == key) return c;
    return std::nullopt;
}

[[nodiscard]] inline std::string scalar(std::optional<ryml::ConstNodeRef> n) {
    if (!n || !n->has_val()) return {};
    return text(n->val());
}

[[nodiscard]] inline std::string_view key_of(ryml::ConstNodeRef n) { return {n.key().str, n.key().len}; }

[[nodiscard]] inline bool is_base(std::string_view name) {
    return name == "base.yaml" || name.ends_with("/base.yaml") || name.ends_with("\\base.yaml");
}

[[nodiscard]] inline std::string pack_of(std::string_view name) {
    const auto slash = name.find_last_of("/\\");
    std::string_view stem = slash == std::string_view::npos ? name : name.substr(slash + 1);
    if (stem.starts_with("pack-")) stem.remove_prefix(5);
    if (stem.ends_with(".yaml")) stem.remove_suffix(5);
    return std::string(stem);
}

/// Everything about one file's parse that a message needs.
struct document {
    const taxonomy_file& file;
    ryml::EventHandlerTree handler;
    ryml::Parser parser;
    ryml::Tree tree;

    explicit document(const taxonomy_file& f) : file(f), parser(&handler, ryml::ParserOptions().locations(true)) {
        ryml::parse_in_arena(&parser, ryml::to_csubstr(file.name), ryml::to_csubstr(file.text), &tree);
    }
    [[nodiscard]] std::string at(ryml::ConstNodeRef n) const {
        return file.name + ":" + decimal(parser.location(n).line + 1);
    }
};

}  // namespace yaml_detail

/// Loads a vocabulary from its files (02 §2). Returns every error at once.
[[nodiscard]] inline taxonomy_load load_taxonomy(std::vector<taxonomy_file> files) {
    using namespace yaml_detail;
    ensure_throwing_callbacks();
    std::stable_sort(files.begin(), files.end(), [](const taxonomy_file& a, const taxonomy_file& b) {
        const bool ab = is_base(a.name), bb = is_base(b.name);
        if (ab != bb) return ab;
        return a.name < b.name;
    });

    taxonomy_load out;
    auto tax = std::make_shared<taxonomy>();
    struct later_see {
        std::string where;
        std::string see;
    };
    std::vector<later_see> sees;

    if (files.empty() || !is_base(files.front().name)) out.errors.push_back("taxonomy/base.yaml: missing — every repository starts from the base vocabulary");

    for (const auto& f : files) {
        std::unique_ptr<document> doc;
        try {
            doc = std::make_unique<document>(f);
        } catch (const parse_failure& e) {
            out.errors.push_back(f.name + ":" + decimal(e.line) + ": not valid YAML: " + e.what());
            continue;
        }
        const ryml::ConstNodeRef root = doc->tree.crootref();
        if (!root.is_map()) {
            out.errors.push_back(f.name + ":1: a taxonomy file is a mapping of pack, entry, dimensions, extends, rejected");
            continue;
        }
        const bool base = is_base(f.name);
        const std::string pack = base ? "base" : pack_of(f.name);
        const std::string declared = scalar(child(root, "pack"));
        if (declared != pack)
            out.errors.push_back(doc->at(root) + ": pack \"" + declared + "\" — " +
                                 (base ? std::string("the base file declares pack: base")
                                       : "the file name says pack \"" + pack + "\""));

        auto entry_of = [&](ryml::ConstNodeRef owner, const std::string& name, bool warn_full) -> std::optional<codebook_entry> {
            const auto e = child(owner, "entry");
            if (!e || !e->is_map()) {
                out.errors.push_back(doc->at(owner) + ": " + name + ": no entry");
                return std::nullopt;
            }
            codebook_entry ce{scalar(child(*e, "brief")), scalar(child(*e, "full")), scalar(child(*e, "when")),
                              scalar(child(*e, "when_not")), scalar(child(*e, "example"))};
            if (const auto why = incomplete(ce, name.substr(name.find('=') + 1)); !why.empty()) {
                out.errors.push_back(doc->at(*e) + ": " + name + ": entry " + why);
                return std::nullopt;
            }
            if (warn_full && ce.full.empty()) out.warnings.push_back(doc->at(*e) + ": " + name + ": entry has no full description");
            return ce;
        };

        auto add_values = [&](ryml::ConstNodeRef values, dimension_id d) {
            if (!values.is_map()) {
                out.errors.push_back(doc->at(values) + ": values is a mapping of value to entry");
                return;
            }
            for (ryml::ConstNodeRef v : values.children()) {
                const std::string value(key_of(v));
                const std::string qualified = tax->info(d).name + "=" + value;
                if (value == taxonomy::any_value) {
                    out.errors.push_back(doc->at(v) + ": " + qualified + ": `any` is reserved and added by the loader");
                    continue;
                }
                const auto e = entry_of(v, qualified, false);
                if (!e) continue;
                std::optional<source_ref> src;
                if (const auto s = child(v, "source")) {
                    source_ref r;
                    r.doc = scalar(child(*s, "doc"));
                    const auto line = scalar(child(*s, "line"));
                    const auto lines = scalar(child(*s, "lines"));
                    for (const char c : line) r.line = r.line * 10 + static_cast<std::uint32_t>(c - '0');
                    for (const char c : lines) r.lines = r.lines * 10 + static_cast<std::uint32_t>(c - '0');
                    r.passage = scalar(child(*s, "passage"));
                    if (r.doc.empty() || r.line == 0) out.errors.push_back(doc->at(*s) + ": " + qualified + ": source needs doc and line");
                    src = std::move(r);
                }
                const auto t = tax->add_value(d, value, *e, std::move(src));
                if (!t) {
                    out.errors.push_back(doc->at(v) + ": " + qualified + " is defined twice");
                    continue;
                }
                if (const auto retired = child(v, "retired")) {
                    const std::string reason = scalar(retired);
                    if (reason.empty()) out.errors.push_back(doc->at(*retired) + ": " + qualified + " retired without a reason");
                    const std::string see = scalar(child(v, "see"));
                    if (!see.empty()) sees.push_back({doc->at(v) + ": " + qualified, see});
                    tax->retire(*t, reason, see);
                }
            }
        };

        if (!base) {
            const auto d = tax->dimension("domain");
            if (d) {
                if (const auto e = entry_of(root, "domain=" + pack, false)) {
                    if (!tax->add_value(*d, pack, *e)) out.errors.push_back(doc->at(root) + ": domain=" + pack + " is defined twice");
                }
            }
        }

        if (const auto dims = child(root, "dimensions")) {
            for (ryml::ConstNodeRef dn : dims->children()) {
                const std::string name(key_of(dn));
                dimension_info info;
                info.name = name;
                info.pack = pack;
                const std::string r = scalar(child(dn, "role"));
                if (r == "hard") info.default_role = role::hard;
                else if (r == "soft") info.default_role = role::soft;
                else out.errors.push_back(doc->at(dn) + ": " + name + ": role \"" + r + "\" is not hard or soft");
                const std::string w = scalar(child(dn, "weight"));
                if (const auto pw = parse_weight(w)) info.weight = *pw;
                else out.errors.push_back(doc->at(dn) + ": " + name + ": weight \"" + w +
                                          "\" must be above zero, at most 1000, with at most three decimals");
                const auto e = entry_of(dn, name, true);
                if (!e) continue;
                info.entry = *e;
                const auto d = tax->add_dimension(std::move(info));
                if (!d) {
                    out.errors.push_back(doc->at(dn) + ": dimension \"" + name + "\" is already defined");
                    continue;
                }
                if (const auto values = child(dn, "values")) add_values(*values, *d);
                if (tax->default_role(*d) == role::hard) tax->add_any(*d);
            }
        }
        if (const auto ext = child(root, "extends")) {
            for (ryml::ConstNodeRef en : ext->children()) {
                const std::string name(key_of(en));
                const auto d = tax->dimension(name);
                if (!d) {
                    out.errors.push_back(doc->at(en) + ": extends \"" + name + "\", which no file defines before this one");
                    continue;
                }
                add_values(en, *d);
            }
        }
        if (const auto rej = child(root, "rejected")) {
            for (ryml::ConstNodeRef rn : rej->children()) {
                rejection r{scalar(child(rn, "concept")), scalar(child(rn, "reason")), scalar(child(rn, "see")), pack};
                if (r.concept_name.empty() || r.reason.empty())
                    out.errors.push_back(doc->at(rn) + ": a rejection needs a concept and a reason");
                if (!r.see.empty()) sees.push_back({doc->at(rn) + ": rejected \"" + r.concept_name + "\"", r.see});
                tax->add_rejection(std::move(r));
            }
        }
    }
    for (const auto& s : sees)
        if (!tax->tag(s.see)) out.errors.push_back(s.where + ": see " + s.see + ", which is not in the vocabulary");
    if (tax->tags() >= 0xffff) out.errors.push_back("the vocabulary holds more than 65 534 values");
    out.tax = std::move(tax);
    return out;
}

}  // namespace pygim::memory::strategy::files
