#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "../../generic/query.h"

namespace pygim::detail {

namespace py = pybind11;

struct PolarsTag {
};

struct IterableTag {
};

struct TableSpec {
    std::string name;
    std::vector<std::string> columns;
    std::optional<std::string> key_column;
    std::string table_hint;

    [[nodiscard]] size_t column_count() const noexcept { return columns.size(); }
    [[nodiscard]] bool has_key() const noexcept { return key_column.has_value(); }
};

struct BatchOptions {
    int batch_size{1000};
    int param_limit{2090};

    [[nodiscard]] int effective_rows_per_statement(size_t column_count) const noexcept {
        const int cols = static_cast<int>(column_count);
        if (cols <= 0) {
            return 0;
        }
        const int per_stmt = std::max(1, std::min(batch_size, param_limit / cols));
        return per_stmt;
    }
};

struct QueryEnvelope {
    Query query;
    std::string label;
};

struct MergePlan {
    TableSpec spec;
    int rows_per_statement{0};
};

template <typename SourceTag>
struct BatchDescriptor {
    TableSpec spec;
    BatchOptions options;
    py::object rows;
    int rows_hint{-1};
    SourceTag tag{};

    BatchDescriptor(TableSpec spec_, BatchOptions opts_, py::object rows_, int hint = -1)
        : spec(std::move(spec_)), options(opts_), rows(std::move(rows_)), rows_hint(hint) {}
};

} // namespace pygim::detail
