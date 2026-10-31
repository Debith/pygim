#pragma once
// BCP API: ODBC BCP function pointer loading for SQL Server bulk operations.
// Thread-safe one-shot loader via function-local static (C++11+ guarantee).

#include "../../../core/persistence_error.h"
#include <cstdint>
#include <stdexcept>
#include <string>

// Deliberately independent of the MSSQL driver's Client SDK (<msodbcsql.h>):
// that header is only present when the driver SDK is installed, so relying on
// it would make the build environment-dependent.  Instead the small set of BCP
// constants/types is declared here and the entry points are resolved from the
// driver library at runtime on every platform (platform/libraries.h: dlopen on
// POSIX, LoadLibrary on Windows).

#include <array>

#include "../../../odbc_headers.h"  // windows.h-before-sql.h on Windows
#include "../../../../platform/libraries.h"
#include "../../../../platform/native.h"

// BCP-specific constants not provided by the ODBC driver-manager headers.
#ifndef SQL_COPT_SS_BCP
#define SQL_COPT_SS_BASE  1200
#define SQL_COPT_SS_BCP   (SQL_COPT_SS_BASE + 19)
#define SQL_BCP_ON        ((SQLULEN)1L)
#define SQL_BCP_OFF       ((SQLULEN)0L)
#endif

#ifndef DB_IN
#define DB_IN 1
#endif

// Type aliases used by BCP.  On Windows BYTE already exists with this exact
// underlying type, and redeclaring an alias to the same type is well-formed;
// DBINT/LPCBYTE exist only in the driver SDK.
#ifndef PYGIM_BCP_TYPES_DEFINED
#define PYGIM_BCP_TYPES_DEFINED
using DBINT   = int;
using BYTE    = unsigned char;
using LPCBYTE = const BYTE*;
#endif

#include "../../../odbc_compat.h"

// ODBC headers may define BOOL/INT macros that collide with C++ identifiers.
#ifdef BOOL
#  undef BOOL
#endif
#ifdef INT
#  undef INT
#endif

namespace pygim::strategy::mssql::bcp {

// ── BCP type tokens ─────────────────────────────────────────────────────────
// Integer tokens passed to bcp_bind(), NOT C type aliases.
namespace sql_type {
    inline constexpr int int1       = 0x30;  // TINYINT
    inline constexpr int int2       = 0x34;  // SMALLINT
    inline constexpr int int4       = 0x38;  // INT
    inline constexpr int bigint     = 0x7f;  // BIGINT
    inline constexpr int bit        = 0x32;  // BIT
    inline constexpr int flt4       = 0x3b;  // REAL / FLOAT(24)
    inline constexpr int flt8       = 0x3e;  // FLOAT / FLOAT(53)
    inline constexpr int character  = 0x2f;  // NVARCHAR / NCHAR (string)
    inline constexpr int varchar    = 0x27;  // VARCHAR
    inline constexpr int nchar      = 0xef;  // NCHAR
    inline constexpr int daten      = 0x28;  // DATE
    inline constexpr int timen      = 0x29;  // TIME
    inline constexpr int datetime2n = 0x2a;  // DATETIME2
    inline constexpr int binary     = 0x2d;  // BINARY / VARBINARY
    inline constexpr int uniqueid   = 0x24;  // UNIQUEIDENTIFIER (16-byte GUID)
    inline constexpr int varlen_data = -10;  // variable-length marker
} // namespace sql_type

/// A name as bcp_initW takes it: UTF-16 (SQLWCHAR is 16 bits wide on every system's ODBC; on
/// Windows it is wchar_t, so this is the SDK's LPCWSTR).
using wide_text = const SQLWCHAR*;

// The driver's library as each system names it, newest first, and where POSIX installs its
// versioned files when the loader's search path has none.
template <platform::family F> struct driver_files;
template <> struct driver_files<platform::family::windows> {
    static constexpr std::array<const char*, 2> names{"msodbcsql18.dll", "msodbcsql17.dll"};
    static constexpr const char* installed = nullptr;
};
template <> struct driver_files<platform::family::posix> {
    static constexpr std::array<const char*, 1> names{"libmsodbcsql-18.so"};   // the SONAME: survives driver updates
    static constexpr const char* installed = "/opt/microsoft/msodbcsql*/lib64/libmsodbcsql-*.so.*";
};

inline constexpr int kSucceed  = 1;
inline constexpr int kFail     = 0;
inline constexpr int kBcpHints = 6;

// ── Function pointer types ──────────────────────────────────────────────────
using bcp_initW_fn   = RETCODE(*)(SQLHDBC, wide_text, wide_text, wide_text, int);
using bcp_bind_fn    = RETCODE(*)(SQLHDBC, LPCBYTE, int, DBINT, LPCBYTE, int, int, int);
using bcp_sendrow_fn = RETCODE(*)(SQLHDBC);
using bcp_batch_fn   = DBINT(*)(SQLHDBC);
using bcp_done_fn    = DBINT(*)(SQLHDBC);
using bcp_collen_fn  = RETCODE(*)(SQLHDBC, DBINT, int);
using bcp_colptr_fn  = RETCODE(*)(SQLHDBC, LPCBYTE, int);
using bcp_control_fn = RETCODE(*)(SQLHDBC, int, void*);

/// Immutable bundle of resolved BCP entry points.
struct BcpApi {
    bcp_initW_fn   init{};
    bcp_bind_fn    bind{};
    bcp_sendrow_fn sendrow{};
    bcp_batch_fn   batch{};
    bcp_done_fn    done{};
    bcp_collen_fn  collen{};
    bcp_colptr_fn  colptr{};
    bcp_control_fn control{};

    [[nodiscard]] constexpr bool loaded() const noexcept {
        return init && bind && sendrow && batch && done && collen && colptr
            && control;
    }
};

// ── Loader ──────────────────────────────────────────────────────────────────

/// Thread-safe one-shot loader. Returns a const ref valid for program lifetime.
[[nodiscard]] inline const BcpApi& ensure_bcp_api() {
    static const BcpApi api = [] {
        BcpApi a;
        using files = driver_files<platform::native>;
        platform::library lib = platform::library::first_of(files::names);
        if (!lib && files::installed != nullptr) lib = platform::library::last_match(files::installed);
        a.init    = lib.function<bcp_initW_fn>("bcp_initW");
        a.bind    = lib.function<bcp_bind_fn>("bcp_bind");
        a.sendrow = lib.function<bcp_sendrow_fn>("bcp_sendrow");
        a.batch   = lib.function<bcp_batch_fn>("bcp_batch");
        a.done    = lib.function<bcp_done_fn>("bcp_done");
        a.collen  = lib.function<bcp_collen_fn>("bcp_collen");
        a.colptr  = lib.function<bcp_colptr_fn>("bcp_colptr");
        a.control = lib.function<bcp_control_fn>("bcp_control");
        return a;
    }();

    if (!api.loaded()) [[unlikely]] {
        throw pygim::core::persistence_error(
            "BCP functions not available. SQL Server ODBC Driver 17/18+ required.\n"
            "Linux: sudo ACCEPT_EULA=Y apt-get install -y msodbcsql18\n"
            "Windows: install the Microsoft ODBC Driver 17/18 for SQL Server");
    }
    return api;
}

} // namespace pygim::strategy::mssql::bcp
