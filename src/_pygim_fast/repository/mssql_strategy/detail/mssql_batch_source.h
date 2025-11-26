#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace pygim::detail {

namespace py = pybind11;

template <typename T>
concept BatchRowSource = requires(const T &src, size_t row, size_t col) {
    { src.row_count() } -> std::same_as<size_t>;
    { src.column_count() } -> std::same_as<size_t>;
    { src.get_value(row, col) } -> std::convertible_to<py::object>;
};

class IterableRowSource {
public:
    IterableRowSource(py::object rows, size_t expected_columns);

    [[nodiscard]] size_t row_count() const noexcept { return rows_.size(); }
    [[nodiscard]] size_t column_count() const noexcept { return expected_columns_; }
    [[nodiscard]] py::object get_value(size_t row, size_t col) const;

private:
    void consume_rows(py::object rows);

    size_t expected_columns_{0};
    std::vector<py::tuple> rows_;
};

class PolarsRowSource {
public:
    PolarsRowSource(py::object df, std::vector<std::string> columns);

    [[nodiscard]] size_t row_count() const noexcept { return row_count_; }
    [[nodiscard]] size_t column_count() const noexcept { return columns_.size(); }
    [[nodiscard]] py::object get_value(size_t row, size_t col) const;

private:
    enum class ColumnKind {
        I64,
        F64,
        BOOL_T,
        STR
    };

    struct ColumnView {
        ColumnKind kind;
        py::array array;
        std::vector<std::string> strings;
    };

    ColumnView build_column_view(const py::object &series, const std::string &dtype, size_t total_rows) const;

    py::object df_;
    std::vector<std::string> columns_;
    size_t row_count_{0};
    std::vector<ColumnView> views_;
};

} // namespace pygim::detail
