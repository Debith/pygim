#include "mssql_transaction.h"

#include <stdexcept>

#include "../mssql_strategy.h"
#include "../../../utils/logging.h"

namespace pygim::detail {

TransactionGuard::TransactionGuard(SQLHDBC dbc) : dbc_(dbc) {
    PYGIM_SCOPE_LOG_TAG("repo.transaction");
    SQLINTEGER outlen = 0;
    SQLGetConnectAttr(dbc_, SQL_ATTR_AUTOCOMMIT, &previous_mode_, 0, &outlen);
    SQLSetConnectAttr(dbc_, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
}

TransactionGuard::~TransactionGuard() {
    PYGIM_SCOPE_LOG_TAG("repo.transaction");
    if (active_) {
        rollback();
    }
    SQLSetConnectAttr(dbc_, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)previous_mode_, 0);
}

void TransactionGuard::commit() {
    PYGIM_SCOPE_LOG_TAG("repo.transaction");
    if (!active_) {
        return;
    }
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, dbc_, SQL_COMMIT);
    if (!SQL_SUCCEEDED(ret)) {
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_DBC, dbc_, "SQLEndTran(commit)");
    }
    active_ = false;
}

void TransactionGuard::rollback() {
    PYGIM_SCOPE_LOG_TAG("repo.transaction");
    if (!active_) {
        return;
    }
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, dbc_, SQL_ROLLBACK);
    if (!SQL_SUCCEEDED(ret)) {
        MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_DBC, dbc_, "SQLEndTran(rollback)");
    }
    active_ = false;
}

} // namespace pygim::detail
