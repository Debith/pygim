#pragma once
// Lightweight SQL identifier helpers.
// BCP session setup and table qualification.

#include "../../core/persistence_error.h"
#include "../../../utils/chars.h"
#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pygim::strategy::mssql::sql {

/// Return true when s is a safe, unquoted SQL identifier
/// (ASCII letters, digits, underscore; does not start with digit).
/// Found 2026-10-31: std::isalnum asked the locale, so under a Latin-1 one
/// the UTF-8 bytes of "t\xc3\xaate" passed as letters — and reached
/// bcp_initW widened byte by byte. ASCII only, whatever the locale.
[[nodiscard]] constexpr bool is_valid_identifier(std::string_view s) noexcept {
    if (s.empty() || chars::is_digit(s[0])) return false;
    return std::ranges::all_of(s, [](char c) { return chars::is_alnum(c) || c == '_'; });
}
static_assert(is_valid_identifier("orders") && is_valid_identifier("_t1") && !is_valid_identifier("1t") &&
              !is_valid_identifier("a-b") && !is_valid_identifier("") && !is_valid_identifier("t\xc3\xaate"));

/// Ensure table name is a valid identifier (possibly schema.table).
/// Returns "dbo.table" if no schema part is present.
/// Validates identifiers to prevent SQL injection — callers must route all
/// user-supplied table names through this function.
[[nodiscard]] inline std::string qualify_table(const std::string& table) {
    auto ok = [](const std::string& s) {
        if (is_valid_identifier(s)) return true;
        auto dot = s.find('.');
        if (dot == std::string::npos) return false;
        return !s.substr(0, dot).empty()
            && !s.substr(dot + 1).empty()
            && is_valid_identifier(s.substr(0, dot))
            && is_valid_identifier(s.substr(dot + 1));
    };
    if (!ok(table))
        throw pygim::core::persistence_error("Invalid table identifier: " + table);
    return (table.find('.') == std::string::npos) ? std::format("dbo.{}", table) : table;
}

} // namespace pygim::strategy::mssql::sql
