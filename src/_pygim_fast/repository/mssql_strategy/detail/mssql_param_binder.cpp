#include "mssql_param_binder.h"

#include "../mssql_strategy.h"
#include "../../../utils/logging.h"

namespace pygim::detail {

void ParameterBinder::reset() {
    PYGIM_SCOPE_LOG_TAG("repo.param_binder");
    int_storage_.clear();
    double_storage_.clear();
    string_storage_.clear();
    indicator_storage_.clear();
}

void ParameterBinder::bind_null(SQLHSTMT stmt, SQLUSMALLINT param_index) {
    PYGIM_SCOPE_LOG_TAG("repo.param_binder");
    indicator_storage_.push_back(SQL_NULL_DATA);
    SQLRETURN ret = SQLBindParameter(stmt, param_index, SQL_PARAM_INPUT,
                                     SQL_C_CHAR, SQL_VARCHAR, 0, 0, nullptr, 0,
                                     &indicator_storage_.back());
    if (!SQL_SUCCEEDED(ret)) {
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter(null)");
    }
}

void ParameterBinder::bind_int(SQLHSTMT stmt, SQLUSMALLINT param_index, long long value) {
    PYGIM_SCOPE_LOG_TAG("repo.param_binder");
    int_storage_.push_back(value);
    indicator_storage_.push_back(0);
    SQLRETURN ret = SQLBindParameter(stmt, param_index, SQL_PARAM_INPUT,
                                     SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
                                     &int_storage_.back(), 0, &indicator_storage_.back());
    if (!SQL_SUCCEEDED(ret)) {
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter(int)");
    }
}

void ParameterBinder::bind_double(SQLHSTMT stmt, SQLUSMALLINT param_index, double value) {
    PYGIM_SCOPE_LOG_TAG("repo.param_binder");
    double_storage_.push_back(value);
    indicator_storage_.push_back(0);
    SQLRETURN ret = SQLBindParameter(stmt, param_index, SQL_PARAM_INPUT,
                                     SQL_C_DOUBLE, SQL_DOUBLE, 0, 0,
                                     &double_storage_.back(), 0, &indicator_storage_.back());
    if (!SQL_SUCCEEDED(ret)) {
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter(double)");
    }
}

void ParameterBinder::bind_string(SQLHSTMT stmt, SQLUSMALLINT param_index, const std::string &value) {
    PYGIM_SCOPE_LOG_TAG("repo.param_binder");
    string_storage_.push_back(value);
    indicator_storage_.push_back(static_cast<SQLLEN>(value.size()));
    const std::string &stored = string_storage_.back();
    SQLRETURN ret = SQLBindParameter(stmt, param_index, SQL_PARAM_INPUT,
                                     SQL_C_CHAR, SQL_VARCHAR,
                                     indicator_storage_.back(), 0,
                                     (SQLPOINTER)stored.c_str(), 0, &indicator_storage_.back());
    if (!SQL_SUCCEEDED(ret)) {
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter(str)");
    }
}

} // namespace pygim::detail
