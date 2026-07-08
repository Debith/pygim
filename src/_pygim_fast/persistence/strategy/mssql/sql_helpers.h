#pragma once
// Lightweight SQL identifier helpers.
// BCP session setup and table qualification.

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>

namespace pygim::strategy::mssql::sql {

/// Return true when s is a safe, unquoted SQL identifier
/// (letters, digits, underscore; does not start with digit).
[[nodiscard]] inline bool is_valid_identifier(const std::string& s) {
    if (s.empty()) return false;
    if (std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    return std::ranges::all_of(s, [](char c) {
        auto uc = static_cast<unsigned char>(c);
        return std::isalnum(uc) || c == '_';
    });
}

/// Ensure table name is a valid identifier (possibly schema.table).
/// Returns "dbo.table" if no schema part is present.
/// Validates identifiers to prevent SQL injection — callers must route all
/// user-supplied table names through this function.
[[nodiscard]] inline std::string qualify_table(const std::string& table) {
    // Temp tables ("#t" / "##t"): strip the hash prefix, validate the
    // remainder, return as-is — they live in tempdb, not a user schema.
    if (!table.empty() && table.front() == '#') {
        std::string bare = table.substr(table.starts_with("##") ? 2 : 1);
        if (!is_valid_identifier(bare))
            throw std::runtime_error("Invalid table identifier: " + table);
        return table;
    }
    // 1-part → dbo-qualified; 2-part (schema.table) and 3-part
    // (database.schema.table) pass through after per-part validation.
    auto ok = [](const std::string& s) {
        std::size_t parts = 0;
        std::size_t start = 0;
        while (true) {
            auto dot = s.find('.', start);
            const auto part = (dot == std::string::npos)
                ? s.substr(start) : s.substr(start, dot - start);
            if (!is_valid_identifier(part)) return false;
            if (++parts > 3) return false;
            if (dot == std::string::npos) return parts >= 1;
            start = dot + 1;
        }
    };
    if (!ok(table))
        throw std::runtime_error("Invalid table identifier: " + table);
    return (table.find('.') == std::string::npos) ? std::format("dbo.{}", table) : table;
}

} // namespace pygim::strategy::mssql::sql
