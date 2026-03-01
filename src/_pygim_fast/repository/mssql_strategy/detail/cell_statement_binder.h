// CellStatementBinder: binds core::CellValue parameters to ODBC statements.
// Pybind-free — operates entirely on C++ variant types.
//
// Replaces the old StatementBinder (which required py::object) and
// ParameterBinder (which required BatchRowSource concept with py::object).
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "../../core/value_types.h"

#if PYGIM_HAVE_ODBC
#  include <sql.h>
#  include <sqlext.h>
#endif

namespace pygim::mssql::detail {

#if PYGIM_HAVE_ODBC

/// Binds a vector of CellValue to an ODBC prepared statement.
/// Manages storage lifetimes for bound parameters.
class CellStatementBinder {
public:
    CellStatementBinder() = default;

    /// Bind all CellValues as parameters starting at position 1.
    void bind_all(SQLHSTMT stmt, const std::vector<core::CellValue> &params) {
        reset();
        if (params.empty()) return;
        m_int_storage.reserve(params.size());
        m_dbl_storage.reserve(params.size());
        m_str_storage.reserve(params.size());
        m_indicators.reserve(params.size());
        SQLFreeStmt(stmt, SQL_RESET_PARAMS);
        for (size_t i = 0; i < params.size(); ++i) {
            bind_one(stmt, static_cast<SQLUSMALLINT>(i + 1), params[i]);
        }
    }

    /// Bind a flat range of CellValues from a TypedColumnBatch (row-major order).
    /// Used for multi-row INSERT/MERGE statements where parameters are
    /// laid out as [row0_col0, row0_col1, ..., row1_col0, row1_col1, ...].
    void bind_batch(SQLHSTMT stmt,
                    const core::TypedColumnBatch &batch,
                    size_t row_begin,
                    size_t row_end) {
        reset();
        const size_t ncols = batch.columns.size();
        const size_t nrows = row_end - row_begin;
        if (ncols == 0 || nrows == 0) return;
        const size_t total = nrows * ncols;
        m_int_storage.reserve(total);
        m_dbl_storage.reserve(total);
        m_str_storage.reserve(total);
        m_indicators.reserve(total);
        SQLFreeStmt(stmt, SQL_RESET_PARAMS);
        SQLUSMALLINT param_index = 1;
        for (size_t r = row_begin; r < row_end; ++r) {
            for (size_t c = 0; c < ncols; ++c) {
                bind_typed_cell(stmt, param_index, batch.columns[c], r);
                ++param_index;
            }
        }
    }

private:
    std::vector<int64_t> m_int_storage;
    std::vector<double> m_dbl_storage;
    std::vector<std::string> m_str_storage;
    std::vector<SQLLEN> m_indicators;

    void reset() {
        m_int_storage.clear();
        m_dbl_storage.clear();
        m_str_storage.clear();
        m_indicators.clear();
    }

    void bind_one(SQLHSTMT stmt, SQLUSMALLINT pos, const core::CellValue &val) {
        struct Binder {
            CellStatementBinder &self;
            SQLHSTMT stmt;
            SQLUSMALLINT pos;

            void operator()(const core::Null &) const {
                self.bind_null(stmt, pos);
            }
            void operator()(bool b) const {
                self.bind_int(stmt, pos, b ? 1 : 0);
            }
            void operator()(int64_t i) const {
                self.bind_int(stmt, pos, i);
            }
            void operator()(double d) const {
                self.bind_double(stmt, pos, d);
            }
            void operator()(const std::string &s) const {
                self.bind_string(stmt, pos, s);
            }
        };
        std::visit(Binder{*this, stmt, pos}, val);
    }

    void bind_typed_cell(SQLHSTMT stmt, SQLUSMALLINT pos,
                         const core::TypedColumnBatch::Column &col, size_t row) {
        // Check null mask first.
        if (col.has_nulls() && col.null_mask[row]) {
            bind_null(stmt, pos);
            return;
        }
        switch (col.kind) {
        case core::TypedColumnBatch::Kind::I64:
            bind_int(stmt, pos, col.i64_data[row]);
            break;
        case core::TypedColumnBatch::Kind::F64:
            bind_double(stmt, pos, col.f64_data[row]);
            break;
        case core::TypedColumnBatch::Kind::BOOL:
            bind_int(stmt, pos, col.bool_data[row] ? 1 : 0);
            break;
        case core::TypedColumnBatch::Kind::STR:
            bind_string(stmt, pos, col.str_data[row]);
            break;
        }
    }

    void bind_null(SQLHSTMT stmt, SQLUSMALLINT pos) {
        m_indicators.push_back(SQL_NULL_DATA);
        static const char *dummy = "";
        SQLRETURN ret = SQLBindParameter(
            stmt, pos, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            0, 0, (SQLPOINTER)dummy, 0, &m_indicators.back());
        if (!SQL_SUCCEEDED(ret)) {
            throw std::runtime_error("SQLBindParameter(NULL) failed");
        }
    }

    void bind_int(SQLHSTMT stmt, SQLUSMALLINT pos, int64_t value) {
        m_int_storage.push_back(value);
        m_indicators.push_back(0);
        SQLRETURN ret = SQLBindParameter(
            stmt, pos, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT,
            0, 0, &m_int_storage.back(), 0, &m_indicators.back());
        if (!SQL_SUCCEEDED(ret)) {
            throw std::runtime_error("SQLBindParameter(int64) failed");
        }
    }

    void bind_double(SQLHSTMT stmt, SQLUSMALLINT pos, double value) {
        m_dbl_storage.push_back(value);
        m_indicators.push_back(0);
        SQLRETURN ret = SQLBindParameter(
            stmt, pos, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE,
            0, 0, &m_dbl_storage.back(), 0, &m_indicators.back());
        if (!SQL_SUCCEEDED(ret)) {
            throw std::runtime_error("SQLBindParameter(double) failed");
        }
    }

    void bind_string(SQLHSTMT stmt, SQLUSMALLINT pos, const std::string &value) {
        m_str_storage.push_back(value);
        m_indicators.push_back(static_cast<SQLLEN>(m_str_storage.back().size()));
        SQLRETURN ret = SQLBindParameter(
            stmt, pos, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            m_indicators.back(), 0,
            (SQLPOINTER)m_str_storage.back().c_str(), 0, &m_indicators.back());
        if (!SQL_SUCCEEDED(ret)) {
            throw std::runtime_error("SQLBindParameter(string) failed");
        }
    }
};

#endif // PYGIM_HAVE_ODBC

} // namespace pygim::mssql::detail
