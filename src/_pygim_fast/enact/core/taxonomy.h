// memory/core/taxonomy.h — the controlled vocabulary, loaded.
//
// Dimensions and their values, interned so that a tag is one dense id and a
// flat array says which dimension it answers (01 §2.3). Every entry carries a
// codebook entry (01 §5, 02 §2.3). Built by a loader (strategy/files), then
// read-only for the life of a snapshot. Pybind-free; no I/O.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../mapping/intern.h"
#include "ids.h"

namespace pygim::enact {

enum class role : std::uint8_t { soft, hard };

[[nodiscard]] constexpr std::string_view role_name(role r) noexcept { return r == role::hard ? "hard" : "soft"; }

/// The six-part description every dimension and value carries, after
/// MacQueen et al. 1998 (overview §4.8). The label is the entry's own name.
struct codebook_entry {
    std::string brief;
    std::string full;      // optional (02 §2.3)
    std::string when;
    std::string when_not;
    std::string example;
};

namespace detail {
[[nodiscard]] inline std::string folded(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string out;
    out.reserve(b - a);
    for (std::size_t i = a; i < b; ++i) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    return out;
}
}  // namespace detail

/// Why `entry` is not complete, or an empty string when it is (02 §2.3):
/// brief, when, when_not and example must be present; no part may be the
/// entry's own name or repeat another part. Form only — never meaning.
///
///     incomplete({"x", "", "", "b", "c"}, "defensive") -> "no when"
[[nodiscard]] inline std::string incomplete(const codebook_entry& e, std::string_view name) {
    const std::pair<std::string_view, const std::string*> parts[] = {
        {"brief", &e.brief}, {"full", &e.full}, {"when", &e.when}, {"when_not", &e.when_not}, {"example", &e.example}};
    const std::string own = detail::folded(name);
    std::vector<std::string> seen;
    for (const auto& [label, text] : parts) {
        const std::string f = detail::folded(*text);
        if (f.empty()) {
            if (label == "full") continue;
            return "no " + std::string(label);
        }
        if (f == own) return std::string(label) + " only repeats the name";
        if (std::find(seen.begin(), seen.end(), f) != seen.end()) return std::string(label) + " repeats another part";
        seen.push_back(f);
    }
    return {};
}

/// A value's citation into a source, as written in a taxonomy file (02 §1.2).
struct source_ref {
    std::string doc;
    std::uint32_t line = 0;
    std::uint32_t lines = 0;
    std::string passage;  // hex digest of the cited lines
};

struct dimension_info {
    std::string name;
    role default_role = role::soft;
    weight_t weight = weight_scale;
    codebook_entry entry;
    std::string pack;  // "base", or the domain a pack is named for
    bool request = false;  // offered before retrieval; stored-memory dimensions stay out
};

/// A request's cues and decision boundaries, separate from what stored knowledge helps do.
/// For example, task=implement can be requested while task=design knowledge is needed.
struct request_rule {
    std::vector<std::string> words;
    std::string give_if;
    std::string not_if;
};

struct tag_info {
    std::string qualified;  // "purpose=defensive"
    std::string value;      // "defensive"
    dimension_id dimension;
    codebook_entry entry;
    std::optional<source_ref> source;
    bool retired = false;
    std::string retired_reason;
    std::string see;  // the replacement, when a retirement or rejection names one
    bool any = false; // the reserved value of a hard dimension (04 §3.2)
    std::optional<request_rule> request;
};

struct rejection {
    std::string concept_name;
    std::string reason;
    std::string see;
    std::string pack;
};

/// The loaded vocabulary. Append-only, like the interners beneath it: a value
/// once added keeps its id for the life of the object (01, tag stability).
template <class Interner = mapping::hashed_interner>
class basic_taxonomy {
public:
    static constexpr std::string_view any_value = "any";

    /// Adds a dimension; nullopt when the name is taken.
    std::optional<dimension_id> add_dimension(dimension_info d) {
        if (m_dimension_names.find(d.name) != Interner::npos) return std::nullopt;
        const auto id = m_dimension_names.intern(d.name);
        m_dimensions.push_back(std::move(d));
        return dimension_id(static_cast<dimension_id::value_type>(id));
    }

    /// Adds `value` to dimension `d`; nullopt when the qualified name is taken.
    std::optional<tag_id> add_value(dimension_id d, std::string value, codebook_entry entry,
                                    std::optional<source_ref> source = std::nullopt,
                                    std::optional<request_rule> request = std::nullopt) {
        std::string qualified = m_dimensions[d.value()].name + "=" + value;
        if (m_tag_names.find(qualified) != Interner::npos) return std::nullopt;
        const auto id = m_tag_names.intern(qualified);
        tag_info info;
        info.qualified = std::move(qualified);
        info.value = std::move(value);
        info.dimension = d;
        info.entry = std::move(entry);
        info.source = std::move(source);
        info.request = std::move(request);
        m_tags.push_back(std::move(info));
        return tag_id(static_cast<tag_id::value_type>(id));
    }

    /// Interns the reserved `any` value of a hard dimension, with a generated
    /// entry (04 §3.2). Idempotent.
    tag_id add_any(dimension_id d) {
        const std::string& dim = m_dimensions[d.value()].name;
        if (auto t = tag(dim + "=" + std::string(any_value))) return *t;
        codebook_entry e{
            "Applies to every value of " + dim + ".",
            "The knowledge holds whatever the answer to the " + dim +
                " question is, including values not yet in the vocabulary.",
            "The knowledge is true for every " + dim + " without exception.",
            "Not when it holds for most " + dim + " values but not all - tag those values instead.",
            "A rule that holds for every " + dim + " carries " + dim + "=any."};
        auto t = *add_value(d, std::string(any_value), std::move(e));
        m_tags[t.value()].any = true;
        return t;
    }

    void retire(tag_id t, std::string reason, std::string see) {
        m_tags[t.value()].retired = true;
        m_tags[t.value()].retired_reason = std::move(reason);
        m_tags[t.value()].see = std::move(see);
    }

    void add_rejection(rejection r) { m_rejections.push_back(std::move(r)); }

    // ── lookups ───────────────────────────────────────────────────────────

    [[nodiscard]] std::optional<dimension_id> dimension(std::string_view name) const {
        const auto id = m_dimension_names.find(name);
        if (id == Interner::npos) return std::nullopt;
        return dimension_id(static_cast<dimension_id::value_type>(id));
    }
    [[nodiscard]] std::optional<tag_id> tag(std::string_view qualified) const {
        const auto id = m_tag_names.find(qualified);
        if (id == Interner::npos) return std::nullopt;
        return tag_id(static_cast<tag_id::value_type>(id));
    }
    [[nodiscard]] const dimension_info& info(dimension_id d) const { return m_dimensions[d.value()]; }
    [[nodiscard]] const tag_info& info(tag_id t) const { return m_tags[t.value()]; }
    [[nodiscard]] dimension_id dimension_of(tag_id t) const { return m_tags[t.value()].dimension; }
    [[nodiscard]] weight_t weight(dimension_id d) const { return m_dimensions[d.value()].weight; }
    [[nodiscard]] role default_role(dimension_id d) const { return m_dimensions[d.value()].default_role; }
    [[nodiscard]] std::size_t dimensions() const noexcept { return m_dimensions.size(); }
    [[nodiscard]] std::size_t tags() const noexcept { return m_tags.size(); }
    [[nodiscard]] const std::vector<rejection>& rejections() const noexcept { return m_rejections; }

    /// Render only first-round dimensions, omitting retired and universal values. Visits
    /// the tags once per selected dimension. Missing request rules are labelled explicitly.
    /// A task=design entry therefore contributes its cues and questions, never kind=principle.
    [[nodiscard]] std::string request_vocabulary(std::string_view dimension = {}) const {
        std::string body;
        for (std::size_t i = 0; i < dimensions(); ++i) {
            const dimension_id d(static_cast<dimension_id::value_type>(i));
            const auto& di = info(d);
            if (!di.request || (!dimension.empty() && di.name != dimension)) continue;
            body += "\n## " + di.name + " — " + di.entry.brief + "\n";
            for (const auto t : values_of(d)) {
                const auto& ti = info(t);
                if (ti.retired || ti.any) continue;
                body += "- `" + ti.qualified + "` — " + ti.entry.brief;
                if (ti.request) {
                    if (!ti.request->words.empty()) {
                        body += " Words: ";
                        for (std::size_t j = 0; j < ti.request->words.size(); ++j) {
                            if (j) body += ", ";
                            body += ti.request->words[j];
                        }
                        body += ".";
                    }
                    body += "\n  Give it if: " + ti.request->give_if + " Not if: " + ti.request->not_if + "\n";
                } else {
                    body += "\n  No separate request rule. Memory codebook — When: " + ti.entry.when +
                        " Not: " + ti.entry.when_not + "\n";
                }
            }
        }
        if (body.empty()) return "No request dimensions are configured for this selection. "
            "Use the store's memory codebook; request rules require an explicit vocabulary update.\n";
        return "Classify the current request using its conversation context. Select every supported value in each "
            "dimension, including several when justified. Cue words suggest candidates; apply the decision questions "
            "before selecting them. Do not guess unknown subjects or locations. Activities describe what is requested "
            "now; knowledge needed to carry them out is a separate retrieval concern.\n\n"
            "Every read needs at least one hard tag. For broad supporting knowledge, activity tags can be soft so "
            "other activities' knowledge remains eligible. "
            "For the dedicated procedure slot, read with exactly one hard task and one hard artifact; use a separate "
            "read for each activity whose procedure is needed. Use known domain tags to scope reads. Tag stored memories "
            "by the activities their knowledge supports, including several when justified.\n" + body;
    }

    /// The `any` value of dimension `d`, if it has one.
    [[nodiscard]] std::optional<tag_id> any_of(dimension_id d) const {
        return tag(m_dimensions[d.value()].name + "=" + std::string(any_value));
    }

    /// Every value of `d`, in load order.
    [[nodiscard]] std::vector<tag_id> values_of(dimension_id d) const {
        std::vector<tag_id> out;
        for (std::size_t i = 0; i < m_tags.size(); ++i)
            if (m_tags[i].dimension == d) out.push_back(tag_id(static_cast<tag_id::value_type>(i)));
        return out;
    }

    /// Whether `d` offers nothing a query could name: no live value but `any`. A vocabulary
    /// starts this way and a store may stay this way — the global store's `domain` has only
    /// `any`, because nothing in it is about one project. Refusing `any` in a query then leaves
    /// that dimension unqueryable, so the refusal lifts exactly here (04 §3.2).
    [[nodiscard]] bool only_any(dimension_id d) const {
        for (const auto t : values_of(d))
            if (!m_tags[t.value()].any && !m_tags[t.value()].retired) return false;
        return true;
    }

    /// The hard-by-default dimensions a memory in `domains` must answer: every
    /// base one, and those of the packs named for its domains.
    [[nodiscard]] std::vector<dimension_id> hard_dimensions_for(const std::vector<std::string>& domains) const {
        std::vector<dimension_id> out;
        for (std::size_t i = 0; i < m_dimensions.size(); ++i) {
            const auto& d = m_dimensions[i];
            if (d.default_role != role::hard) continue;
            if (d.pack == "base" || std::find(domains.begin(), domains.end(), d.pack) != domains.end())
                out.push_back(dimension_id(static_cast<dimension_id::value_type>(i)));
        }
        return out;
    }

    /// Up to five qualified names near `qualified`, for a refusal to offer:
    /// the values of the same dimension, or the dimensions when that is unknown.
    [[nodiscard]] std::vector<std::string> suggestions(std::string_view qualified) const {
        std::vector<std::string> out;
        const auto eq = qualified.find('=');
        if (eq != std::string_view::npos) {
            if (auto d = dimension(qualified.substr(0, eq))) {
                for (auto t : values_of(*d)) {
                    if (m_tags[t.value()].retired) continue;
                    out.push_back(m_tags[t.value()].qualified);
                    if (out.size() == 5) break;
                }
                return out;
            }
        }
        for (const auto& d : m_dimensions) {
            out.push_back(d.name);
            if (out.size() == 5) break;
        }
        return out;
    }

    /// The vocabulary's identity: a digest over its normalised content in load
    /// order (02 §2.4). Comments and formatting never reach it.
    [[nodiscard]] digest version() const {
        std::string enc = "pygim-taxonomy-1";
        auto entry = [&](const codebook_entry& e) {
            encode_field(enc, e.brief);
            encode_field(enc, e.full);
            encode_field(enc, e.when);
            encode_field(enc, e.when_not);
            encode_field(enc, e.example);
        };
        for (const auto& d : m_dimensions) {
            encode_field(enc, d.name);
            encode_field(enc, role_name(d.default_role));
            encode_field(enc, weight_text(d.weight));
            encode_field(enc, d.pack);
            entry(d.entry);
        }
        for (const auto& t : m_tags) {
            encode_field(enc, t.qualified);
            entry(t.entry);
            encode_field(enc, t.source ? t.source->doc + ":" + decimal(t.source->line) + "+" +
                                             decimal(t.source->lines) + "#" + t.source->passage
                                       : std::string());
            encode_field(enc, t.retired ? "retired:" + t.retired_reason + ">" + t.see : std::string());
        }
        for (const auto& r : m_rejections) {
            encode_field(enc, r.concept_name);
            encode_field(enc, r.reason);
            encode_field(enc, r.see);
            encode_field(enc, r.pack);
        }
        // No suffix for old vocabularies: their frozen receipt identities remain valid.
        for (const auto& d : m_dimensions) {
            if (!d.request) continue;
            encode_field(enc, "request-dimension");
            encode_field(enc, d.name);
        }
        for (const auto& t : m_tags) {
            if (!t.request) continue;
            encode_field(enc, "request-value");
            encode_field(enc, t.qualified);
            encode_field(enc, decimal(t.request->words.size()));
            for (const auto& w : t.request->words) encode_field(enc, w);
            encode_field(enc, t.request->give_if);
            encode_field(enc, t.request->not_if);
        }
        return digest::of(enc);
    }

private:
    Interner m_dimension_names;
    Interner m_tag_names;
    std::vector<dimension_info> m_dimensions;
    std::vector<tag_info> m_tags;
    std::vector<rejection> m_rejections;
};

using taxonomy = basic_taxonomy<>;

}  // namespace pygim::enact
