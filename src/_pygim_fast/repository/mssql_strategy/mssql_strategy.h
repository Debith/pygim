#pragma once
#include <string>
#include <vector>
#include <optional>
#include <sstream>
#ifdef PYGIM_ENABLE_MSSQL
#include <sql.h>
#include <sqlext.h>
#endif
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace pygim {
namespace py = pybind11;

class MssqlStrategyNative {
public:
    explicit MssqlStrategyNative(std::string conn) : m_conn_str(std::move(conn)) {
#ifdef PYGIM_ENABLE_MSSQL
        init_handles();
#endif
    }
    ~MssqlStrategyNative() {
#ifdef PYGIM_ENABLE_MSSQL
        cleanup_handles();
#endif
    }
    py::object fetch(const py::object& key);
    void save(const py::object& key, const py::object& value);
    std::string repr() const { return "MssqlStrategyNative(conn_str=***hidden***)"; }
private:
    std::string m_conn_str;
#ifdef PYGIM_ENABLE_MSSQL
    SQLHENV m_env {SQL_NULL_HENV};
    SQLHDBC m_dbc {SQL_NULL_HDBC};
    void init_handles();
    void cleanup_handles();
    void ensure_connected();
    static void raise_if_error(SQLRETURN, SQLSMALLINT, SQLHANDLE, const char*);
    static bool is_valid_identifier(const std::string& s);
    py::object fetch_impl(const std::string& table, const py::object& pk);
    void upsert_impl(const std::string& table, const py::object& pk, const py::object& value_mapping);
    py::object execute_query_object(const py::object& query_obj);
#endif
};
} // namespace pygim
