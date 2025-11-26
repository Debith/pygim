// (Always-on) MSSQL strategy header with optional ODBC detection.
// If system ODBC headers (sql.h, sqlext.h) are not available the class still
// compiles but acts as a stub that raises at runtime. This removes the need for
// an external enabling macro (previously PYGIM_ENABLE_MSSQL) while keeping
// build portability.
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// Header presence feature detection. Both <sql.h> and <sqlext.h> are required
// for a usable ODBC based implementation. Allow build system to override via
// -DPYGIM_HAVE_ODBC.
#ifndef PYGIM_HAVE_ODBC
#  if __has_include(<sql.h>) && __has_include(<sqlext.h>)
#    define PYGIM_HAVE_ODBC 1
#  else
#    define PYGIM_HAVE_ODBC 0
#  endif
#endif

#if PYGIM_HAVE_ODBC
#  include <sql.h>
#  include <sqlext.h>
#  ifdef BOOL
#    undef BOOL
#  endif
#  ifdef INT
#    undef INT
#  endif
#  if !defined(_WIN32) && !defined(_WIN64)
#    ifndef SQL_COPT_SS_BCP
#      define SQL_COPT_SS_BASE 1200
#      define SQL_COPT_SS_BCP (SQL_COPT_SS_BASE + 19)
#      define SQL_BCP_ON ((SQLULEN)1L)
#      define SQL_BCP_OFF ((SQLULEN)0L)
#    endif
#  endif
#endif

namespace pygim {
namespace py = pybind11;

class MssqlStrategyNative {
public:
    explicit MssqlStrategyNative(std::string conn);
    ~MssqlStrategyNative();
    py::object fetch(const py::object& key);
    void save(const py::object& key, const py::object& value);
    void bulk_insert(const std::string& table,
                     const std::vector<std::string>& columns,
                     const py::object& rows,
                     int batch_size = 1000,
                     const std::string& table_hint = "TABLOCK");
    void bulk_upsert(const std::string& table,
                     const std::vector<std::string>& columns,
                     const py::object& rows,
                     const std::string& key_column = "id",
                     int batch_size = 1000,
                     const std::string& table_hint = "TABLOCK");
    void bulk_insert_arrow_bcp(const std::string& table,
                               const py::bytes& arrow_ipc_bytes,
                               int batch_size = 100000,
                               const std::string& table_hint = "TABLOCK");
    std::string repr() const;
    static void raise_if_error(SQLRETURN, SQLSMALLINT, SQLHANDLE, const char*);
#if PYGIM_HAVE_ODBC
    SQLHDBC connection_handle() const noexcept { return m_dbc; }
#endif
private:
    std::string m_conn_str;
#if PYGIM_HAVE_ODBC
    SQLHENV m_env {SQL_NULL_HENV};
    SQLHDBC m_dbc {SQL_NULL_HDBC};
    bool m_connected {false};
#  if defined(PYGIM_HAVE_ARROW) && PYGIM_HAVE_ARROW
    bool m_bcp_attr_enabled {false};
#  endif
    void init_handles();
    void cleanup_handles();
    void ensure_connected();
    static bool is_valid_identifier(const std::string& s);
    py::object fetch_impl(const std::string& table, const py::object& pk);
    void upsert_impl(const std::string& table, const py::object& pk, const py::object& value_mapping);
    py::object execute_query_object(const py::object& query_obj);
    void upsert_polars_impl(const std::string& table,
                            const std::vector<std::string>& columns,
                            const py::object& df,
                            const std::string& key_column,
                            int batch_size,
                            const std::string& table_hint);
#endif
};
} // namespace pygim
