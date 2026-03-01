#pragma once
// Standalone ODBC error-handling utility.
// Free function so BCP and strategy code can share without class coupling.

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

namespace pygim::odbc {

/// Throw a descriptive std::runtime_error when an ODBC call fails.
/// No-op if ret indicates success.
inline void raise_if_error(SQLRETURN ret, SQLSMALLINT type,
                           SQLHANDLE handle, const char *what) {
    if (SQL_SUCCEEDED(ret)) return;
    SQLCHAR state[6];
    SQLINTEGER native;
    SQLCHAR msg[256];
    SQLSMALLINT len;
    if (SQLGetDiagRec(type, handle, 1, state, &native, msg, sizeof(msg), &len)
        == SQL_SUCCESS) {
        std::string state_str(reinterpret_cast<const char *>(state));
        std::string msg_str(reinterpret_cast<const char *>(msg));
        throw std::runtime_error(
            std::string(what) + " failed: [" + state_str + "] " + msg_str);
    }
    throw std::runtime_error(std::string(what) + " failed (no diagnostics)");
}

} // namespace pygim::odbc
