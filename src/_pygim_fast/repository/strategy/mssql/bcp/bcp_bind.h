#pragma once
// BCP column binding: orchestration layer.
// Delegates per-type bind/rebind logic to dedicated dispatch headers;
// provides bind_columns, rebind_columns, classify_columns, setup_staging.

#include "bcp_bind_dispatch.h"
#include "bcp_rebind_dispatch.h"

namespace pygim::strategy::mssql::bcp {

// ── Column dispatcher ───────────────────────────────────────────────────────
/// Bind all columns in a RecordBatch to BCP.
/// Uses a compile-time dispatch table for O(1) type → bind-function lookup.
/// @throws std::runtime_error for unsupported Arrow types.
inline std::vector<ColumnBinding> bind_columns(
    const BcpApi& bcp, SQLHDBC dbc,
    const std::shared_ptr<arrow::RecordBatch>& batch)
{
    const int n = batch->num_columns();
    std::vector<ColumnBinding> bindings;
    bindings.reserve(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        auto field = batch->schema()->field(i);
        auto array = batch->column(i);
        const int ordinal = i + 1;
        const auto id = field->type()->id();

        auto fn = (id < arrow::Type::MAX_ID) ? bind_dispatch[id] : nullptr;
        if (!fn) [[unlikely]]
            throw std::runtime_error("Unsupported Arrow type: " + field->type()->ToString());

        bindings.push_back(fn(bcp, dbc, array, ordinal));
    }
    return bindings;
}

// ── Column classification ───────────────────────────────────────────────────
/// Split bindings into fixed-width and string/binary columns.
/// Used by the row loop to choose fast path (no nulls, fixed only)
/// vs general path (nulls or variable-length).
inline ClassifiedColumns classify_columns(std::vector<ColumnBinding>& bindings) {
    ClassifiedColumns c;
    c.fixed.reserve(bindings.size());
    c.string.reserve(bindings.size());
    for (auto& b : bindings) {
        (b.is_string ? c.string : c.fixed).push_back(&b);
        if (b.has_nulls) c.any_has_nulls = true;
    }
    return c;
}

// ── Fast rebind ─────────────────────────────────────────────────────────────
/// Update Arrow data pointers in existing bindings without re-calling bcp_bind.
/// Uses compile-time dispatch table for O(1) lookup.
inline void rebind_columns(
    std::vector<ColumnBinding>& bindings,
    ClassifiedColumns& classified,
    const std::shared_ptr<arrow::RecordBatch>& batch)
{
    const int n = batch->num_columns();
    assert(static_cast<int>(bindings.size()) == n);

    classified.any_has_nulls = false;

    for (int i = 0; i < n; ++i) {
        auto& b = bindings[static_cast<size_t>(i)];
        auto array = batch->column(i);

        auto fn = (b.arrow_type < arrow::Type::MAX_ID)
                    ? rebind_dispatch[b.arrow_type] : nullptr;
        if (fn) [[likely]]
            fn(b, array);
        else
            rebind_detail::reset_string_fields(b, array);  // defensive; bind_columns rejects unknown types

        if (b.has_nulls) classified.any_has_nulls = true;
    }
}

// ── Staging buffer ──────────────────────────────────────────────────────────
/// Allocate a single contiguous staging buffer for all fixed-width columns.
/// Each column's bcp_colptr is pointed into this buffer at its offset, so the
/// row loop only needs memcpy into the buffer (no per-column bcp_colptr call).
inline std::vector<uint8_t> setup_staging(
    const BcpApi& bcp, SQLHDBC dbc,
    std::span<ColumnBinding*> fixed_cols)
{
    size_t total = 0;
    for (auto* bp : fixed_cols) {
        bp->staging_offset = total;
        total += bp->value_stride;
    }
    std::vector<uint8_t> buf(total);
    for (auto* bp : fixed_cols) {
        bcp.colptr(dbc, buf.data() + bp->staging_offset, bp->ordinal);
        bcp.collen(dbc, static_cast<DBINT>(bp->value_stride), bp->ordinal);
    }
    return buf;
}

} // namespace pygim::strategy::mssql::bcp
