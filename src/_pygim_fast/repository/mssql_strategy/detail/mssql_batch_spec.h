#pragma once

#include <string>
#include <vector>

#include "value_objects.h"

namespace pygim::detail {

class BatchSpec {
public:
    BatchSpec(std::string table,
              std::vector<std::string> columns,
              std::string key_column,
              std::string table_hint,
              int batch_size,
              int param_limit = 2090);

    [[nodiscard]] size_t column_count() const noexcept;
    [[nodiscard]] int rows_per_stmt() const noexcept;

    [[nodiscard]] const std::string &table() const noexcept { return table_; }
    [[nodiscard]] const std::vector<std::string> &columns() const noexcept { return columns_; }
    [[nodiscard]] const std::string &key_column() const noexcept { return key_column_; }
    [[nodiscard]] const std::string &table_hint() const noexcept { return table_hint_; }

    [[nodiscard]] int batch_size() const noexcept { return batch_size_; }
    [[nodiscard]] int param_limit() const noexcept { return param_limit_; }

    [[nodiscard]] TableSpec to_table_spec() const;

private:
    std::string table_;
    std::vector<std::string> columns_;
    std::string key_column_;
    std::string table_hint_;
    int batch_size_;
    int param_limit_;
};

} // namespace pygim::detail
