#include "mssql_batch_source.h"

#include <stdexcept>

#include "../../../utils/logging.h"

namespace pygim::detail {

IterableRowSource::IterableRowSource(py::object rows, size_t expected_columns)
    : expected_columns_(expected_columns) {
    PYGIM_SCOPE_LOG_TAG("repo.batch_source");
    consume_rows(std::move(rows));
}

void IterableRowSource::consume_rows(py::object rows) {
    PYGIM_SCOPE_LOG_TAG("repo.batch_source");
    for (py::handle handle : rows) {
        py::object row_obj = py::reinterpret_borrow<py::object>(handle);
        if (!py::isinstance<py::sequence>(row_obj)) {
            throw std::runtime_error("Each row must be a sequence");
        }
        py::sequence seq = row_obj.cast<py::sequence>();
        if (static_cast<size_t>(py::len(seq)) != expected_columns_) {
            throw std::runtime_error("Row length mismatch");
        }
        rows_.push_back(py::reinterpret_borrow<py::tuple>(py::tuple(seq)));
    }
}

py::object IterableRowSource::get_value(size_t row, size_t col) const {
    PYGIM_SCOPE_LOG_TAG("repo.batch_source");
    if (row >= rows_.size() || col >= expected_columns_) {
        throw std::out_of_range("IterableRowSource index out of range");
    }
    return rows_[row][static_cast<py::ssize_t>(col)];
}

PolarsRowSource::PolarsRowSource(py::object df, std::vector<std::string> columns)
    : df_(std::move(df)), columns_(std::move(columns)) {
    PYGIM_SCOPE_LOG_TAG("repo.batch_source");
    row_count_ = df_.attr("height").cast<size_t>();
    views_.reserve(columns_.size());
    for (const auto &col_name : columns_) {
        py::object series = df_.attr("get_column")(col_name);
        std::string dtype = py::str(series.attr("dtype"));
        views_.push_back(build_column_view(series, dtype, row_count_));
    }
}

PolarsRowSource::ColumnView PolarsRowSource::build_column_view(const py::object &series,
                                                               const std::string &dtype,
                                                               size_t total_rows) const {
    PYGIM_SCOPE_LOG_TAG("repo.batch_source");
    if (dtype.find("Int") != std::string::npos) {
        py::array arr = series.attr("to_numpy")().cast<py::array>();
        return ColumnView{ColumnKind::I64, arr, {}};
    }
    if (dtype.find("Float") != std::string::npos) {
        py::array arr = series.attr("to_numpy")().cast<py::array>();
        return ColumnView{ColumnKind::F64, arr, {}};
    }
    if (dtype.find("Boolean") != std::string::npos) {
        py::array arr = series.attr("to_numpy")().cast<py::array>();
        return ColumnView{ColumnKind::BOOL_T, arr, {}};
    }
    py::list lst = series.attr("to_list")().cast<py::list>();
    std::vector<std::string> strings;
    strings.reserve(total_rows);
    for (py::handle h : lst) {
        strings.emplace_back(py::str(h));
    }
    return ColumnView{ColumnKind::STR, py::array(), std::move(strings)};
}

py::object PolarsRowSource::get_value(size_t row, size_t col) const {
    PYGIM_SCOPE_LOG_TAG("repo.batch_source");
    if (row >= row_count_ || col >= columns_.size()) {
        throw std::out_of_range("PolarsRowSource index out of range");
    }
    const ColumnView &view = views_[col];
    switch (view.kind) {
    case ColumnKind::I64: {
        auto *ptr = reinterpret_cast<const long long *>(view.array.data());
        return py::int_(ptr[row]);
    }
    case ColumnKind::F64: {
        auto *ptr = reinterpret_cast<const double *>(view.array.data());
        return py::float_(ptr[row]);
    }
    case ColumnKind::BOOL_T: {
        auto *ptr = reinterpret_cast<const uint8_t *>(view.array.data());
        return py::bool_(ptr[row] != 0);
    }
    case ColumnKind::STR:
    default:
        return py::str(view.strings[row]);
    }
}

} // namespace pygim::detail
