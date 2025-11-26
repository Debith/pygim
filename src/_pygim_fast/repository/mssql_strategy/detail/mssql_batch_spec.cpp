#include "mssql_batch_spec.h"

#include <algorithm>
#include <stdexcept>

namespace pygim::detail {

namespace {
constexpr int kDefaultParamLimit = 2090;
constexpr int kDefaultBatchSize = 500;

bool contains_column(const std::vector<std::string> &columns, const std::string &needle) {
    return std::find(columns.begin(), columns.end(), needle) != columns.end();
}
} // namespace

BatchSpec::BatchSpec(std::string table,
                     std::vector<std::string> columns,
                     std::string key_column,
                     std::string table_hint,
                     int batch_size,
                     int param_limit)
    : table_(std::move(table)),
      columns_(std::move(columns)),
      key_column_(std::move(key_column)),
      table_hint_(std::move(table_hint)),
      batch_size_(batch_size > 0 ? batch_size : kDefaultBatchSize),
      param_limit_(param_limit > 0 ? param_limit : kDefaultParamLimit) {
    if (table_.empty()) {
        throw std::invalid_argument("BatchSpec: table name cannot be empty");
    }
    if (columns_.empty()) {
        throw std::invalid_argument("BatchSpec: columns cannot be empty");
    }
    if (key_column_.empty()) {
        throw std::invalid_argument("BatchSpec: key_column cannot be empty");
    }
    if (!contains_column(columns_, key_column_)) {
        throw std::invalid_argument("BatchSpec: key_column must be present in columns");
    }
}

size_t BatchSpec::column_count() const noexcept {
    return columns_.size();
}

int BatchSpec::rows_per_stmt() const noexcept {
    const int cols = static_cast<int>(column_count());
    if (cols <= 0) {
        return 0;
    }
    const int by_limit = std::max(1, param_limit_ / cols);
    return std::max(1, std::min(batch_size_, by_limit));
}

TableSpec BatchSpec::to_table_spec() const {
    TableSpec spec;
    spec.name = table_;
    spec.columns = columns_;
    spec.key_column = key_column_;
    spec.table_hint = table_hint_;
    return spec;
}

} // namespace pygim::detail
