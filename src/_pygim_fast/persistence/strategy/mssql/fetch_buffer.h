// repository/strategy/mssql/fetch_buffer.h
// Column-oriented fetch buffers for ODBC block cursors.

#pragma once

#include "sql_type_map.h"
#include "odbc_error.h"   // includes <sql.h>, <sqlext.h>, undefs BOOL/INT

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pygim::strategy::mssql {

/// Per-column fetch buffer: raw data + null indicators + type metadata.
struct FetchBuffer {
    std::vector<uint8_t> data;
    std::vector<SQLLEN>  indicators;
    TypeMapping          mapping;
    std::string          name;
};

/// Complete set of column buffers for a block-cursor fetch loop.
struct FetchBufferSet {
    std::vector<FetchBuffer> columns;
    SQLULEN                  rows_fetched{0};   // written by SQL_ATTR_ROWS_FETCHED_PTR
    int64_t                  block_size{0};

    /// Allocate data and indicator buffers for every column.
    [[nodiscard]] static FetchBufferSet
    allocate(const std::vector<std::pair<std::string, TypeMapping>>& col_info,
             int64_t block_size) {
        FetchBufferSet fbs;
        fbs.block_size = block_size;
        fbs.columns.reserve(col_info.size());

        for (const auto& [name, mapping] : col_info) {
            FetchBuffer buf;
            buf.data.resize(static_cast<std::size_t>(mapping.element_width * block_size));
            buf.indicators.resize(static_cast<std::size_t>(block_size));
            buf.mapping = mapping;
            buf.name    = name;
            fbs.columns.push_back(std::move(buf));
        }
        return fbs;
    }

    /// Bind all column buffers to the statement via SQLBindCol.
    void bind(SQLHSTMT stmt) {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            auto& col = columns[i];
            // ODBC columns are 1-indexed
            SQLRETURN ret = SQLBindCol(
                stmt,
                static_cast<SQLUSMALLINT>(i + 1),
                col.mapping.c_type,
                col.data.data(),
                col.mapping.element_width,
                col.indicators.data());
            odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt,
                                 "FetchBufferSet::bind: SQLBindCol");
        }
    }
};

/// Convert ODBC indicator array to a validity bitmap (1 = non-null, 0 = null).
/// Branchless: compiles to cmp + setne, vectorisable by the backend.
inline void indicators_to_valid_bytes(const SQLLEN* indicators,
                                      int64_t       count,
                                      uint8_t*      out) {
    for (int64_t i = 0; i < count; ++i) {
        out[i] = static_cast<uint8_t>(indicators[i] != SQL_NULL_DATA);
    }
}

} // namespace pygim::strategy::mssql
