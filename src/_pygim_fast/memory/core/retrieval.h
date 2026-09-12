// memory/core/retrieval.h — what a read asks, what it returns, and the
// receipt it leaves (01 §7, 04 §3). Pybind-free; no I/O.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ids.h"

namespace pygim::memory {

/// A read as the caller writes it: tags by name.
struct query {
    std::vector<std::string> hard;
    std::vector<std::string> soft;
    std::uint32_t max_memories = 8;
    std::uint32_t budget = 0;  // tokens; 0 means unbounded
};

/// The same read, resolved against one vocabulary.
struct resolved_query {
    std::vector<tag_id> hard;
    std::vector<tag_id> soft;
    std::uint32_t max_memories = 8;
    std::uint32_t budget = 0;
};

/// One candidate, with everything its inclusion is explained by (G2).
struct match {
    memory_id id;
    std::vector<tag_id> hard_matched;   // includes an `any` value when that is what matched
    std::vector<tag_id> soft_matched;
    std::vector<tag_id> soft_missed;
    score_t soft_score = 0;             // milli-units
    score_t semantic_score = 0;         // micro-units; 0 with no ranker
    score_t final_score = 0;            // 10^-9 units (04 §3.3)
    std::uint32_t soft_hits = 0;
    std::uint32_t rank = 0;
    std::uint32_t tokens = 0;
};

/// What the agent receives (04 §3.6–3.8).
struct context {
    std::optional<memory_id> procedure;
    std::string procedure_note;          // why the slot is empty, or that the pair has two
    std::vector<match> selected;
    std::vector<match> skipped;          // candidates ranked but not included
    std::uint32_t corpus = 0;
    std::uint32_t candidates = 0;
    std::uint32_t tokens = 0;
    bool over_budget = false;
};

/// What a read depended on, so it can be rerun later (01 §3.1, 04 §5).
struct receipt {
    row_id snapshot;
    std::uint64_t version = 0;
    digest taxonomy;
    query asked;
    std::vector<row_id> keys;            // the procedure first, then the selected, in order
    std::string time;
    std::uint64_t session = 0;
};

}  // namespace pygim::memory
