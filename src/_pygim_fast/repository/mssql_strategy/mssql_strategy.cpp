#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "repository/mssql_strategy/mssql_strategy.h"

#ifdef PYGIM_ENABLE_MSSQL
#include <regex>
#include <stdexcept>
#include <iostream>
#endif

namespace pygim {
#ifdef PYGIM_ENABLE_MSSQL
namespace {
inline std::string extract_table(const pybind11::object& key) {
    if (pybind11::isinstance<pybind11::tuple>(key)) {
        auto t = key.cast<pybind11::tuple>();
        if (t.size() >= 1) return t[0].cast<std::string>();
    }
    throw std::runtime_error("MssqlStrategyNative: key must be a tuple(table, pk)");
}
inline pybind11::object extract_pk(const pybind11::object& key) {
    auto t = key.cast<pybind11::tuple>();
    if (t.size() < 2) throw std::runtime_error("MssqlStrategyNative: key missing pk value");
    return t[1];
}
}
#endif

py::object MssqlStrategyNative::fetch(const py::object& key) {
#ifndef PYGIM_ENABLE_MSSQL
    throw std::runtime_error("MSSQL native support not compiled (define PYGIM_ENABLE_MSSQL)");
#else
    ensure_connected();
    if (py::hasattr(key, "sql") && py::hasattr(key, "params")) {
        try { return execute_query_object(key); } catch(...) { return py::none(); }
    }
    std::string table = extract_table(key);
    py::object pk = extract_pk(key);
    if (!is_valid_identifier(table)) throw std::runtime_error("Invalid table identifier");
    try { return fetch_impl(table, pk); } catch (...) { return py::none(); }
#endif
}

void MssqlStrategyNative::save(const py::object& key, const py::object& value) {
#ifndef PYGIM_ENABLE_MSSQL
    throw std::runtime_error("MSSQL native support not compiled (define PYGIM_ENABLE_MSSQL)");
#else
    ensure_connected();
    std::string table = extract_table(key);
    py::object pk = extract_pk(key);
    if (!is_valid_identifier(table)) throw std::runtime_error("Invalid table identifier");
    upsert_impl(table, pk, value);
#endif
}

#ifdef PYGIM_ENABLE_MSSQL
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
    if (m_dbc != SQL_NULL_HDBC) { SQLDisconnect(m_dbc); SQLFreeHandle(SQL_HANDLE_DBC, m_dbc); m_dbc = SQL_NULL_HDBC; }
    if (m_env != SQL_NULL_HENV) { SQLFreeHandle(SQL_HANDLE_ENV, m_env); m_env = SQL_NULL_HENV; }
}
void MssqlStrategyNative::ensure_connected() {
    SQLRETURN ret; SQLSMALLINT outstrlen = 0;
    if (m_dbc == SQL_NULL_HDBC) throw std::runtime_error("ODBC: dbc handle null");
    ret = SQLDriverConnect(m_dbc, NULL, (SQLCHAR*)m_conn_str.c_str(), SQL_NTS, NULL, 0, &outstrlen, SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(ret)) raise_if_error(ret, SQL_HANDLE_DBC, m_dbc, "SQLDriverConnect");
}
void MssqlStrategyNative::raise_if_error(SQLRETURN ret, SQLSMALLINT type, SQLHANDLE handle, const char* what) {
    if (SQL_SUCCEEDED(ret)) return; SQLCHAR state[6]; SQLINTEGER native; SQLCHAR msg[256]; SQLSMALLINT len;
    if (SQLGetDiagRec(type, handle, 1, state, &native, msg, sizeof(msg), &len) == SQL_SUCCESS) {
        std::stringstream ss; ss << what << " failed: [" << state << "] " << msg; throw std::runtime_error(ss.str()); }
    throw std::runtime_error(std::string(what) + " failed (no diagnostics)");
}
bool MssqlStrategyNative::is_valid_identifier(const std::string& s) { static const std::regex r("^[A-Za-z_][A-Za-z0-9_]*$"); return std::regex_match(s, r); }
py::object MssqlStrategyNative::fetch_impl(const std::string& table, const py::object& pk) {
    SQLHSTMT stmt = SQL_NULL_HSTMT; if (SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt) != SQL_SUCCESS) throw std::runtime_error("ODBC: alloc stmt failed");
    std::stringstream ss; ss << "SELECT * FROM " << table << " WHERE id=?"; std::string sql = ss.str();
    SQLRETURN ret = SQLPrepare(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS); if (!SQL_SUCCEEDED(ret)) { SQLFreeHandle(SQL_HANDLE_STMT, stmt); raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare"); }
    long long llval=0; std::string svalue; bool is_int=false; if (py::isinstance<py::int_>(pk)) { llval=pk.cast<long long>(); is_int=true; } else { svalue=py::str(pk); }
    if (is_int) ret = SQLBindParameter(stmt,1,SQL_PARAM_INPUT,SQL_C_SBIGINT,SQL_BIGINT,0,0,&llval,0,NULL);
    else { SQLLEN ind=svalue.size(); ret = SQLBindParameter(stmt,1,SQL_PARAM_INPUT,SQL_C_CHAR,SQL_VARCHAR,ind,0,(SQLPOINTER)svalue.c_str(),0,&ind); }
    if (!SQL_SUCCEEDED(ret)) { SQLFreeHandle(SQL_HANDLE_STMT, stmt); raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter"); }
    ret = SQLExecute(stmt); if (!SQL_SUCCEEDED(ret)) { SQLFreeHandle(SQL_HANDLE_STMT, stmt); return py::none(); }
    ret = SQLFetch(stmt); if (ret == SQL_NO_DATA) { SQLFreeHandle(SQL_HANDLE_STMT, stmt); return py::none(); }
    SQLSMALLINT colCount=0; SQLNumResultCols(stmt,&colCount); py::dict row;
    for (SQLSMALLINT i=1;i<=colCount;++i) { char colName[128]; SQLSMALLINT nameLen=0,dataType=0,scale=0,nullable=0; SQLULEN colSize=0;
        SQLDescribeCol(stmt,i,(SQLCHAR*)colName,sizeof(colName),&nameLen,&dataType,&colSize,&scale,&nullable);
        std::vector<char> buf(colSize+16); SQLLEN outLen=0; SQLRETURN dret=SQLGetData(stmt,i,SQL_C_CHAR,buf.data(),buf.size(),&outLen);
        if (SQL_SUCCEEDED(dret)) { if (outLen == SQL_NULL_DATA) row[colName]=py::none(); else row[colName]=py::str(buf.data()); }
        else row[colName]=py::none(); }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt); return std::move(row);
}
void MssqlStrategyNative::upsert_impl(const std::string& table, const py::object& pk, const py::object& value_mapping) {
    if (!py::isinstance<py::dict>(value_mapping)) throw std::runtime_error("MssqlStrategyNative.save expects a dict-like value");
    py::dict d=value_mapping.cast<py::dict>(); std::stringstream updateSS; updateSS << "UPDATE " << table << " SET "; bool first=true;
    for (auto & item : d) { std::string col=py::str(item.first); std::string val=py::str(item.second); if (!is_valid_identifier(col)) continue; if (!first) updateSS << ','; first=false; updateSS << col << "='" << val << "'"; }
    updateSS << " WHERE id='" << py::str(pk) << "'"; std::string updateSQL=updateSS.str(); SQLHSTMT stmt=SQL_NULL_HSTMT; SQLAllocHandle(SQL_HANDLE_STMT,m_dbc,&stmt);
    SQLRETURN ret = SQLExecDirect(stmt,(SQLCHAR*)updateSQL.c_str(),SQL_NTS); bool needInsert=false; if (!SQL_SUCCEEDED(ret)) needInsert=true; else { SQLLEN rowCount=0; SQLRowCount(stmt,&rowCount); if (rowCount==0) needInsert=true; }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt); if (!needInsert) return; std::stringstream cols; std::stringstream vals; first=true;
    for (auto & item : d) { std::string col=py::str(item.first); std::string val=py::str(item.second); if (!is_valid_identifier(col)) continue; if (!first) { cols << ','; vals << ','; } first=false; cols << col; vals << "'" << val << "'"; }
    std::string insertSQL = "INSERT INTO " + table + " (id," + cols.str() + ") VALUES ('" + py::str(pk).cast<std::string>() + "'," + vals.str() + ")";
    SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &stmt); ret = SQLExecDirect(stmt,(SQLCHAR*)insertSQL.c_str(),SQL_NTS); SQLFreeHandle(SQL_HANDLE_STMT, stmt); if (!SQL_SUCCEEDED(ret)) raise_if_error(ret, SQL_HANDLE_STMT, stmt, "INSERT exec");
}
py::object MssqlStrategyNative::execute_query_object(const py::object& query_obj) {
    std::string sql = query_obj.attr("sql").cast<std::string>(); py::list params = query_obj.attr("params")(); SQLHSTMT stmt=SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT,m_dbc,&stmt) != SQL_SUCCESS) throw std::runtime_error("ODBC: alloc stmt failed");
    size_t limit_pos = sql.rfind(" LIMIT "); if (limit_pos != std::string::npos) { std::string before=sql.substr(0,limit_pos); std::string after=sql.substr(limit_pos+7); std::stringstream ls(after); int n; ls >> n; if (!ls.fail() && before.rfind("SELECT",0)==0) sql = "SELECT TOP " + std::to_string(n) + before.substr(6); }
    SQLRETURN ret = SQLPrepare(stmt,(SQLCHAR*)sql.c_str(),SQL_NTS); if (!SQL_SUCCEEDED(ret)) { SQLFreeHandle(SQL_HANDLE_STMT, stmt); raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLPrepare"); }
    std::vector<long long> int_storage; std::vector<std::string> str_storage; std::vector<SQLLEN> ind(params.size()); int_storage.reserve(params.size()); str_storage.reserve(params.size());
    for (size_t i=0;i<params.size();++i) { py::object p=params[i]; if (p.is_none()) { ind[i]=SQL_NULL_DATA; static const char* dummy=""; ret=SQLBindParameter(stmt,(SQLUSMALLINT)(i+1),SQL_PARAM_INPUT,SQL_C_CHAR,SQL_VARCHAR,0,0,(SQLPOINTER)dummy,0,&ind[i]); }
        else if (py::isinstance<py::int_>(p)) { int_storage.push_back(p.cast<long long>()); ind[i]=0; ret=SQLBindParameter(stmt,(SQLUSMALLINT)(i+1),SQL_PARAM_INPUT,SQL_C_SBIGINT,SQL_BIGINT,0,0,&int_storage.back(),0,&ind[i]); }
        else { str_storage.emplace_back(py::str(p)); ind[i]=(SQLLEN)str_storage.back().size(); ret=SQLBindParameter(stmt,(SQLUSMALLINT)(i+1),SQL_PARAM_INPUT,SQL_C_CHAR,SQL_VARCHAR,ind[i],0,(SQLPOINTER)str_storage.back().c_str(),0,&ind[i]); }
        if (!SQL_SUCCEEDED(ret)) { SQLFreeHandle(SQL_HANDLE_STMT, stmt); raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter"); }
    }
    ret=SQLExecute(stmt); if (!SQL_SUCCEEDED(ret)) { SQLFreeHandle(SQL_HANDLE_STMT, stmt); return py::none(); }
    SQLSMALLINT colCount=0; SQLNumResultCols(stmt,&colCount); py::list rows; while (true) { ret=SQLFetch(stmt); if (ret==SQL_NO_DATA) break; if (!SQL_SUCCEEDED(ret)) break; py::dict row; for (SQLSMALLINT c=1;c<=colCount;++c){ char colName[128]; SQLSMALLINT nameLen=0,dataType=0,scale=0,nullable=0; SQLULEN colSize=0; SQLDescribeCol(stmt,c,(SQLCHAR*)colName,sizeof(colName),&nameLen,&dataType,&colSize,&scale,&nullable); std::vector<char> buf(colSize+16); SQLLEN outLen=0; SQLRETURN dret=SQLGetData(stmt,c,SQL_C_CHAR,buf.data(),buf.size(),&outLen); if (SQL_SUCCEEDED(dret)) { if (outLen==SQL_NULL_DATA) row[colName]=py::none(); else row[colName]=py::str(buf.data()); } else row[colName]=py::none(); } rows.append(std::move(row)); }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt); return rows;
}
#endif
} // namespace pygim

namespace py = pybind11;
PYBIND11_MODULE(mssql_strategy, m) {
    py::class_<pygim::MssqlStrategyNative>(m, "MssqlStrategyNative")
        .def(py::init<std::string>(), py::arg("connection_string"))
        .def("fetch", &pygim::MssqlStrategyNative::fetch, py::arg("key"))
        .def("save", &pygim::MssqlStrategyNative::save, py::arg("key"), py::arg("value"))
        .def("__repr__", &pygim::MssqlStrategyNative::repr);
}
