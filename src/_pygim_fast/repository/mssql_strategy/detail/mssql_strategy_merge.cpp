#include "../mssql_strategy.h"

#include "helpers.h"
#include "mssql_batch_source.h"
#include "mssql_batch_spec.h"
#include "mssql_bulk_upsert_impl.h"
#include "../../../utils/logging.h"

namespace py = pybind11;

namespace pygim {

#if PYGIM_HAVE_ODBC

void MssqlStrategyNative::bulk_upsert(const std::string &table,
                                      const std::vector<std::string> &columns,
                                      const py::object &rows,
                                      const std::string &key_column,
                                      int batch_size,
                                      const std::string &table_hint) {
    PYGIM_SCOPE_LOG_TAG("repo.merge");
    ensure_connected();
    if (!detail::is_valid_identifier(table)) {
        throw std::runtime_error("Invalid table identifier");
    }
    for (const auto &col : columns) {
        if (!detail::is_valid_identifier(col)) {
            throw std::runtime_error("Invalid column identifier");
        }
    }
    if (!detail::is_valid_identifier(key_column)) {
        throw std::runtime_error("Invalid key column identifier");
    }
    detail::BatchSpec spec(table, columns, key_column, table_hint, batch_size);
    if (detail::is_polars_dataframe(rows)) {
        upsert_polars_impl(table, columns, rows, key_column, batch_size, table_hint);
        return;
    }
    if (!py::hasattr(rows, "__iter__")) {
        throw std::runtime_error("rows must be an iterable of sequences");
    }
    detail::IterableRowSource source(rows, spec.column_count());
    detail::bulk_upsert_impl(m_dbc, spec, source);
}

void MssqlStrategyNative::upsert_polars_impl(const std::string &table,
                                             const std::vector<std::string> &columns,
                                             const py::object &df,
                                             const std::string &key_column,
                                             int batch_size,
                                             const std::string &table_hint) {
    PYGIM_SCOPE_LOG_TAG("repo.merge");
    detail::BatchSpec spec(table, columns, key_column, table_hint, batch_size);
    detail::PolarsRowSource source(df, columns);
    detail::bulk_upsert_impl(m_dbc, spec, source);
}

#endif

} // namespace pygim
