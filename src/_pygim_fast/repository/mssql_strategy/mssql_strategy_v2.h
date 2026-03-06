// MssqlStrategy v2: pybind-free MSSQL storage strategy.
// Implements core::Strategy with ODBC backend.
//
// All Python data extraction is done in the adapter layer before reaching
// this class. Every method accepts/returns only core C++ types.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/strategy.h"
#include "../core/value_types.h"
#include "../query/mssql_dialect.h"

#include <sql.h>
#include <sqlext.h>
#ifdef BOOL
#  undef BOOL
#endif
#ifdef INT
#  undef INT
#endif
#if !defined(_WIN32) && !defined(_WIN64)
#  ifndef SQL_COPT_SS_BCP
#    define SQL_COPT_SS_BASE 1200
#    define SQL_COPT_SS_BCP (SQL_COPT_SS_BASE + 19)
#    define SQL_BCP_ON ((SQLULEN)1L)
#    define SQL_BCP_OFF ((SQLULEN)0L)
#  endif
#endif

namespace pygim::mssql {

/// BCP performance metrics — all pure C++, no pybind.
struct BcpMetrics {
    double setup_seconds{0.0};
    double reader_open_seconds{0.0};
    double bind_columns_seconds{0.0};
    double row_loop_seconds{0.0};
    double batch_flush_seconds{0.0};
    double done_seconds{0.0};
    double total_seconds{0.0};
    long long processed_rows{0};
    long long sent_rows{0};
    long long record_batches{0};
    std::string input_mode{"none"};
};

/// Pybind-free MSSQL strategy implementing core::Strategy.
///
/// Owns ODBC connection handles and an MssqlDialect instance.
/// All public methods accept/return only core types.
class MssqlStrategy final : public core::Strategy {
public:
    explicit MssqlStrategy(std::string connection_string);
    ~MssqlStrategy() override;

    // Non-copyable, non-movable (owns ODBC handles).
    MssqlStrategy(const MssqlStrategy &) = delete;
    MssqlStrategy &operator=(const MssqlStrategy &) = delete;

    // ---- core::Strategy interface -------------------------------------------

    core::StrategyCapabilities capabilities() const override;
    const core::QueryDialect *dialect() const override;

    std::optional<core::ResultSet> fetch(const core::RenderedQuery &query) override;
    void save(const core::TablePkKey &key, const core::RowMap &data) override;

    /// Persist bulk data from a DataView.
    ///
    /// Dispatches internally:
    ///   ArrowView     → BCP via bulk_insert_arrow_bcp (zero-copy, fastest path)
    ///   TypedBatchView → INSERT or MERGE based on opts.mode
    void persist(const core::TableSpec &table_spec,
                 core::DataView view,
                 const core::PersistOptions &opts) override;

    // ---- Metrics & introspection --------------------------------------------

    const BcpMetrics &last_bcp_metrics() const noexcept { return m_last_bcp_metrics; }
    std::string repr() const;

    // ---- ODBC handle access (for detail implementations) --------------------

    SQLHDBC connection_handle() const noexcept { return m_dbc; }
    static void raise_if_error(SQLRETURN ret, SQLSMALLINT type,
                               SQLHANDLE handle, const char *what);

private:
    std::string m_conn_str;
    query::MssqlDialect m_dialect;
    BcpMetrics m_last_bcp_metrics;

    SQLHENV m_env{SQL_NULL_HENV};
    SQLHDBC m_dbc{SQL_NULL_HDBC};
    bool m_connected{false};
    bool m_bcp_attr_enabled{false};

    void init_handles();
    void cleanup_handles();
    void ensure_connected();

    // ---- Internal implementation methods ------------------------------------

    /// Execute a SELECT and return results.
    std::optional<core::ResultSet> fetch_impl(const std::string &sql,
                                              const std::vector<core::CellValue> &params);

    /// Upsert a single row (UPDATE then INSERT on miss).
    void upsert_impl(const std::string &table,
                     const core::CellValue &pk,
                     const core::RowMap &data);

    /// Run a typed batch through multi-row INSERT statements.
    void bulk_insert_typed(const std::string &table,
                           const core::TypedColumnBatch &batch,
                           int batch_size,
                           const std::string &table_hint);

    /// Run a typed batch through MERGE statements.
    void bulk_upsert_typed(const std::string &table,
                           const core::TypedColumnBatch &batch,
                           const std::string &key_column,
                           int batch_size,
                           const std::string &table_hint);
};

} // namespace pygim::mssql
