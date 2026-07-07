// persistence/strategy/mssql/upsert.h
// Keyed-write SQL rendering: BCP staging table + MERGE / anti-join INSERT.
//
// The keyed save flow (save_impl.h::execute_keyed) is:
//   1. create #staging with the frame's columns, typed from the target
//   2. BCP the frame into #staging (fast path, same connection)
//   3. MERGE target USING #staging  (upsert)
//      or INSERT ... WHERE NOT EXISTS (insert_missing)
//   4. drop #staging
// Everything here is pure string rendering + validation; execution lives in
// save_impl.h.

#pragma once

#include "dialect.h"
#include "sql_helpers.h"

#include <atomic>
#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace pygim::strategy::mssql::upsert {

/// Validate frame columns and merge keys before any SQL is rendered.
/// All names must be safe identifiers; keys must be a non-empty subset of
/// the frame's columns. Throws std::runtime_error naming every offender.
inline void validate_columns(const std::vector<std::string>& columns,
                             const std::vector<std::string>& keys) {
    std::string bad;
    for (const auto& c : columns) {
        if (!sql::is_valid_identifier(c)) bad += (bad.empty() ? "" : ", ") + c;
    }
    if (!bad.empty())
        throw std::runtime_error("Keyed save: column names are not valid SQL identifiers: " + bad);

    if (keys.empty())
        throw std::runtime_error("Keyed save requires at least one key column");

    std::string missing;
    for (const auto& k : keys) {
        if (!sql::is_valid_identifier(k)) {
            throw std::runtime_error("Keyed save: key is not a valid SQL identifier: " + k);
        }
        if (std::ranges::find(columns, k) == columns.end()) {
            missing += (missing.empty() ? "" : ", ") + k;
        }
    }
    if (!missing.empty())
        throw std::runtime_error("Keyed save: key column(s) not present in the frame: " + missing);
}

/// Unique local-temp staging name per call (connection-scoped anyway; the
/// counter guards against reuse on the same pooled connection).
[[nodiscard]] inline std::string next_stage_name() {
    static std::atomic<std::uint64_t> counter{0};
    return std::format("#pygim_stage_{}", counter.fetch_add(1, std::memory_order_relaxed));
}

/// SELECT TOP(0) ... INTO #stage FROM target: copies the frame-relevant
/// column types/nullability from the target. ISNULL(c, c) preserves the
/// type and nullability but strips IDENTITY, so BCP can insert explicit
/// key values into the staging table.
[[nodiscard]] inline std::string render_create_stage(const std::string& stage,
                                                     const std::string& target_quoted,
                                                     const std::vector<std::string>& columns,
                                                     const MssqlDialect& d) {
    std::string cols;
    for (const auto& c : columns) {
        auto q = d.quote_identifier(c);
        if (!cols.empty()) cols += ", ";
        cols += std::format("ISNULL({0}, {0}) AS {0}", q);
    }
    return std::format("SELECT TOP(0) {} INTO {} FROM {};", cols, stage, target_quoted);
}

/// Fail-fast guard executed before the MERGE / anti-join INSERT: duplicate
/// merge-key values in the source frame would otherwise either error late
/// (matched duplicates, 8672) or insert silently (new-key duplicates,
/// including every insert_missing case) — the exact corruption keyed writes
/// exist to prevent. One cheap set-based check catches all of them.
[[nodiscard]] inline std::string render_duplicate_guard(const std::string& stage,
                                                        const std::vector<std::string>& keys,
                                                        const MssqlDialect& d) {
    std::string key_cols;
    for (const auto& k : keys) {
        if (!key_cols.empty()) key_cols += ", ";
        key_cols += d.quote_identifier(k);
    }
    return std::format(
        "IF EXISTS (SELECT 1 FROM {} GROUP BY {} HAVING COUNT(*) > 1) "
        "THROW 50000, 'pygim keyed save: source frame contains duplicate merge-key values', 1;",
        stage, key_cols);
}

/// MERGE upsert: update non-key columns on key match, insert otherwise.
/// HOLDLOCK serializes concurrent merges on the key range (the standard
/// guard against the classic MERGE race; concurrent upserts of the same
/// key range block or deadlock rather than duplicate). `skip_update_col`
/// excludes a non-updatable column (the target's IDENTITY) from the SET
/// list.
[[nodiscard]] inline std::string render_merge(const std::string& target_quoted,
                                              const std::string& stage,
                                              const std::vector<std::string>& columns,
                                              const std::vector<std::string>& keys,
                                              const MssqlDialect& d,
                                              const std::string& skip_update_col = {}) {
    auto is_key = [&](const std::string& c) {
        return std::ranges::find(keys, c) != keys.end() || c == skip_update_col;
    };

    std::string on;
    for (const auto& k : keys) {
        auto q = d.quote_identifier(k);
        if (!on.empty()) on += " AND ";
        on += std::format("t.{0} = s.{0}", q);
    }

    std::string set;
    for (const auto& c : columns) {
        if (is_key(c)) continue;
        auto q = d.quote_identifier(c);
        if (!set.empty()) set += ", ";
        set += std::format("{0} = s.{0}", q);
    }

    std::string insert_cols, insert_vals;
    for (const auto& c : columns) {
        auto q = d.quote_identifier(c);
        if (!insert_cols.empty()) { insert_cols += ", "; insert_vals += ", "; }
        insert_cols += q;
        insert_vals += "s." + q;
    }

    std::string sql = std::format(
        "MERGE {} WITH (HOLDLOCK) AS t USING {} AS s ON {}", target_quoted, stage, on);
    if (!set.empty()) {
        sql += std::format(" WHEN MATCHED THEN UPDATE SET {}", set);
    }
    sql += std::format(" WHEN NOT MATCHED BY TARGET THEN INSERT ({}) VALUES ({});",
                       insert_cols, insert_vals);
    return sql;
}

/// Anti-join insert: add rows whose key is absent from the target, leave
/// existing rows untouched. UPDLOCK+HOLDLOCK on the existence probe keeps
/// concurrent inserters from racing in duplicates.
[[nodiscard]] inline std::string render_insert_missing(const std::string& target_quoted,
                                                       const std::string& stage,
                                                       const std::vector<std::string>& columns,
                                                       const std::vector<std::string>& keys,
                                                       const MssqlDialect& d) {
    std::string on;
    for (const auto& k : keys) {
        auto q = d.quote_identifier(k);
        if (!on.empty()) on += " AND ";
        on += std::format("t.{0} = s.{0}", q);
    }

    std::string cols, vals;
    for (const auto& c : columns) {
        auto q = d.quote_identifier(c);
        if (!cols.empty()) { cols += ", "; vals += ", "; }
        cols += q;
        vals += "s." + q;
    }

    return std::format(
        "INSERT INTO {0} ({1}) SELECT {2} FROM {3} AS s "
        "WHERE NOT EXISTS (SELECT 1 FROM {0} AS t WITH (UPDLOCK, HOLDLOCK) WHERE {4});",
        target_quoted, cols, vals, stage, on);
}

} // namespace pygim::strategy::mssql::upsert
