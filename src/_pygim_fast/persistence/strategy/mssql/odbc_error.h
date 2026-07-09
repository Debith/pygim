#pragma once
// ODBC error-handling utility.
// Shared by BCP pipeline, backend, and connection pool.

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

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

/// Error classification for retry/skip policies at the data-access boundary
/// (design doc 2.5). Mapped from the SQLSTATE class of the first diagnostic
/// record, mirroring the DB-API taxonomy:
///   Integrity - constraint violations (duplicate key, FK); skip/dedup.
///   Transient - connection loss, timeouts, deadlock victim; retry.
///   Data      - value conversion/truncation/range problems in the data.
enum class ErrorKind { Generic, Integrity, Transient, Data };

[[nodiscard]] inline ErrorKind classify_error(std::string_view sqlstate,
                                              SQLINTEGER native,
                                              std::string_view message) {
    if (sqlstate.starts_with("23")) return ErrorKind::Integrity;              // constraint
    if (native == 2627 || native == 2601 || native == 547) return ErrorKind::Integrity;
    // pygim's own keyed-save duplicate guard raises via THROW 50000.
    if (native == 50000 && message.contains("duplicate merge-key")) return ErrorKind::Integrity;
    if (sqlstate.starts_with("08")) return ErrorKind::Transient;              // connection
    if (sqlstate == "40002") return ErrorKind::Integrity;                     // txn integrity constraint violation
    if (sqlstate == "40001" || sqlstate == "40003") return ErrorKind::Transient;  // deadlock / unknown completion
    if (sqlstate == "HYT00" || sqlstate == "HYT01") return ErrorKind::Transient;  // timeouts
    if (native == 1222) return ErrorKind::Transient;                          // lock request timeout
    if (sqlstate.starts_with("22")) return ErrorKind::Data;                   // data exception
    return ErrorKind::Generic;
}

/// ODBC failure carrying the primary diagnostic record's SQLSTATE and
/// native error code, so the Python boundary can raise a typed exception.
struct OdbcError : std::runtime_error {
    std::string sqlstate;   //!< primary record's 5-char SQLSTATE ("" if none)
    SQLINTEGER  native{0};  //!< primary record's native error code
    ErrorKind   kind{ErrorKind::Generic};

    OdbcError(const std::string& message, std::string state, SQLINTEGER native_code)
        : std::runtime_error(message)
        , sqlstate(std::move(state))
        , native(native_code)
        , kind(classify_error(sqlstate, native_code, message)) {}

    /// Explicit-kind constructor for failures the driver reports without
    /// diagnostics (e.g. BCP commit shortfalls, where the row-count return
    /// value is the only signal and the cause is constraint enforcement).
    OdbcError(const std::string& message, std::string state, SQLINTEGER native_code,
              ErrorKind explicit_kind)
        : std::runtime_error(message)
        , sqlstate(std::move(state))
        , native(native_code)
        , kind(explicit_kind) {}
};

/// Collect all ODBC diagnostic records from a handle into a single string.
/// Returns empty string if no records are available. When `first_state` /
/// `first_native` are given, they receive the primary record's fields.
[[nodiscard]] inline std::string collect_diagnostics(SQLSMALLINT type, SQLHANDLE handle,
                                                     std::string* first_state = nullptr,
                                                     SQLINTEGER* first_native = nullptr) {
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
        if (rec == 1) {
            if (first_state) *first_state = reinterpret_cast<const char*>(state);
            if (first_native) *first_native = native;
        }
        if (!result.empty()) result += "; ";
        result += "[";
        result += reinterpret_cast<const char*>(state);
        result += "] ";
        result += reinterpret_cast<const char*>(msg);
    }
    return result;
}

/// Throw a classified OdbcError when an ODBC call fails.
/// No-op if ret indicates success.
/// Collects all available diagnostic records (up to 10).
inline void raise_if_error(SQLRETURN ret, SQLSMALLINT type,
                           SQLHANDLE handle, const char* what) {
    if (SQL_SUCCEEDED(ret)) return;

    std::string first_state;
    SQLINTEGER first_native = 0;
    auto diag = collect_diagnostics(type, handle, &first_state, &first_native);
    if (!diag.empty()) {
        throw OdbcError(std::format("{} failed: {}", what, diag),
                        std::move(first_state), first_native);
    }

    const char* code_hint = "";
    if (ret == SQL_ERROR)               code_hint = " (SQL_ERROR)";
    else if (ret == SQL_INVALID_HANDLE) code_hint = " (SQL_INVALID_HANDLE)";
    else if (ret == SQL_NO_DATA)        code_hint = " (SQL_NO_DATA)";
    else if (ret == SQL_NEED_DATA)      code_hint = " (SQL_NEED_DATA)";

    throw OdbcError(std::format("{} failed (no diagnostics{})", what, code_hint),
                    "", 0);
}

} // namespace pygim::strategy::mssql::odbc
