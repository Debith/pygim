#pragma once
// BCP API: ODBC BCP function pointer loading for SQL Server bulk operations.
// Thread-safe one-shot loader via function-local static (C++11+ guarantee).

#include <cstdint>
#include <stdexcept>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#include <msodbcsql.h>
#else
#include <dlfcn.h>
#include <sql.h>
#include <sqlext.h>

// BCP-specific constants not provided by unixODBC headers.
#ifndef SQL_COPT_SS_BCP
#define SQL_COPT_SS_BASE  1200
#define SQL_COPT_SS_BCP   (SQL_COPT_SS_BASE + 19)
#define SQL_BCP_ON        ((SQLULEN)1L)
#define SQL_BCP_OFF       ((SQLULEN)0L)
#endif

#ifndef DB_IN
#define DB_IN 1
#endif

// Type aliases used by BCP (not in unixODBC headers on Linux).
#ifndef PYGIM_BCP_TYPES_DEFINED
#define PYGIM_BCP_TYPES_DEFINED
using DBINT   = int;
using BYTE    = unsigned char;
using LPCBYTE = BYTE*;
#ifndef LPCWSTR
using LPCWSTR = const SQLWCHAR*;
#endif
#endif

#endif // !Windows

namespace pygim::bcp {

// ── BCP type tokens ─────────────────────────────────────────────────────────
// These are integer tokens passed to bcp_bind(), NOT C type aliases.
// Namespaced to avoid collision with ODBC typedefs of the same name.
namespace sql_type {
    inline constexpr int bigint     = 0x7f;
    inline constexpr int int4       = 0x38;
    inline constexpr int bit        = 0x32;
    inline constexpr int flt8       = 0x3e;
    inline constexpr int character  = 0x2f;
    inline constexpr int varchar    = 0x27;
    inline constexpr int nchar      = 0xef;
    inline constexpr int daten      = 0x28;
    inline constexpr int datetime2n = 0x2a;
    inline constexpr int varlen_data = -10;
} // namespace sql_type

inline constexpr int kSucceed  = 1;
inline constexpr int kFail     = 0;
inline constexpr int kBcpHints = 6;

// ── Function pointer types ──────────────────────────────────────────────────
using bcp_initW_fn   = RETCODE(*)(SQLHDBC, LPCWSTR, LPCWSTR, LPCWSTR, int);
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
        return init && bind && sendrow && batch && done && collen && colptr;
    }
};

// ── Loader ──────────────────────────────────────────────────────────────────

/// Thread-safe one-shot loader. Returns a const ref valid for program lifetime.
inline const BcpApi& ensure_bcp_api() {
    static const BcpApi api = [] {
        BcpApi a;
#if !defined(_WIN32) && !defined(_WIN64)
        void* handle = dlopen(
            "/opt/microsoft/msodbcsql18/lib64/libmsodbcsql-18.5.so.1.1",
            RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            a.init    = reinterpret_cast<bcp_initW_fn>(dlsym(handle, "bcp_initW"));
            a.bind    = reinterpret_cast<bcp_bind_fn>(dlsym(handle, "bcp_bind"));
            a.sendrow = reinterpret_cast<bcp_sendrow_fn>(dlsym(handle, "bcp_sendrow"));
            a.batch   = reinterpret_cast<bcp_batch_fn>(dlsym(handle, "bcp_batch"));
            a.done    = reinterpret_cast<bcp_done_fn>(dlsym(handle, "bcp_done"));
            a.collen  = reinterpret_cast<bcp_collen_fn>(dlsym(handle, "bcp_collen"));
            a.colptr  = reinterpret_cast<bcp_colptr_fn>(dlsym(handle, "bcp_colptr"));
            a.control = reinterpret_cast<bcp_control_fn>(dlsym(handle, "bcp_control"));
        }
#endif
        return a;
    }();

    if (!api.loaded()) [[unlikely]] {
        throw std::runtime_error(
            "BCP functions not available. SQL Server ODBC Driver 17/18+ required.\n"
            "Install: sudo ACCEPT_EULA=Y apt-get install -y msodbcsql18");
    }
    return api;
}

} // namespace pygim::bcp
