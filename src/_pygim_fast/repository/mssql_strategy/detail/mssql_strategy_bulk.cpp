#include "../mssql_strategy.h"

#include <type_traits>

#include <pybind11/numpy.h>

#include "helpers.h"
#include "sql_builders.h"
#include "value_objects.h"
#include "../../../utils/logging.h"

namespace py = pybind11;

namespace pygim {

#if PYGIM_HAVE_ODBC
namespace bulk_detail {

template <typename ModeTag>
constexpr bool is_polars_mode_v = std::is_same_v<ModeTag, detail::PolarsTag>;

struct ColumnAccess {
    enum Kind {
        I64,
        F64,
        BOOL_T,
        STR
    } kind;
    py::array array;
    py::list list;
};

template <typename ModeTag>
void prepare_accessors(const detail::BatchDescriptor<ModeTag> &, std::vector<ColumnAccess> &accs) {
    PYGIM_SCOPE_LOG_TAG("repo.bulk");
    accs.clear();
}

template <>
inline void prepare_accessors(const detail::BatchDescriptor<detail::PolarsTag> &batch,
                              std::vector<ColumnAccess> &accs) {
    PYGIM_SCOPE_LOG_TAG("repo.bulk");
    const auto &columns = batch.spec.columns;
    accs.reserve(columns.size());
    for (const auto &name : columns) {
        py::object series = batch.rows.attr("get_column")(name);
        std::string dtype = py::str(series.attr("dtype"));
        if (dtype.find("Int") != std::string::npos) {
            accs.push_back({ColumnAccess::I64, series.attr("to_numpy")().cast<py::array>(), py::list()});
        } else if (dtype.find("Float") != std::string::npos) {
            accs.push_back({ColumnAccess::F64, series.attr("to_numpy")().cast<py::array>(), py::list()});
        } else if (dtype.find("Boolean") != std::string::npos) {
            accs.push_back({ColumnAccess::BOOL_T, series.attr("to_numpy")().cast<py::array>(), py::list()});
        } else {
            accs.push_back({ColumnAccess::STR, py::array(), series.attr("to_list")().cast<py::list>()});
        }
    }
}

template <typename ModeTag>
void run_bulk_insert(MssqlStrategyNative &self, detail::BatchDescriptor<ModeTag> &batch) {
    PYGIM_SCOPE_LOG_TAG("repo.bulk");
    constexpr bool polars_mode = is_polars_mode_v<ModeTag>;
    const auto &spec = batch.spec;
    const size_t ncols = spec.column_count();
    if (ncols == 0) {
        return;
    }
    if (static_cast<int>(ncols) > batch.options.param_limit) {
        throw std::runtime_error("Too many columns for one INSERT");
    }
    int batch_size = batch.options.batch_size > 0 ? batch.options.batch_size : 1000;
    const int param_limit = batch.options.param_limit;
    const int rows_per_stmt_init = std::max(1, std::min(batch_size, param_limit / static_cast<int>(ncols)));

    detail::InsertSqlBuilder builder(spec, spec.columns);
    auto build_insert_sql = [&](int rows_per_stmt) {
        return builder.build(rows_per_stmt);
    };

    py::iterator iter(batch.rows);
    int total_rows_hint = batch.rows_hint;
    std::vector<ColumnAccess> accessors;
    if constexpr (polars_mode) {
        prepare_accessors(batch, accessors);
        total_rows_hint = batch.rows_hint;
    } else {
        if (!py::hasattr(batch.rows, "__iter__")) {
            throw std::runtime_error("rows must be an iterable or a Polars DataFrame");
        }
    }

    std::vector<long long> int_storage;
    std::vector<std::string> str_storage;
    std::vector<SQLLEN> ind_storage;
    int_storage.reserve(static_cast<size_t>(batch_size) * ncols);
    str_storage.reserve(static_cast<size_t>(batch_size) * ncols);
    ind_storage.reserve(static_cast<size_t>(batch_size) * ncols);

    SQLHDBC dbc = self.connection_handle();
    SQLUINTEGER old_autocommit = SQL_AUTOCOMMIT_ON;
    SQLINTEGER outlen = 0;
    SQLGetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, &old_autocommit, 0, &outlen);
    SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);

    const std::string sql_full = build_insert_sql(rows_per_stmt_init);
    SQLHSTMT stmt_full = SQL_NULL_HSTMT;
    SQLHSTMT stmt_tail = SQL_NULL_HSTMT;
    int tail_rows = 0;
    SQLRETURN ret;
    try {
        if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt_full) != SQL_SUCCESS) {
            throw std::runtime_error("ODBC: alloc stmt failed");
        }
        ret = SQLPrepare(stmt_full, (SQLCHAR *)sql_full.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            try {
                MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt_full, "SQLPrepare");
            } catch (...) {
                SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
                throw;
            }
        }

        int polars_offset = 0;
        while (true) {
            int rows_this = 0;
            std::vector<py::sequence> py_rows;
            py_rows.reserve(static_cast<size_t>(rows_per_stmt_init));
            if constexpr (polars_mode) {
                rows_this = std::min(rows_per_stmt_init, total_rows_hint - polars_offset);
                if (rows_this <= 0) {
                    break;
                }
            } else {
                for (; rows_this < rows_per_stmt_init && iter != py::iterator::sentinel(); ++rows_this, ++iter) {
                    py::handle h = *iter;
                    py::object row_obj = py::reinterpret_borrow<py::object>(h);
                    if (!py::isinstance<py::sequence>(row_obj)) {
                        throw std::runtime_error("Each row must be a sequence");
                    }
                    py::sequence row = row_obj.cast<py::sequence>();
                    if (static_cast<size_t>(py::len(row)) != ncols) {
                        throw std::runtime_error("Row length mismatch");
                    }
                    py_rows.push_back(row);
                }
                if (rows_this == 0) {
                    break;
                }
            }

            SQLHSTMT stmt = stmt_full;
            if (rows_this != rows_per_stmt_init) {
                if (rows_this != tail_rows) {
                    if (stmt_tail != SQL_NULL_HSTMT) {
                        SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
                        stmt_tail = SQL_NULL_HSTMT;
                    }
                    const std::string sql_tail = build_insert_sql(rows_this);
                    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt_tail) != SQL_SUCCESS) {
                        throw std::runtime_error("ODBC: alloc stmt failed");
                    }
                    ret = SQLPrepare(stmt_tail, (SQLCHAR *)sql_tail.c_str(), SQL_NTS);
                    if (!SQL_SUCCEEDED(ret)) {
                        try {
                            MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt_tail, "SQLPrepare");
                        } catch (...) {
                            SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
                            throw;
                        }
                    }
                    tail_rows = rows_this;
                }
                stmt = stmt_tail;
            }

            SQLFreeStmt(stmt, SQL_RESET_PARAMS);
            std::vector<double> dbl_storage;
            dbl_storage.reserve(static_cast<size_t>(rows_this) * ncols);
            int_storage.clear();
            str_storage.clear();
            ind_storage.clear();
            std::vector<const void *> col_ptrs;
            if constexpr (polars_mode) {
                col_ptrs.resize(ncols, nullptr);
                for (size_t c = 0; c < ncols; ++c) {
                    const ColumnAccess &access = accessors[c];
                    if (access.kind != ColumnAccess::STR) {
                        col_ptrs[c] = access.array.request().ptr;
                    }
                }
            }

            size_t param_index = 1;
            for (int r = 0; r < rows_this; ++r) {
                for (size_t c = 0; c < ncols; ++c) {
                    if constexpr (polars_mode) {
                        const ColumnAccess &access = accessors[c];
                        size_t idx = static_cast<size_t>(polars_offset) + static_cast<size_t>(r);
                        if (access.kind == ColumnAccess::I64) {
                            auto *ptr = reinterpret_cast<const long long *>(col_ptrs[c]);
                            long long v = ptr[idx];
                            int_storage.push_back(v);
                            ind_storage.push_back(0);
                            ret = SQLBindParameter(stmt, static_cast<SQLUSMALLINT>(param_index), SQL_PARAM_INPUT,
                                                   SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &int_storage.back(), 0, &ind_storage.back());
                        } else if (access.kind == ColumnAccess::F64) {
                            auto *ptr = reinterpret_cast<const double *>(col_ptrs[c]);
                            double dv = ptr[idx];
                            dbl_storage.push_back(dv);
                            ind_storage.push_back(0);
                            ret = SQLBindParameter(stmt, static_cast<SQLUSMALLINT>(param_index), SQL_PARAM_INPUT,
                                                   SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &dbl_storage.back(), 0, &ind_storage.back());
                        } else if (access.kind == ColumnAccess::BOOL_T) {
                            auto *ptr = reinterpret_cast<const uint8_t *>(col_ptrs[c]);
                            uint8_t bv = ptr[idx] ? 1 : 0;
                            int_storage.push_back(static_cast<long long>(bv));
                            ind_storage.push_back(0);
                            ret = SQLBindParameter(stmt, static_cast<SQLUSMALLINT>(param_index), SQL_PARAM_INPUT,
                                                   SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &int_storage.back(), 0, &ind_storage.back());
                        } else {
                            std::string sv = py::str(access.list[static_cast<py::ssize_t>(idx)]);
                            str_storage.emplace_back(std::move(sv));
                            ind_storage.push_back(static_cast<SQLLEN>(str_storage.back().size()));
                            ret = SQLBindParameter(stmt, static_cast<SQLUSMALLINT>(param_index), SQL_PARAM_INPUT,
                                                   SQL_C_CHAR, SQL_VARCHAR, ind_storage.back(), 0,
                                                   (SQLPOINTER)str_storage.back().c_str(), 0, &ind_storage.back());
                        }
                    } else {
                        py::sequence row = py_rows[static_cast<size_t>(r)];
                        py::object value = row[static_cast<py::ssize_t>(c)];
                        if (value.is_none()) {
                            ind_storage.push_back(SQL_NULL_DATA);
                            static const char *dummy = "";
                            ret = SQLBindParameter(stmt, static_cast<SQLUSMALLINT>(param_index), SQL_PARAM_INPUT,
                                                   SQL_C_CHAR, SQL_VARCHAR, 0, 0, (SQLPOINTER)dummy, 0, &ind_storage.back());
                        } else if (py::isinstance<py::int_>(value)) {
                            int_storage.push_back(value.cast<long long>());
                            ind_storage.push_back(0);
                            ret = SQLBindParameter(stmt, static_cast<SQLUSMALLINT>(param_index), SQL_PARAM_INPUT,
                                                   SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &int_storage.back(), 0, &ind_storage.back());
                        } else {
                            std::string sv = py::str(value);
                            str_storage.emplace_back(std::move(sv));
                            ind_storage.push_back(static_cast<SQLLEN>(str_storage.back().size()));
                            ret = SQLBindParameter(stmt, static_cast<SQLUSMALLINT>(param_index), SQL_PARAM_INPUT,
                                                   SQL_C_CHAR, SQL_VARCHAR, ind_storage.back(), 0,
                                                   (SQLPOINTER)str_storage.back().c_str(), 0, &ind_storage.back());
                        }
                    }
                    if (!SQL_SUCCEEDED(ret)) {
                        try {
                            MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter");
                        } catch (...) {
                            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
                            throw;
                        }
                    }
                    ++param_index;
                }
            }

            ret = SQLExecute(stmt);
            if (!SQL_SUCCEEDED(ret)) {
                try {
                    MssqlStrategyNative::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLExecute");
                } catch (...) {
                    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
                    throw;
                }
            }
            SQLFreeStmt(stmt, SQL_CLOSE);
            if constexpr (polars_mode) {
                polars_offset += rows_this;
            }
        }
        SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    } catch (...) {
        SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_ROLLBACK);
        if (stmt_full != SQL_NULL_HSTMT) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
        }
        if (stmt_tail != SQL_NULL_HSTMT) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
        }
        SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)old_autocommit, 0);
        throw;
    }
    SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)old_autocommit, 0);
    if (stmt_full != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt_full);
    }
    if (stmt_tail != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt_tail);
    }
}

} // namespace bulk_detail
#endif

void MssqlStrategyNative::bulk_insert(const std::string &table,
                                      const std::vector<std::string> &columns,
                                      const py::object &rows,
                                      int batch_size,
                                      const std::string &table_hint) {
    PYGIM_SCOPE_LOG_TAG("repo.bulk");
#if PYGIM_HAVE_ODBC
    ensure_connected();
    auto build_spec = [&]() {
        return detail::make_table_spec(table, columns, std::nullopt, table_hint);
    };
    detail::BatchOptions options;
    options.batch_size = batch_size > 0 ? batch_size : 1000;
    options.param_limit = 2090;
    if (detail::is_polars_dataframe(rows)) {
        auto spec = build_spec();
        int hint = rows.attr("height").cast<int>();
        detail::BatchDescriptor<detail::PolarsTag> descriptor(std::move(spec), options, rows, hint);
        bulk_detail::run_bulk_insert(*this, descriptor);
    } else {
        if (!py::hasattr(rows, "__iter__")) {
            throw std::runtime_error("rows must be an iterable or a Polars DataFrame");
        }
        auto spec = build_spec();
        detail::BatchDescriptor<detail::IterableTag> descriptor(std::move(spec), options, rows);
        bulk_detail::run_bulk_insert(*this, descriptor);
    }
#else
    throw std::runtime_error("MssqlStrategyNative built without ODBC headers; feature unavailable");
#endif
}

} // namespace pygim
