// memory/core/knowledge.h — memories, what is observed about them, and what
// needs a human's eye.
//
// A memory's content record never changes (01, Decision 4.1); what moves —
// its tags, whether it is a head — lives in the snapshot (snapshot.h).
// Pybind-free; no I/O.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ids.h"
#include "taxonomy.h"

namespace pygim::memory {

/// The content record of one memory, as the row that created it wrote it.
struct memory_record {
    row_id key;                      // the creating row's id (03 §2.2)
    digest content;                  // the text's digest; the text lives in an object
    std::string slug;                // a chain's file name, stable across its versions
    std::string title;
    std::uint32_t tokens = 1;        // content bytes / 4, at least 1 (04 §3.7)
    std::string origin;              // written, corrected, merged, seed
    std::string created;             // the row's time
    std::string author;
    std::uint64_t session = 0;
    std::uint32_t turn = 0;
    std::string reason;
    std::vector<row_id> supersedes;
    std::vector<row_id> generalises;  // the instances whose shared pattern this states (overview §4.11)
    std::vector<row_id> seen;
    std::vector<std::string> cites;
    std::string corpus;
    std::string revision;
};

[[nodiscard]] constexpr std::uint32_t token_estimate(std::size_t bytes) noexcept {
    const std::size_t t = bytes / 4;
    return t == 0 ? 1u : static_cast<std::uint32_t>(t);
}

/// One observation (01 §4): a memory admitted as a candidate, included in a
/// context, reported useful (optionally for one tag), or a procedure whose
/// steps held.
struct usage_record {
    row_id memory;
    std::string tag;                 // qualified name; empty for the whole memory
    std::string kind;                // admitted, included, useful, steps_held
    std::string time;
    std::uint64_t session = 0;
    std::string reason;
};

/// The folded view of usage records for one memory; always rebuildable.
struct counters {
    std::uint32_t admitted = 0;
    std::uint32_t included = 0;
    std::uint32_t useful = 0;
};

/// A proposal waiting for a human (02 §3.1), folded across the writes that
/// asked for it.
struct pending_proposal {
    std::string concept_name;
    std::string dimension;
    codebook_entry entry;
    std::vector<row_id> asked_by;

    /// The folding key: lowercase, spaces as underscores, and the dimension.
    [[nodiscard]] static std::string key(std::string_view concept_name, std::string_view dimension) {
        std::string k;
        for (const char c : concept_name) k.push_back(c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        k.push_back('|');
        k.append(dimension);
        return k;
    }
};

/// Something the service noticed that needs judgement it does not have: a
/// forked chain, a hand edit, a row naming a tag that no longer exists.
struct review_item {
    std::string kind;
    std::string text;
};

}  // namespace pygim::memory
