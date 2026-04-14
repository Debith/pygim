#pragma once
// ODBC error-handling utility.
// Shared by BCP pipeline, backend, and connection pool.

#include <format>
#include <stdexcept>
#include <string>

#include <sql.h>
#include <sqlext.h>
// ODBC headers may define BOOL/INT macros that collide with C++ identifiers.
#ifdef BOOL
#  undef BOOL
#endif
#ifdef INT
#  undef INT
#endif

namespace pygim::strategy::mssql::odbc {

/// Collect all ODBC diagnostic records from a handle into a single string.
/// Returns empty string if no records are available.
[[nodiscard]] inline std::string collect_diagnostics(SQLSMALLINT type, SQLHANDLE handle) {
    std::string result;
    SQLCHAR state[6];
    SQLINTEGER native;
    SQLCHAR msg[512];
    SQLSMALLINT len;

    for (SQLSMALLINT rec = 1; rec <= 10; ++rec) {
        auto dr = SQLGetDiagRec(type, handle, rec, state, &native,
                                msg, sizeof(msg), &len);
        if (dr != SQL_SUCCESS && dr != SQL_SUCCESS_WITH_INFO)
            break;
        if (!result.empty()) result += "; ";
        result += "[";
        result += reinterpret_cast<const char*>(state);
        result += "] ";
        result += reinterpret_cast<const char*>(msg);
    }
    return result;
}

/// Throw a descriptive std::runtime_error when an ODBC call fails.
/// No-op if ret indicates success.
/// Collects all available diagnostic records (up to 10).
inline void raise_if_error(SQLRETURN ret, SQLSMALLINT type,
                           SQLHANDLE handle, const char* what) {
    if (SQL_SUCCEEDED(ret)) return;

    auto diag = collect_diagnostics(type, handle);
    if (!diag.empty()) {
        throw std::runtime_error(std::format("{} failed: {}", what, diag));
    }

    const char* code_hint = "";
    if (ret == SQL_ERROR)               code_hint = " (SQL_ERROR)";
    else if (ret == SQL_INVALID_HANDLE) code_hint = " (SQL_INVALID_HANDLE)";
    else if (ret == SQL_NO_DATA)        code_hint = " (SQL_NO_DATA)";
    else if (ret == SQL_NEED_DATA)      code_hint = " (SQL_NEED_DATA)";

    throw std::runtime_error(
        std::format("{} failed (no diagnostics{})", what, code_hint));
}

} // namespace pygim::strategy::mssql::odbc
