// MssqlStrategy: pybind-free MSSQL storage strategy.
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

// Include transpose strategies after ODBC headers + BOOL/INT cleanup so that
// value_types.h (already parsed above) is safe and BOOL is not re-defined.
#include "detail/bcp/bcp_transpose_strategy.h"

namespace pygim::mssql {

/// BCP performance metrics — all pure C++, no pybind.
struct BcpMetrics {
    double setup_seconds{0.0};
    double reader_open_seconds{0.0};
    double bind_columns_seconds{0.0};
    double row_loop_seconds{0.0};
    double fixed_copy_seconds{0.0};
    double colptr_redirect_seconds{0.0};
    double string_pack_seconds{0.0};
    double sendrow_seconds{0.0};
    double batch_flush_seconds{0.0};
    double done_seconds{0.0};
    double total_seconds{0.0};
    long long processed_rows{0};
    long long sent_rows{0};
    long long record_batches{0};
    std::string input_mode{"none"};
    std::string simd_level{"scalar"};
    std::string timing_level{"stage"};
};

/// Pybind-free MSSQL strategy implementing core::Strategy.
///
/// Templated on Transpose — the row-loop transpose algorithm used in the BCP
/// hot path.  The default (RowMajorTranspose) preserves the pre-0.4 behaviour.
/// Pass ColumnMajorTranspose (or a future SIMD strategy) at acquire_repository()
/// time to select a different algorithm for the lifetime of the connection.
///
/// The template parameter is resolved once at construction; process_batch<T>()
/// is called with a concrete Transpose& reference, enabling the compiler to
/// devirtualize and inline T::run() (T is final in all provided strategies).
template <typename Transpose = bcp::RowMajorTranspose>
class MssqlStrategy : public core::Strategy {
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
    ///   ArrowView     → BCP via process_batch<Transpose> (zero-copy, devirtualized)
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
    Transpose m_transpose{};    ///< Transpose algorithm — fixed for this connection's lifetime.

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

    /// Run a typed batch through multi-row INSERT or MERGE statements.
    ///
    /// Mode is resolved at compile time; builder type, default batch size,
    /// and error-context labels are selected via if constexpr — the loop body
    /// is shared verbatim between both operations.
    template <core::PersistMode Mode>
    void bulk_persist_typed(const std::string &table,
                            const core::TypedColumnBatch &batch,
                            const std::string &key_column,
                            int batch_size,
                            const std::string &table_hint);
};

// Explicit instantiation declarations — bodies are in mssql_strategy_core.cpp.
// Including this header suppresses implicit instantiation in every TU.
extern template class MssqlStrategy<bcp::RowMajorTranspose>;
extern template class MssqlStrategy<bcp::ColumnMajorTranspose>;

// ---- Factory ----------------------------------------------------------------

/// Create the appropriate MssqlStrategy specialisation from a hint string.
///
/// @param conn_str       ODBC connection string.
/// @param transpose_hint Selects the BCP transpose algorithm:
///                         ""             / "row_major"    → RowMajorTranspose (default)
///                         "column_major"                  → ColumnMajorTranspose
/// @return               Owning pointer to the constructed strategy.
std::unique_ptr<core::Strategy> make_mssql_strategy(
    const std::string &conn_str,
    const std::string &transpose_hint = "");

} // namespace pygim::mssql
