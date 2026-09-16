// memory/core/retrieval.h — what a read asks, what it returns, and the
// receipt it leaves (01 §7, 04 §3). Pybind-free; no I/O.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ids.h"

namespace pygim::memory {

/// A read as the caller writes it: tags by name.
struct query {
    std::vector<std::string> hard;
    std::vector<std::string> soft;
    std::uint32_t max_memories = 8;
    std::uint32_t budget = 0;  // tokens; 0 means unbounded
    std::string term;          // when set, only memories whose title or text contains it (ASCII case-insensitive)
};

/// The same read, resolved against one vocabulary.
struct resolved_query {
    std::vector<tag_id> hard;
    std::vector<tag_id> soft;
    std::uint32_t max_memories = 8;
    std::uint32_t budget = 0;
    std::optional<std::vector<std::uint32_t>> within;  // the term's matches among the candidates, when a term was given
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
    std::vector<memory_id> evidence;    // candidates this generalisation folds: listed, not placed (04 §3.7)
};

/// What the agent receives (04 §3.6–3.8).
struct context {
    std::optional<memory_id> procedure;
    std::string procedure_note;          // why the slot is empty, or that the pair has two
    std::vector<match> selected;
    std::vector<match> skipped;          // candidates ranked but not included
    std::vector<memory_id> procedure_evidence;  // candidates the procedure generalises, folded under it
    std::vector<std::pair<tag_id, std::uint32_t>> facets;          // tags among the candidates, and how many carry each
    std::vector<std::pair<std::string, std::uint32_t>> by_source;  // documents the candidates cite, and how many cite each
    std::uint32_t uncited = 0;           // candidates citing nothing
    std::uint32_t corpus = 0;
    std::uint32_t candidates = 0;        // what the hard tags admit
    std::optional<std::uint32_t> term_matched;  // of those, the ones a term kept
    std::uint32_t folded = 0;            // candidates listed under a generalisation instead of placed
    std::uint32_t budget_dropped = 0;    // of the skipped, those the budget had no room for
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
