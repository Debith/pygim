// ArrowLoadStrategy: builds Arrow RecordBatch directly during ODBC fetch.
//
// Eliminates the intermediate row-major ResultSet by appending values
// directly into columnar Arrow builders in a single pass.
//
// This header includes Arrow headers — only include it in translation
// units that need Arrow materialization.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/api.h>
#include <arrow/builder.h>

#include "load_strategy.h"

namespace pygim::core {

/// LoadStrategy that materializes SQL results into an Arrow RecordBatch.
///
/// Usage:
///   ArrowLoadStrategy strat;
///   mssql_strategy.load(query, strat);   // drives begin→append→finish
///   auto batch = strat.result();         // shared_ptr<RecordBatch>
class ArrowLoadStrategy : public LoadStrategy {
public:
    void begin(const std::vector<ColumnInfo> &columns) override {
        m_nrows = 0;
        m_col_names.clear();
        m_builders.clear();
        m_col_names.reserve(columns.size());
        m_builders.reserve(columns.size());

        for (const auto &col : columns) {
            m_col_names.push_back(col.name);
            switch (col.type) {
            case ColumnType::Bool:
                m_builders.emplace_back(std::make_unique<arrow::BooleanBuilder>());
                break;
            case ColumnType::Int64:
                m_builders.emplace_back(std::make_unique<arrow::Int64Builder>());
                break;
            case ColumnType::Double:
                m_builders.emplace_back(std::make_unique<arrow::DoubleBuilder>());
                break;
            case ColumnType::String:
                m_builders.emplace_back(std::make_unique<arrow::StringBuilder>());
                break;
            }
        }
    }

    void end_row() override { ++m_nrows; }

    void finish() override {
        std::vector<std::shared_ptr<arrow::Field>> fields;
        std::vector<std::shared_ptr<arrow::Array>> arrays;
        fields.reserve(m_builders.size());
        arrays.reserve(m_builders.size());

        for (size_t i = 0; i < m_builders.size(); ++i) {
            auto [arr, dtype] = finish_builder(m_builders[i]);
            fields.push_back(arrow::field(m_col_names[i], std::move(dtype)));
            arrays.push_back(std::move(arr));
        }

        auto schema = arrow::schema(std::move(fields));
        m_batch = arrow::RecordBatch::Make(
            std::move(schema), static_cast<int64_t>(m_nrows), std::move(arrays));
    }

    void append_null(size_t col) override {
        std::visit([](auto &b) {
            auto s = b->AppendNull();
            if (!s.ok()) throw std::runtime_error(s.ToString());
        }, m_builders[col]);
    }

    void append_int64(size_t col, int64_t val) override {
        auto s = std::get<Int64Ptr>(m_builders[col])->Append(val);
        if (!s.ok()) throw std::runtime_error(s.ToString());
    }

    void append_double(size_t col, double val) override {
        auto s = std::get<DoublePtr>(m_builders[col])->Append(val);
        if (!s.ok()) throw std::runtime_error(s.ToString());
    }

    void append_bool(size_t col, bool val) override {
        auto s = std::get<BoolPtr>(m_builders[col])->Append(val);
        if (!s.ok()) throw std::runtime_error(s.ToString());
    }

    void append_string(size_t col, std::string_view val) override {
        auto s = std::get<StringPtr>(m_builders[col])->Append(val);
        if (!s.ok()) throw std::runtime_error(s.ToString());
    }

    /// Retrieve the built RecordBatch after finish().
    /// Returns nullptr if finish() has not been called or no rows were fetched.
    std::shared_ptr<arrow::RecordBatch> result() const { return m_batch; }

private:
    using BoolPtr   = std::unique_ptr<arrow::BooleanBuilder>;
    using Int64Ptr  = std::unique_ptr<arrow::Int64Builder>;
    using DoublePtr = std::unique_ptr<arrow::DoubleBuilder>;
    using StringPtr = std::unique_ptr<arrow::StringBuilder>;
    using Builder   = std::variant<BoolPtr, Int64Ptr, DoublePtr, StringPtr>;

    std::vector<std::string> m_col_names;
    std::vector<Builder> m_builders;
    size_t m_nrows{0};
    std::shared_ptr<arrow::RecordBatch> m_batch;

    static std::pair<std::shared_ptr<arrow::Array>, std::shared_ptr<arrow::DataType>>
    finish_builder(Builder &b) {
        return std::visit([](auto &builder)
            -> std::pair<std::shared_ptr<arrow::Array>, std::shared_ptr<arrow::DataType>> {
            auto res = builder->Finish();
            if (!res.ok()) throw std::runtime_error(res.status().ToString());
            auto arr = *res;
            return {arr, arr->type()};
        }, b);
    }
};

} // namespace pygim::core
