// memory/core/provenance.h — the audit row, and what a write carries.
//
// The audit log is the index (03, Decision 1): every change to the index side
// is one row, and a row's id is the digest of its canonical encoding (03 §2.1),
// so rows form a mergeable hash chain. A row names tags by qualified name and
// memories by key — nothing on disk depends on load order (02 §1.3, 03 §2.2).
// Pybind-free; no I/O; the files strategy decides how a row looks on disk.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ids.h"
#include "taxonomy.h"

namespace pygim::memory {

/// The kinds of change. Each is one row.
namespace ops {
inline constexpr std::string_view taxonomy = "taxonomy";  // the vocabulary as it now stands
inline constexpr std::string_view write = "write";        // a memory is born
inline constexpr std::string_view link = "link";          // an association added
inline constexpr std::string_view unlink = "unlink";      // associations it had seen removed
inline constexpr std::string_view retire = "retire";      // a memory leaves retrieval
inline constexpr std::string_view accept = "accept";      // a human accepts a generalisation; its instances fold
inline constexpr std::string_view lessons = "lessons";    // what a consolidation learnt, for the human to review
inline constexpr std::string_view merge = "merge";        // two histories joined (03 §5)
}  // namespace ops

/// One audit row. `fields` is a multimap: a key may repeat (`tag`, `seen`),
/// and neither the order of fields nor of parents reaches the id, so a row
/// read back from any serialisation hashes to the id it was written with.
struct row {
    row_id id;
    std::vector<row_id> parents;
    std::string clone;
    std::uint64_t seq = 0;
    std::string time;
    std::string op;
    std::vector<std::pair<std::string, std::string>> fields;

    void add(std::string key, std::string value) { fields.emplace_back(std::move(key), std::move(value)); }

    /// The first value of `key`, or an empty view.
    [[nodiscard]] std::string_view get(std::string_view key) const {
        for (const auto& [k, v] : fields)
            if (k == key) return v;
        return {};
    }
    /// Every value of `key`, in the order they were added.
    [[nodiscard]] std::vector<std::string_view> all(std::string_view key) const {
        std::vector<std::string_view> out;
        for (const auto& [k, v] : fields)
            if (k == key) out.push_back(v);
        return out;
    }
};

/// The bytes a row's id is the digest of: a version tag, the parents sorted,
/// the clone, sequence, time and op, and the fields sorted by key then value
/// — each length-prefixed (ids.h, encode_field).
[[nodiscard]] inline std::string canonical(const row& r) {
    std::string enc = "pygim-row-1";
    std::vector<std::string> parents;
    parents.reserve(r.parents.size());
    for (const auto& p : r.parents) parents.push_back(p.hex());
    std::sort(parents.begin(), parents.end());
    encode_field(enc, decimal(parents.size()));
    for (const auto& p : parents) encode_field(enc, p);
    encode_field(enc, r.clone);
    encode_field(enc, decimal(r.seq));
    encode_field(enc, r.time);
    encode_field(enc, r.op);
    auto fields = r.fields;
    std::sort(fields.begin(), fields.end());
    encode_field(enc, decimal(fields.size()));
    for (const auto& [k, v] : fields) {
        encode_field(enc, k);
        encode_field(enc, v);
    }
    return enc;
}

[[nodiscard]] inline row_id compute_id(const row& r) { return digest::of(canonical(r)); }

/// Seals a row: computes and stores its id.
inline void seal(row& r) { r.id = compute_id(r); }

/// A concept the vocabulary lacks, proposed by a write (02 §3.1).
struct proposal_request {
    std::string concept_name;
    std::string dimension;  // empty: a new dimension, or unknown
    codebook_entry entry;
};

/// Everything a write carries (01 §6). Memories are named as the caller saw
/// them — `#12` or a key prefix — and resolved by the service.
struct write_request {
    std::string title;
    std::string text;
    std::string reason;
    std::vector<std::string> tags;        // qualified names
    std::vector<std::string> supersedes;  // empty: start a chain
    std::vector<std::string> generalises; // a generalisation's instances; they stay heads (overview §4.11)
    std::vector<std::string> seen;
    std::vector<std::string> cites;       // locators, kept as given
    std::vector<proposal_request> proposals;
    std::string origin = "written";       // written, corrected, merged, seed
    std::string slug;                     // ingestion names it; otherwise derived from the title
    std::string author = "agent";
    std::uint64_t session = 0;
    std::uint32_t turn = 0;
    std::string corpus;                   // ingestion only: the corpus file and its digest
    std::string revision;
};

}  // namespace pygim::memory
