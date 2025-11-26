#pragma once

#include <sql.h>
#include <sqlext.h>

namespace pygim::detail {

class TransactionGuard {
public:
    explicit TransactionGuard(SQLHDBC dbc);
    TransactionGuard(const TransactionGuard &) = delete;
    TransactionGuard &operator=(const TransactionGuard &) = delete;
    ~TransactionGuard();

    void commit();
    void rollback();

private:
    SQLHDBC dbc_;
    SQLUINTEGER previous_mode_{SQL_AUTOCOMMIT_ON};
    bool active_{true};
};

} // namespace pygim::detail
