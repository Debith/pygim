// connection_uri.h — Parse URI scheme and create the appropriate Strategy.
//
// Supported schemes:
//   memory://              → MemoryStrategy
//   mssql://server/db      → MssqlStrategy (converted to ODBC connection string)
//
// Future:
//   postgres://server/db   → PostgresStrategy
//
// This is pure C++ (pybind-free). Lives in core because it returns
// core::Strategy, but references concrete strategy headers.
#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "memory_strategy.h"
#include "strategy.h"

namespace pygim::core {

/// Parsed components of a connection URI.
struct ConnectionUri {
    std::string scheme;   // "memory", "mssql", etc.
    std::string host;     // server (empty for memory)
    std::string database; // database name (empty for memory)
    std::string original; // original URI string
};

/// Parse a connection URI string into components.
///
/// Accepted formats:
///   "memory://"
///   "mssql://server/database"
///   "mssql://server"              (database omitted)
///   "Driver={...};Server=...;..." (raw ODBC — scheme inferred as "mssql")
inline ConnectionUri parse_uri(const std::string &uri) {
    ConnectionUri result;
    result.original = uri;

    // Raw ODBC connection string detection (contains '=' and no "://")
    if (uri.find("://") == std::string::npos && uri.find('=') != std::string::npos) {
        result.scheme = "mssql";
        // host/database not parsed — the raw string is passed through as-is.
        return result;
    }

    // URI scheme parsing
    auto scheme_end = uri.find("://");
    if (scheme_end == std::string::npos) {
        throw std::invalid_argument(
            "ConnectionUri: invalid URI '" + uri + "'. "
            "Expected scheme://... (e.g., memory://, mssql://server/db).");
    }

    result.scheme = uri.substr(0, scheme_end);

    // Everything after "://"
    std::string_view rest(uri.data() + scheme_end + 3, uri.size() - scheme_end - 3);

    // Strip trailing slashes
    while (!rest.empty() && rest.back() == '/') {
        rest.remove_suffix(1);
    }

    if (rest.empty()) {
        // scheme:// with nothing after — valid for memory://
        return result;
    }

    // Split on first '/' → host / database
    auto slash = rest.find('/');
    if (slash == std::string_view::npos) {
        result.host = std::string(rest);
    } else {
        result.host = std::string(rest.substr(0, slash));
        result.database = std::string(rest.substr(slash + 1));
    }

    return result;
}

/// Build an ODBC connection string from parsed URI components.
inline std::string build_odbc_connection_string(const ConnectionUri &uri) {
    // If the original was already a raw ODBC string, pass through.
    if (uri.host.empty() && uri.database.empty() && uri.scheme == "mssql") {
        return uri.original;
    }

    std::string conn = "Driver={ODBC Driver 18 for SQL Server};"
                       "Server=" + uri.host + ";";
    if (!uri.database.empty()) {
        conn += "Database=" + uri.database + ";";
    }
    conn += "TrustServerCertificate=yes;Encrypt=yes;";
    return conn;
}

} // namespace pygim::core
