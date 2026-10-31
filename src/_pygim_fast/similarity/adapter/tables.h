#pragma once
// similarity/adapter/tables.h — a corpus's units and a sweep's pairs as Arrow
// RecordBatches.
//
// ADAPTER layer (Arrow, no Python): built with the GIL released, handed to
// Python as pyarrow Tables through utils/arrow_stream.h. Columns are typed and
// columnar — a pair is a row, not an object — and every string is stored
// once: the qualname and origin columns of pairs are dictionary-encoded
// (int32 indices into the distinct qualnames / origins; both a_ and b_
// columns share one dictionary), which polars reads as a Categorical. A
// pairs row is therefore integers and a float only, 56 bytes and a bit, written
// straight into Arrow buffers — what keeps a 100M-row result at memory
// bandwidth. Unit ids are the corpus's insertion indices, so `pairs.a` and
// `pairs.b` join `units.id`.
//
//     units:  id u32 | kind dict<i32,utf8> | qualname utf8 | origin dict<i32,utf8> |
//             line u32 | owner u32 (null outside a class) | nodes u32 | weight u64
//     pairs:  a u32 | b u32 | a_qualname dict | a_origin dict | a_line u32 |
//             b_qualname dict | b_origin dict | b_line u32 |
//             score f64 | shared u64 | total u64 | exact bool
//
// On the core.h worked example (A, B, C as f, g, h in one file "m.py"),
// pairs(function) is three rows, the first (0, 1, "f", "m.py", 1, "g", "m.py",
// 2, 1.0, s, 2s, true).
//
// Both pairs entry points take a column selection (pair_col): every column by
// default, or e.g. (a, b, score) — 16 bytes a row instead of 56 — when the
// names, files and lines are joined from units() later or not needed.
//
// Cost: one pass over the rows per column; strings copied once into the batch.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <arrow/api.h>

#include "../../mapping/intern.h"
#include "../../utils/string_list.h"
#include "../core.h"

namespace pygim::similarity::tables {

/// The rows of a result's batch unless the caller says otherwise: a million, 56 MiB with every
/// column — few enough batches that their overhead never shows, small enough to stream.
inline constexpr std::size_t default_batch_rows = std::size_t{1} << 20;

namespace detail {

inline void check(const arrow::Status& st) {
    if (!st.ok()) throw std::runtime_error("similarity: building an Arrow table failed: " + st.ToString());
}
template <class T>
T value(arrow::Result<T> r) {
    check(r.status());
    return std::move(r).ValueOrDie();
}

/// The distinct strings of `values` as one utf8 array, and each value's index
/// into it (first appearance order).
class dictionary {
public:
    /// `values`: any range of strings or string views. Interned (mapping/intern.h: dense ids in
    /// first-seen order, which are the indices): LLVM's 156,089 qualnames in 9-11 ms where an
    /// unordered_map of views took 11-18 ms, the same ids (2026-10-28).
    template <class Strings>
    explicit dictionary(const Strings& values) {
        mapping::basic_hashed_interner<std::uint32_t> seen;
        std::size_t bytes = 0;
        for (const std::string_view v : values) bytes += v.size();
        seen.reserve(values.size(), bytes);
        m_index.reserve(values.size());
        for (const std::string_view v : values) m_index.push_back(static_cast<std::int32_t>(seen.intern(v)));
        arrow::StringBuilder b;
        check(b.Reserve(static_cast<std::int64_t>(seen.size())));
        for (std::uint32_t id = 0; id < seen.size(); ++id) check(b.Append(seen[id]));
        m_values = value(b.Finish());
    }

    [[nodiscard]] const std::shared_ptr<arrow::Array>& values() const noexcept { return m_values; }
    /// The index of value `i` into values().
    [[nodiscard]] std::int32_t index_of(std::size_t i) const noexcept { return m_index[i]; }
    /// Every value's index, moved out: a dictionary column's indices by unit id.
    [[nodiscard]] std::vector<std::int32_t> release_indices() && noexcept { return std::move(m_index); }

private:
    std::shared_ptr<arrow::Array> m_values;
    std::vector<std::int32_t> m_index;
};

/// A dictionary column: `rows` indices into `dict`.
inline std::shared_ptr<arrow::Array> encoded(const std::shared_ptr<arrow::Array>& dict,
                                             const std::vector<std::int32_t>& rows) {
    arrow::Int32Builder ib;
    check(ib.AppendValues(rows));
    auto indices = value(ib.Finish());
    return value(arrow::DictionaryArray::FromArrays(arrow::dictionary(arrow::int32(), arrow::utf8()), indices, dict));
}

template <class Builder, class T>
std::shared_ptr<arrow::Array> column(const std::vector<T>& v) {
    Builder b;
    check(b.AppendValues(v));
    return value(b.Finish());
}

template <class Strings>
std::shared_ptr<arrow::Array> strings(std::size_t n, Strings&& at) {
    arrow::StringBuilder b;
    check(b.Reserve(static_cast<std::int64_t>(n)));
    for (std::size_t i = 0; i < n; ++i) check(b.Append(at(i)));
    return value(b.Finish());
}

inline std::shared_ptr<arrow::Field> dict_field(const char* name) {
    return arrow::field(name, arrow::dictionary(arrow::int32(), arrow::utf8()), false);
}

}  // namespace detail

/// The units of `c` (optionally only one kind), one row each, in insertion order.
template <class Corpus>
[[nodiscard]] std::shared_ptr<arrow::RecordBatch> units(const Corpus& c, std::optional<unit_kind> kind) {
    using namespace detail;
    const auto& us = c.units();
    std::vector<std::int64_t> owner(us.size(), -1);
    for (std::uint32_t i = 0; i < us.size(); ++i) {
        for (std::uint32_t m : us[i].methods()) owner[m] = i;
    }
    std::vector<std::uint32_t> rows;
    for (std::uint32_t i = 0; i < us.size(); ++i) {
        if (!kind || us[i].kind() == *kind) rows.push_back(i);
    }

    const dictionary origins(c.origins());
    arrow::StringBuilder kinds_b;
    for (std::string_view name : unit_kind_names) check(kinds_b.Append(name.data(), static_cast<std::int32_t>(name.size())));
    const auto kinds = value(kinds_b.Finish());

    std::vector<std::int32_t> kind_ix, origin_ix;
    std::vector<std::uint32_t> line, nodes;
    std::vector<std::uint64_t> weight;
    arrow::UInt32Builder owner_b;
    for (std::uint32_t i : rows) {
        kind_ix.push_back(static_cast<std::int32_t>(number_of(us[i].kind())));   // the dictionary is unit_kind_names
        origin_ix.push_back(origins.index_of(us[i].origin()));
        line.push_back(us[i].line());
        nodes.push_back(static_cast<std::uint32_t>(c.nodes(i)));
        weight.push_back(us[i].weight());
        check(owner[i] < 0 ? owner_b.AppendNull() : owner_b.Append(static_cast<std::uint32_t>(owner[i])));
    }

    auto schema = arrow::schema({
        arrow::field("id", arrow::uint32(), false),
        dict_field("kind"),
        arrow::field("qualname", arrow::utf8(), false),
        dict_field("origin"),
        arrow::field("line", arrow::uint32(), false),
        arrow::field("owner", arrow::uint32(), true),
        arrow::field("nodes", arrow::uint32(), false),
        arrow::field("weight", arrow::uint64(), false),
    });
    return arrow::RecordBatch::Make(schema, static_cast<std::int64_t>(rows.size()), {
        column<arrow::UInt32Builder>(rows),
        encoded(kinds, kind_ix),
        strings(rows.size(), [&](std::size_t r) { return std::string_view(us[rows[r]].qualname()); }),
        encoded(origins.values(), origin_ix),
        column<arrow::UInt32Builder>(line),
        value(owner_b.Finish()),
        column<arrow::UInt32Builder>(nodes),
        column<arrow::UInt64Builder>(weight),
    });
}

/// The columns a pairs result can have, in their default order.
enum class pair_col : std::uint8_t { a, b, a_qualname, a_origin, a_line, b_qualname, b_origin, b_line, score, shared, total, exact };

/// What a pairs column holds: a unit id or a line (uint32), an index into the names' or the files'
/// dictionary (int32), a score (float64), a count (uint64), or a bit.
enum class pair_type : std::uint8_t { id, name, origin, score, count, bit };

/// One pairs column's facts, all in one place: its name, what it holds, and whether it reads the
/// units' columns (unit_columns).
class pair_column {
public:
    constexpr pair_column(std::string_view name, pair_type type, bool reads_units = false) noexcept
        : m_name(name), m_type(type), m_reads_units(reads_units) {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] constexpr pair_type type() const noexcept { return m_type; }
    /// Whether it shows a unit's name, file or line, read from the units' columns.
    [[nodiscard]] constexpr bool reads_units() const noexcept { return m_reads_units; }
    /// The bytes a row takes in its buffer (a bit: none; its byte holds eight rows).
    [[nodiscard]] constexpr std::size_t width() const noexcept {
        return m_type == pair_type::score || m_type == pair_type::count ? 8 : m_type == pair_type::bit ? 0 : 4;
    }

private:
    std::string_view m_name;
    pair_type m_type;
    bool m_reads_units;
};

/// Every pairs column by its pair_col.
inline constexpr std::array<pair_column, 12> pair_columns_table{{
    {"a", pair_type::id},
    {"b", pair_type::id},
    {"a_qualname", pair_type::name, true},
    {"a_origin", pair_type::origin, true},
    {"a_line", pair_type::id, true},
    {"b_qualname", pair_type::name, true},
    {"b_origin", pair_type::origin, true},
    {"b_line", pair_type::id, true},
    {"score", pair_type::score},
    {"shared", pair_type::count},
    {"total", pair_type::count},
    {"exact", pair_type::bit},
}};
static_assert(pair_columns_table.size() == static_cast<std::size_t>(pair_col::exact) + 1, "a column for every pair_col");
[[nodiscard]] constexpr const pair_column& column_of(pair_col c) noexcept { return pair_columns_table[static_cast<std::size_t>(c)]; }
/// Whether column `c` shows a unit's name, file or line: what needs the units' columns.
[[nodiscard]] constexpr bool shows_units(pair_col c) noexcept { return column_of(c).reads_units(); }
static_assert(column_of(pair_col::b_origin).name() == "b_origin" && column_of(pair_col::total).width() == 8 && shows_units(pair_col::a_line) &&
              !shows_units(pair_col::score));

inline constexpr auto pair_column_names = [] {
    std::array<std::string_view, pair_columns_table.size()> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = pair_columns_table[i].name();
    return out;
}();

/// The columns named by `names`, in that order; every column when there is
/// no selection. An unknown, repeated or empty selection is an
/// invalid_argument naming the offender and the known columns.
[[nodiscard]] inline std::vector<pair_col> select_pair_columns(const std::optional<std::vector<std::string>>& names) {
    std::vector<pair_col> out;
    if (!names) {
        for (std::size_t i = 0; i < pair_column_names.size(); ++i) out.push_back(static_cast<pair_col>(i));
        return out;
    }
    auto known = [] { return joined(pair_column_names, ", "); };
    if (names->empty()) throw std::invalid_argument("similarity: columns must name at least one column (" + known() + ")");
    for (const std::string& n : *names) {
        const auto it = std::find(pair_column_names.begin(), pair_column_names.end(), n);
        if (it == pair_column_names.end())
            throw std::invalid_argument("similarity: unknown pairs column '" + n + "' (known: " + known() + ")");
        const auto col = static_cast<pair_col>(it - pair_column_names.begin());
        if (std::find(out.begin(), out.end(), col) != out.end())
            throw std::invalid_argument("similarity: pairs column '" + n + "' requested twice");
        out.push_back(col);
    }
    return out;
}

class pair_batch;

/// Every unit's name, file and line as a pairs result's columns take them: the two dictionaries
/// (the distinct qualnames, the distinct origins), each unit's index into each, and each unit's
/// line. The same for every result over one corpus state — so every result of an index shares
/// one dictionary, and a table of the best N is the head of the whole ranking's — and the index
/// keeps it (index_state::unit_columns): built per result, it took 25-30 ms of LLVM's 83-98 ms
/// ranked query and 16 ms of Lib/test's 45 ms, on one thread (2026-10-28).
/// Cost: one pass over the units and the origins; names are hashed in place, not copied.
class unit_columns {
public:
    template <class Corpus>
    explicit unit_columns(const Corpus& c) {
        const auto& us = c.units();
        std::vector<std::string_view> qualnames;
        qualnames.reserve(us.size());
        for (const unit& u : us) qualnames.push_back(u.qualname());
        detail::dictionary names(qualnames);
        m_names = names.values();
        m_name_ix = std::move(names).release_indices();
        const detail::dictionary origins(c.origins());
        m_origins = origins.values();
        m_origin_ix.reserve(us.size());
        m_line.reserve(us.size());
        for (const unit& u : us) {
            m_origin_ix.push_back(origins.index_of(u.origin()));
            m_line.push_back(u.line());
        }
    }
    [[nodiscard]] const std::shared_ptr<arrow::Array>& names() const noexcept { return m_names; }
    [[nodiscard]] const std::shared_ptr<arrow::Array>& origins() const noexcept { return m_origins; }
    [[nodiscard]] const std::vector<std::int32_t>& name_ix() const noexcept { return m_name_ix; }
    [[nodiscard]] const std::vector<std::int32_t>& origin_ix() const noexcept { return m_origin_ix; }
    [[nodiscard]] const std::vector<std::uint32_t>& line() const noexcept { return m_line; }

private:
    std::shared_ptr<arrow::Array> m_names;     // the distinct qualnames
    std::shared_ptr<arrow::Array> m_origins;   // the distinct origins
    std::vector<std::int32_t> m_name_ix;       // per unit id: its index into each
    std::vector<std::int32_t> m_origin_ix;
    std::vector<std::uint32_t> m_line;         // per unit id
};

/// What every batch of one pairs result shares: the selected columns and
/// their schema, and the units' columns (unit_columns) when names, files or
/// lines are among them. A batch is only integer copies: no string is
/// touched per row.
class pair_columns {
public:
    /// The shared part of every batch of a pairs result with columns `cols`; `units` may be null
    /// when no column shows a name, file or line (needs_units).
    pair_columns(std::shared_ptr<const unit_columns> units, std::vector<pair_col> cols);

    /// Whether columns `cols` show a unit's name, file or line.
    [[nodiscard]] static bool needs_units(const std::vector<pair_col>& cols) noexcept { return std::any_of(cols.begin(), cols.end(), shows_units); }

    [[nodiscard]] const std::shared_ptr<arrow::Schema>& schema() const noexcept { return m_schema; }
    /// Pairs [m, m + n) as one batch of the selected columns. Every column
    /// is written straight into its buffer; no builder, no string copied.
    /// Cost: one store per selected column per row — 56 bytes a row with
    /// every column, 16 for (a, b, score).
    [[nodiscard]] std::shared_ptr<arrow::RecordBatch> batch(const match* m, std::size_t n) const;
    /// The bytes one row takes in the selected columns (a bit column counted as a byte): what sizes
    /// a stream's tiles to a cache.
    [[nodiscard]] std::size_t row_bytes() const noexcept {
        std::size_t b = 0;
        for (pair_col c : m_cols) b += std::max<std::size_t>(column_of(c).width(), 1);
        return b;
    }

private:
    friend class pair_batch;
    std::vector<pair_col> m_cols;
    std::shared_ptr<arrow::Schema> m_schema;
    std::shared_ptr<const unit_columns> m_units;
    const std::int32_t* m_name_ix = nullptr;     // m_units' per unit id, read per row
    const std::int32_t* m_origin_ix = nullptr;
    const std::uint32_t* m_line = nullptr;
};

/// The Arrow type a plain column of `t` has (a dictionary column's indices: int32).
[[nodiscard]] inline std::shared_ptr<arrow::DataType> arrow_type(pair_type t) {
    switch (t) {
        case pair_type::id: return arrow::uint32();
        case pair_type::name: case pair_type::origin: return arrow::int32();
        case pair_type::score: return arrow::float64();
        case pair_type::count: return arrow::uint64();
        case pair_type::bit: return arrow::boolean();
    }
    return arrow::null();
}

/// The field of column `c` (see the file comment).
[[nodiscard]] inline std::shared_ptr<arrow::Field> pair_field(pair_col c) {
    const pair_column& col = column_of(c);
    const std::string name(col.name());
    if (col.type() == pair_type::name || col.type() == pair_type::origin) return detail::dict_field(name.c_str());
    return arrow::field(name, arrow_type(col.type()), false);
}

inline pair_columns::pair_columns(std::shared_ptr<const unit_columns> units, std::vector<pair_col> cols)
    : m_cols(std::move(cols)), m_units(std::move(units)) {
    arrow::FieldVector fields;
    for (pair_col x : m_cols) fields.push_back(pair_field(x));
    m_schema = arrow::schema(std::move(fields));
    // Every batch's columns take the schema's own types, and Arrow compares a column's type with its
    // field's by a fingerprint it fills on first use: filled here, once, before any worker shares
    // them — a type made per batch had its fingerprint built per batch, and the schema's filled by
    // whichever workers got there first.
    (void)m_schema->fingerprint();
    for (const auto& f : m_schema->fields()) (void)f->type()->fingerprint();
    if (needs_units(m_cols)) {
        if (!m_units) throw std::logic_error("similarity: a pairs result that shows names, files or lines needs the units' columns");
        m_name_ix = m_units->name_ix().data();
        m_origin_ix = m_units->origin_ix().data();
        m_line = m_units->line().data();
    }
}

/// One batch of a pairs result while it is written: every selected column allocated at once for
/// `rows` rows, then filled — from scored matches (a ranked result), or piece by piece at the
/// places a placed stream gives (stream.h), by any threads on disjoint rows — and finished into a
/// RecordBatch. Writers on one batch start and end on multiples of 8 rows (but at its end), so no
/// two share a byte of the `exact` column's bits.
class pair_batch {
public:
    pair_batch(const pair_columns& cols, std::size_t rows) : m_cols(&cols), m_rows(rows) {
        m_columns.reserve(cols.m_cols.size());
        for (pair_col c : cols.m_cols) m_columns.emplace_back(c, rows);
    }

    /// Rows [at, at + n) from the matches m[0..n).
    void write(std::size_t at, const match* m, std::size_t n) {
        fill(at, n, [&](std::size_t i) { return m[i].a(); }, [&](std::size_t i) { return m[i].b(); },
             [&](std::size_t i) { return m[i].score().shared(); }, [&](std::size_t i) { return m[i].score().total(); },
             [&](std::size_t i) { return m[i].value(); });
    }
    /// Rows [at, at + n): unit `a` of weight `wa` against the units b[0..n) of weights wb[0..n),
    /// sharing shared[0..n) subtrees with each — (a, b) as (lower, higher) id, scored as match does.
    /// Shared counts of 32 bits (a function's) or 64 (a class's: its header and its methods').
    template <class Shared>
    void write(std::size_t at, std::uint32_t a, std::uint64_t wa, const std::uint32_t* b, const std::uint64_t* wb,
               const Shared* shared, std::size_t n) {
        fill(at, n, [&](std::size_t i) { return std::min(a, b[i]); }, [&](std::size_t i) { return std::max(a, b[i]); },
             [&](std::size_t i) { return std::uint64_t{shared[i]}; }, [&](std::size_t i) { return wa + wb[i]; },
             [&](std::size_t i) { return score(shared[i], wa + wb[i]).value(); });
    }

    [[nodiscard]] std::shared_ptr<arrow::RecordBatch> finish() && {
        std::vector<std::shared_ptr<arrow::Array>> arrays;
        arrays.reserve(m_columns.size());
        for (std::size_t i = 0; i < m_columns.size(); ++i) {
            arrays.push_back(std::move(m_columns[i]).array(*m_cols, m_rows, m_cols->m_schema->field(static_cast<int>(i))->type()));
        }
        return arrow::RecordBatch::Make(m_cols->m_schema, static_cast<std::int64_t>(m_rows), std::move(arrays));
    }

private:
    // One selected column's buffer: values of its width, or bits (zeroed, set one by one).
    class column_buffer {
    public:
        column_buffer(pair_col c, std::size_t rows) : m_col(c) {
            const bool bits = column_of(c).type() == pair_type::bit;
            const std::size_t bytes = bits ? (rows + 7) / 8 : rows * column_of(c).width();
            m_buf = detail::value(arrow::AllocateBuffer(static_cast<std::int64_t>(bytes)));
            if (bits) std::fill(m_buf->mutable_data(), m_buf->mutable_data() + bytes, std::uint8_t{0});
        }
        [[nodiscard]] pair_col col() const noexcept { return m_col; }
        template <class T>
        [[nodiscard]] T* data() const noexcept { return reinterpret_cast<T*>(m_buf->mutable_data()); }

        /// The column as an array of `type`, its field's in the schema.
        [[nodiscard]] std::shared_ptr<arrow::Array> array(const pair_columns& cols, std::size_t rows, const std::shared_ptr<arrow::DataType>& type) && {
            const auto n = static_cast<std::int64_t>(rows);
            auto plain = [&](const std::shared_ptr<arrow::DataType>& of) {
                return arrow::MakeArray(arrow::ArrayData::Make(of, n, {nullptr, std::move(m_buf)}, 0));
            };
            auto coded = [&](const std::shared_ptr<arrow::Array>& dict) -> std::shared_ptr<arrow::Array> {
                return std::make_shared<arrow::DictionaryArray>(type, plain(arrow::int32()), dict);
            };
            const pair_type t = column_of(m_col).type();
            if (t == pair_type::name) return coded(cols.m_units->names());
            if (t == pair_type::origin) return coded(cols.m_units->origins());
            return plain(type);
        }

    private:
        pair_col m_col;
        std::shared_ptr<arrow::Buffer> m_buf;
    };

    // Rows [at, at + n), each column in one loop of its own over the row accessors.
    template <class A, class B, class S, class T, class V>
    void fill(std::size_t at, std::size_t n, A&& a, B&& b, S&& shared, T&& total, V&& value) {
        const pair_columns& c = *m_cols;
        for (column_buffer& col : m_columns) {
            switch (col.col()) {
                case pair_col::a: copy(col.data<std::uint32_t>() + at, n, a); break;
                case pair_col::b: copy(col.data<std::uint32_t>() + at, n, b); break;
                case pair_col::a_qualname: copy(col.data<std::int32_t>() + at, n, [&](std::size_t i) { return c.m_name_ix[a(i)]; }); break;
                case pair_col::a_origin: copy(col.data<std::int32_t>() + at, n, [&](std::size_t i) { return c.m_origin_ix[a(i)]; }); break;
                case pair_col::a_line: copy(col.data<std::uint32_t>() + at, n, [&](std::size_t i) { return c.m_line[a(i)]; }); break;
                case pair_col::b_qualname: copy(col.data<std::int32_t>() + at, n, [&](std::size_t i) { return c.m_name_ix[b(i)]; }); break;
                case pair_col::b_origin: copy(col.data<std::int32_t>() + at, n, [&](std::size_t i) { return c.m_origin_ix[b(i)]; }); break;
                case pair_col::b_line: copy(col.data<std::uint32_t>() + at, n, [&](std::size_t i) { return c.m_line[b(i)]; }); break;
                case pair_col::score: copy(col.data<double>() + at, n, value); break;
                case pair_col::shared: copy(col.data<std::uint64_t>() + at, n, shared); break;
                case pair_col::total: copy(col.data<std::uint64_t>() + at, n, total); break;
                case pair_col::exact: {   // score::exact, eight rows a byte; a writer's rows start on a byte
                    std::uint8_t* bits = col.data<std::uint8_t>();
                    auto exact = [&](std::size_t i) { return static_cast<unsigned>(2 * shared(i) == total(i)); };
                    std::size_t i = 0;
                    for (; i < n && (at + i) % 8 != 0; ++i) bits[(at + i) / 8] |= static_cast<std::uint8_t>(exact(i) << ((at + i) % 8));
                    for (; i + 8 <= n; i += 8) {   // a whole byte, assembled without a branch and stored once
                        unsigned byte = 0;
                        for (unsigned k = 0; k < 8; ++k) byte |= exact(i + k) << k;
                        bits[(at + i) / 8] = static_cast<std::uint8_t>(byte);
                    }
                    for (; i < n; ++i) bits[(at + i) / 8] |= static_cast<std::uint8_t>(exact(i) << ((at + i) % 8));
                    break;
                }
            }
        }
    }
    template <class T, class F>
    static void copy(T* out, std::size_t n, F&& f) {
        for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<T>(f(i));
    }

    const pair_columns* m_cols;
    std::size_t m_rows;
    std::vector<column_buffer> m_columns;
};

inline std::shared_ptr<arrow::RecordBatch> pair_columns::batch(const match* m, std::size_t n) const {
    pair_batch out(*this, n);
    out.write(0, m, n);
    return std::move(out).finish();
}

}  // namespace pygim::similarity::tables
