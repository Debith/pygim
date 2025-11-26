#include "../mssql_strategy.h"
#include "helpers.h"

namespace pygim {

MssqlStrategyNative::MssqlStrategyNative(std::string conn) : m_conn_str(std::move(conn)) {
#if PYGIM_HAVE_ODBC
    init_handles();
#endif
}

MssqlStrategyNative::~MssqlStrategyNative() {
#if PYGIM_HAVE_ODBC
    cleanup_handles();
#endif
}

std::string MssqlStrategyNative::repr() const {
#if PYGIM_HAVE_ODBC
    return "MssqlStrategyNative(conn_str=***hidden***)";
#else
    return "MssqlStrategyNative(odbc_unavailable)";
#endif
}

#if PYGIM_HAVE_ODBC
void MssqlStrategyNative::init_handles() {
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_env) != SQL_SUCCESS) {
        throw std::runtime_error("ODBC: Failed to allocate env handle");
    }
    SQLSetEnvAttr(m_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (SQLAllocHandle(SQL_HANDLE_DBC, m_env, &m_dbc) != SQL_SUCCESS) {
        throw std::runtime_error("ODBC: Failed to allocate dbc handle");
    }
}

void MssqlStrategyNative::cleanup_handles() {
    if (m_dbc != SQL_NULL_HDBC) {
        SQLDisconnect(m_dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, m_dbc);
        m_dbc = SQL_NULL_HDBC;
    }
    if (m_env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, m_env);
        m_env = SQL_NULL_HENV;
    }
    m_connected = false;
#if defined(PYGIM_HAVE_ARROW) && PYGIM_HAVE_ARROW
    m_bcp_attr_enabled = false;
#endif
}

void MssqlStrategyNative::ensure_connected() {
    SQLRETURN ret;
    SQLSMALLINT outstrlen = 0;
    if (m_dbc == SQL_NULL_HDBC)
        throw std::runtime_error("ODBC: dbc handle null");
    if (m_connected)
        return;
#if defined(PYGIM_HAVE_ARROW) && PYGIM_HAVE_ARROW
    if (!m_bcp_attr_enabled) {
        SQLRETURN attr_ret = SQLSetConnectAttr(m_dbc, SQL_COPT_SS_BCP, (SQLPOINTER)SQL_BCP_ON, SQL_IS_UINTEGER);
        if (!SQL_SUCCEEDED(attr_ret)) {
            raise_if_error(attr_ret, SQL_HANDLE_DBC, m_dbc, "SQLSetConnectAttr(BCP_ON)");
        }
        m_bcp_attr_enabled = true;
    }
#endif
    ret = SQLDriverConnect(m_dbc, NULL, (SQLCHAR *)m_conn_str.c_str(), SQL_NTS, NULL, 0, &outstrlen, SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(ret))
        raise_if_error(ret, SQL_HANDLE_DBC, m_dbc, "SQLDriverConnect");
    m_connected = true;
}

void MssqlStrategyNative::raise_if_error(SQLRETURN ret, SQLSMALLINT type, SQLHANDLE handle, const char *what) {
    if (SQL_SUCCEEDED(ret))
        return;
    SQLCHAR state[6];
    SQLINTEGER native;
    SQLCHAR msg[256];
    SQLSMALLINT len;
    if (SQLGetDiagRec(type, handle, 1, state, &native, msg, sizeof(msg), &len) == SQL_SUCCESS) {
        std::string state_str(reinterpret_cast<const char *>(state));
        std::string msg_str(reinterpret_cast<const char *>(msg));
        std::string error = std::string(what) + " failed: [" + state_str + "] " + msg_str;
        throw std::runtime_error(error);
    }
    throw std::runtime_error(std::string(what) + " failed (no diagnostics)");
}

bool MssqlStrategyNative::is_valid_identifier(const std::string &s) {
    return detail::is_valid_identifier(s);
}
#endif

} // namespace pygim
