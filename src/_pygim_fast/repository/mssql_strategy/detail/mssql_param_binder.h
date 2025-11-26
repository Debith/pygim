#pragma once

#include <vector>

#include <sql.h>

#include <pybind11/pybind11.h>

#include "../mssql_strategy.h"
#include "mssql_batch_source.h"

namespace pygim::detail {

namespace py = pybind11;

class ParameterBinder {
public:
    ParameterBinder() = default;

    template <BatchRowSource Src>
    void bind_all(SQLHSTMT stmt, const Src &source, size_t row_begin, size_t row_end);

private:
    void reset();
    void bind_null(SQLHSTMT stmt, SQLUSMALLINT param_index);
    void bind_int(SQLHSTMT stmt, SQLUSMALLINT param_index, long long value);
    void bind_double(SQLHSTMT stmt, SQLUSMALLINT param_index, double value);
    void bind_string(SQLHSTMT stmt, SQLUSMALLINT param_index, const std::string &value);

    std::vector<long long> int_storage_;
    std::vector<double> double_storage_;
    std::vector<std::string> string_storage_;
    std::vector<SQLLEN> indicator_storage_;
};

template <BatchRowSource Src>
void ParameterBinder::bind_all(SQLHSTMT stmt, const Src &source, size_t row_begin, size_t row_end) {
    reset();
    if (row_end <= row_begin) {
        return;
    }
    const size_t columns = source.column_count();
    if (columns == 0) {
        return;
    }
    const size_t total_params = (row_end - row_begin) * columns;
    int_storage_.reserve(total_params);
    double_storage_.reserve(total_params);
    string_storage_.reserve(total_params);
    indicator_storage_.reserve(total_params);
    SQLFreeStmt(stmt, SQL_RESET_PARAMS);
    SQLUSMALLINT param_index = 1;
    for (size_t row = row_begin; row < row_end; ++row) {
        for (size_t col = 0; col < columns; ++col) {
            py::object value = source.get_value(row, col);
            if (value.is_none()) {
                bind_null(stmt, param_index);
            } else if (py::isinstance<py::int_>(value) || py::isinstance<py::bool_>(value)) {
                long long ival = value.cast<long long>();
                bind_int(stmt, param_index, ival);
            } else if (py::isinstance<py::float_>(value)) {
                double dval = value.cast<double>();
                bind_double(stmt, param_index, dval);
            } else {
                std::string sval = py::str(value);
                bind_string(stmt, param_index, sval);
            }
            ++param_index;
        }
    }
}

} // namespace pygim::detail
