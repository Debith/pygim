#pragma once
// Lightweight SQL identifier helpers.
// BCP session setup and table qualification.

#include <algorithm>
#include <stdexcept>
#include <string>

namespace pygim::sql {

/// Return true when s is a safe, unquoted SQL identifier
/// (letters, digits, underscore; does not start with digit).
inline bool is_valid_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        auto uc = static_cast<unsigned char>(c);
        return std::isalnum(uc) || c == '_';
    });
}

/// Ensure table name is a valid identifier (possibly schema.table).
/// Returns "dbo.table" if no schema part is present.
inline std::string qualify_table(const std::string &table) {
    auto ok = [](const std::string &s) {
        if (is_valid_identifier(s)) return true;
        auto dot = s.find('.');
        if (dot == std::string::npos) return false;
        return !s.substr(0, dot).empty()
            && !s.substr(dot + 1).empty()
            && is_valid_identifier(s.substr(0, dot))
            && is_valid_identifier(s.substr(dot + 1));
    };
    if (!ok(table))
        throw std::runtime_error("Invalid table identifier: " + table);
    return (table.find('.') == std::string::npos) ? "dbo." + table : table;
}

} // namespace pygim::sql
