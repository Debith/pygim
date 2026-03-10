#pragma once
// Fast test-data generator backed by Arrow array builders.
// Generates columnar Arrow RecordBatches entirely in C++ with zero
// Python-object overhead — typically 50–100× faster than pure-Python
// list-comprehension approaches.
//
// Supported column types:
//   int8, int16, int32, int64, uint8, uint16, uint32, uint64,
//   bool, float32, float64, string, date, time, timestamp,
//   duration, binary, uuid
//
// All generation is deterministic given the same seed.

#include <arrow/api.h>
#include <arrow/builder.h>
#include <arrow/c/bridge.h>

#include <pybind11/pybind11.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace pygim::datagen {

// ── Error-handling helpers ──────────────────────────────────────────────────

#define DATAGEN_THROW_NOT_OK(expr)                          \
    do {                                                    \
        auto _s = (expr);                                   \
        if (!_s.ok()) throw std::runtime_error(_s.ToString()); \
    } while (0)

#define DATAGEN_FINISH(builder, out)                        \
    do {                                                    \
        auto _r = (builder).Finish();                       \
        if (!_r.ok())                                       \
            throw std::runtime_error(_r.status().ToString()); \
        (out) = std::move(*_r);                             \
    } while (0)

// ── Fast PRNG (xorshift64*) ────────────────────────────────────────────────

struct Xorshift64 {
    uint64_t state;

    explicit Xorshift64(uint64_t seed = 42) noexcept
        : state(seed | 1)  // guarantee non-zero
    {}

    uint64_t next() noexcept {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    /// Uniform value in [lo, hi] (inclusive).
    int64_t range_i64(int64_t lo, int64_t hi) noexcept {
        auto span = static_cast<uint64_t>(hi - lo) + 1;
        return lo + static_cast<int64_t>(next() % span);
    }

    /// Uniform double in [0, 1).
    double unit_double() noexcept {
        return static_cast<double>(next() >> 11) * 0x1.0p-53;
    }

    bool coin(double p) noexcept { return unit_double() < p; }
};

// ── Column type identification ──────────────────────────────────────────────

enum class ColType {
    Int8, Int16, Int32, Int64,
    UInt8, UInt16, UInt32, UInt64,
    Bool,
    Float32, Float64,
    String, Date32, Time64, Timestamp, Duration,
    Binary, Uuid,
    Serial,  // sequential int32: 1, 2, 3, … (for PK / id columns)
};

/// Parse a user-provided type string into a ColType.
/// Accepts Arrow names, SQL names, and common aliases (case-insensitive).
inline ColType parse_type(const std::string& s) {
    std::string t(s.size(), '\0');
    std::transform(s.begin(), s.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // ── Integer types ───────────────────────────────────────────────────
    if (t == "int8"   || t == "tinyint")                     return ColType::Int8;
    if (t == "int16"  || t == "smallint")                    return ColType::Int16;
    if (t == "int32"  || t == "int" || t == "integer")       return ColType::Int32;
    if (t == "int64"  || t == "bigint" || t == "long")       return ColType::Int64;
    if (t == "uint8")                                        return ColType::UInt8;
    if (t == "uint16")                                       return ColType::UInt16;
    if (t == "uint32")                                       return ColType::UInt32;
    if (t == "uint64")                                       return ColType::UInt64;

    // ── Boolean ─────────────────────────────────────────────────────────
    if (t == "bool" || t == "boolean" || t == "bit")         return ColType::Bool;

    // ── Floating-point (Python convention: "float" → 64-bit) ────────────
    if (t == "float32" || t == "real")                       return ColType::Float32;
    if (t == "float64" || t == "double" || t == "float")     return ColType::Float64;

    // ── String ──────────────────────────────────────────────────────────
    if (t == "string" || t == "utf8" || t == "varchar" ||
        t == "nvarchar" || t == "text" || t == "nchar")      return ColType::String;

    // ── Temporal ────────────────────────────────────────────────────────
    if (t == "date"   || t == "date32")                      return ColType::Date32;
    if (t == "time"   || t == "time64")                      return ColType::Time64;
    if (t == "timestamp" || t == "datetime" || t == "datetime2")
                                                             return ColType::Timestamp;
    if (t == "duration")                                     return ColType::Duration;

    // ── Binary / UUID ───────────────────────────────────────────────────
    if (t == "binary" || t == "varbinary" || t == "bytes")   return ColType::Binary;
    if (t == "uuid" || t == "uniqueidentifier" || t == "guid")
                                                             return ColType::Uuid;

    // ── Sequential (PK / id) ────────────────────────────────────────────
    if (t == "serial" || t == "sequence" || t == "identity")
                                                             return ColType::Serial;

    throw std::invalid_argument(
        "Unknown column type: '" + s + "'.  Supported: "
        "int8, int16, int32, int64, uint8–uint64, bool, float32, float64, "
        "string, date, time, timestamp, duration, binary, uuid, serial");
}

/// Map ColType → Arrow DataType.
inline std::shared_ptr<arrow::DataType> arrow_type_for(ColType ct) {
    switch (ct) {
        case ColType::Int8:      return arrow::int8();
        case ColType::Int16:     return arrow::int16();
        case ColType::Int32:     return arrow::int32();
        case ColType::Int64:     return arrow::int64();
        case ColType::UInt8:     return arrow::uint8();
        case ColType::UInt16:    return arrow::uint16();
        case ColType::UInt32:    return arrow::uint32();
        case ColType::UInt64:    return arrow::uint64();
        case ColType::Bool:      return arrow::boolean();
        case ColType::Float32:   return arrow::float32();
        case ColType::Float64:   return arrow::float64();
        case ColType::String:    return arrow::utf8();
        case ColType::Date32:    return arrow::date32();
        case ColType::Time64:    return arrow::time64(arrow::TimeUnit::NANO);
        case ColType::Timestamp: return arrow::timestamp(arrow::TimeUnit::MICRO);
        case ColType::Duration:  return arrow::duration(arrow::TimeUnit::MICRO);
        case ColType::Binary:    return arrow::binary();
        case ColType::Uuid:      return arrow::utf8();  // UUID stored as string
        case ColType::Serial:    return arrow::int32();  // sequential 1, 2, 3, …
    }
    __builtin_unreachable();
}

// ── Per-type column generators ──────────────────────────────────────────────
// Each function fills an Arrow array with `rows` values using the given PRNG.
// NULL injection is controlled by `null_frac` ∈ [0, 1].

template <typename BuilderT, typename T>
std::shared_ptr<arrow::Array> gen_int(
    int64_t rows, Xorshift64& rng, double null_frac, T lo, T hi)
{
    BuilderT b;
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    auto span = static_cast<uint64_t>(hi) - static_cast<uint64_t>(lo) + 1;
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac)) {
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        } else {
            auto val = static_cast<T>(static_cast<uint64_t>(lo) + rng.next() % span);
            DATAGEN_THROW_NOT_OK(b.Append(val));
        }
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

/// Sequential integer column (id-like): 1, 2, 3, … rows.
template <typename BuilderT, typename T>
std::shared_ptr<arrow::Array> gen_sequential(int64_t rows) {
    BuilderT b;
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    for (int64_t i = 0; i < rows; ++i) {
        DATAGEN_THROW_NOT_OK(b.Append(static_cast<T>(i + 1)));
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

inline std::shared_ptr<arrow::Array> gen_bool(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    arrow::BooleanBuilder b;
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac))
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        else
            DATAGEN_THROW_NOT_OK(b.Append(static_cast<bool>(rng.next() & 1)));
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

template <typename BuilderT, typename T>
std::shared_ptr<arrow::Array> gen_float(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    BuilderT b;
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac))
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        else
            DATAGEN_THROW_NOT_OK(b.Append(static_cast<T>(i) * T(1.23456789)));
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

inline std::shared_ptr<arrow::Array> gen_string(
    int64_t rows, const std::string& col_name, Xorshift64& rng, double null_frac)
{
    arrow::StringBuilder b;
    auto avg_len = static_cast<int64_t>(col_name.size()) + 9;  // "prefix_0000001"
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    DATAGEN_THROW_NOT_OK(b.ReserveData(rows * avg_len));

    char buf[128];
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac)) {
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        } else {
            int len = std::snprintf(buf, sizeof(buf), "%s_%07lld",
                                    col_name.c_str(),
                                    static_cast<long long>(i));
            DATAGEN_THROW_NOT_OK(b.Append(buf, len));
        }
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

/// DATE32: days since epoch, cycling through ~27 years.
inline std::shared_ptr<arrow::Array> gen_date32(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    arrow::Date32Builder b;
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac))
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        else
            DATAGEN_THROW_NOT_OK(b.Append(static_cast<int32_t>(i % 10'000)));
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

/// TIME64[ns]: time of day (nanoseconds since midnight).
inline std::shared_ptr<arrow::Array> gen_time64(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    arrow::Time64Builder b(arrow::time64(arrow::TimeUnit::NANO),
                          arrow::default_memory_pool());
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac)) {
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        } else {
            int64_t h  = i % 24;
            int64_t m  = (i * 7) % 60;
            int64_t s  = (i * 13) % 60;
            int64_t ns = h * 3'600'000'000'000LL
                       + m * 60'000'000'000LL
                       + s * 1'000'000'000LL;
            DATAGEN_THROW_NOT_OK(b.Append(ns));
        }
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

/// TIMESTAMP[µs]: microseconds since epoch, base at 2020-01-01.
inline std::shared_ptr<arrow::Array> gen_timestamp(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    arrow::TimestampBuilder b(arrow::timestamp(arrow::TimeUnit::MICRO),
                              arrow::default_memory_pool());
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    // 2020-01-01T00:00:00Z in µs since epoch
    constexpr int64_t base_us = 1'577'836'800'000'000LL;
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac))
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        else
            DATAGEN_THROW_NOT_OK(b.Append(base_us + i * 1'000'003LL));
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

/// DURATION[µs]: microsecond intervals.
inline std::shared_ptr<arrow::Array> gen_duration(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    arrow::DurationBuilder b(arrow::duration(arrow::TimeUnit::MICRO),
                             arrow::default_memory_pool());
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac))
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        else
            DATAGEN_THROW_NOT_OK(b.Append(i * 1'000LL));
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

/// BINARY: variable-length random bytes (1–16 bytes per row).
inline std::shared_ptr<arrow::Array> gen_binary(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    arrow::BinaryBuilder b;
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    DATAGEN_THROW_NOT_OK(b.ReserveData(rows * 10));
    uint8_t buf[32];
    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac)) {
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        } else {
            int len = 1 + static_cast<int>(i % 16);
            for (int j = 0; j < len; ++j)
                buf[j] = static_cast<uint8_t>(rng.next());
            DATAGEN_THROW_NOT_OK(b.Append(buf, len));
        }
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

/// UUID: deterministic UUID-v4–formatted strings.
inline std::shared_ptr<arrow::Array> gen_uuid(
    int64_t rows, Xorshift64& rng, double null_frac)
{
    arrow::StringBuilder b;
    DATAGEN_THROW_NOT_OK(b.Reserve(rows));
    DATAGEN_THROW_NOT_OK(b.ReserveData(rows * 36));

    static constexpr char hex[] = "0123456789abcdef";
    char buf[37];  // 36 chars + '\0'

    auto put = [&](int pos, uint8_t byte) {
        buf[pos]     = hex[byte >> 4];
        buf[pos + 1] = hex[byte & 0x0f];
    };

    for (int64_t i = 0; i < rows; ++i) {
        if (null_frac > 0.0 && rng.coin(null_frac)) {
            DATAGEN_THROW_NOT_OK(b.AppendNull());
        } else {
            uint64_t a = rng.next();
            uint64_t c = rng.next();

            // Format: xxxxxxxx-xxxx-4xxx-{8|9|a|b}xxx-xxxxxxxxxxxx
            put(0,  static_cast<uint8_t>(a >> 56));
            put(2,  static_cast<uint8_t>(a >> 48));
            put(4,  static_cast<uint8_t>(a >> 40));
            put(6,  static_cast<uint8_t>(a >> 32));
            buf[8] = '-';
            put(9,  static_cast<uint8_t>(a >> 24));
            put(11, static_cast<uint8_t>(a >> 16));
            buf[13] = '-';
            put(14, static_cast<uint8_t>(((a >> 8) & 0x0f) | 0x40));  // version 4
            put(16, static_cast<uint8_t>(a));
            buf[18] = '-';
            put(19, static_cast<uint8_t>(((c >> 56) & 0x3f) | 0x80));  // variant 1
            put(21, static_cast<uint8_t>(c >> 48));
            buf[23] = '-';
            put(24, static_cast<uint8_t>(c >> 40));
            put(26, static_cast<uint8_t>(c >> 32));
            put(28, static_cast<uint8_t>(c >> 24));
            put(30, static_cast<uint8_t>(c >> 16));
            put(32, static_cast<uint8_t>(c >> 8));
            put(34, static_cast<uint8_t>(c));
            buf[36] = '\0';

            DATAGEN_THROW_NOT_OK(b.Append(buf, 36));
        }
    }
    std::shared_ptr<arrow::Array> out;
    DATAGEN_FINISH(b, out);
    return out;
}

// ── Column spec & dispatch ──────────────────────────────────────────────────

struct ColumnSpec {
    std::string name;
    ColType     type;
};

/// Generate a single Arrow array for a column spec.
inline std::shared_ptr<arrow::Array> generate_column(
    const ColumnSpec& spec, int64_t rows, Xorshift64& rng, double null_frac)
{
    switch (spec.type) {
        case ColType::Int8:
            return gen_int<arrow::Int8Builder, int8_t>(rows, rng, null_frac, -120, 120);
        case ColType::Int16:
            return gen_int<arrow::Int16Builder, int16_t>(rows, rng, null_frac, -15'000, 15'000);
        case ColType::Int32:
            return gen_int<arrow::Int32Builder, int32_t>(rows, rng, null_frac, 0, 100'000);
        case ColType::Int64:
            return gen_int<arrow::Int64Builder, int64_t>(rows, rng, null_frac, 0, 10'000'000'000LL);
        case ColType::UInt8:
            return gen_int<arrow::UInt8Builder, uint8_t>(rows, rng, null_frac, 0, 250);
        case ColType::UInt16:
            return gen_int<arrow::UInt16Builder, uint16_t>(rows, rng, null_frac, 0, 30'000);
        case ColType::UInt32:
            return gen_int<arrow::UInt32Builder, uint32_t>(rows, rng, null_frac, 0, 2'000'000'000u);
        case ColType::UInt64:
            return gen_int<arrow::UInt64Builder, uint64_t>(rows, rng, null_frac, 0, 4'000'000'000ULL);
        case ColType::Bool:
            return gen_bool(rows, rng, null_frac);
        case ColType::Float32:
            return gen_float<arrow::FloatBuilder, float>(rows, rng, null_frac);
        case ColType::Float64:
            return gen_float<arrow::DoubleBuilder, double>(rows, rng, null_frac);
        case ColType::String:
            return gen_string(rows, spec.name, rng, null_frac);
        case ColType::Date32:
            return gen_date32(rows, rng, null_frac);
        case ColType::Time64:
            return gen_time64(rows, rng, null_frac);
        case ColType::Timestamp:
            return gen_timestamp(rows, rng, null_frac);
        case ColType::Duration:
            return gen_duration(rows, rng, null_frac);
        case ColType::Binary:
            return gen_binary(rows, rng, null_frac);
        case ColType::Uuid:
            return gen_uuid(rows, rng, null_frac);
        case ColType::Serial:
            return gen_sequential<arrow::Int32Builder, int32_t>(rows);
    }
    __builtin_unreachable();
}

/// Build a complete Arrow RecordBatch from column specifications.
inline std::shared_ptr<arrow::RecordBatch> generate_batch(
    const std::vector<ColumnSpec>& columns,
    int64_t rows,
    uint64_t seed,
    double null_fraction)
{
    Xorshift64 rng(seed);

    std::vector<std::shared_ptr<arrow::Field>>  fields;
    std::vector<std::shared_ptr<arrow::Array>>  arrays;
    fields.reserve(columns.size());
    arrays.reserve(columns.size());

    for (const auto& spec : columns) {
        fields.push_back(arrow::field(spec.name, arrow_type_for(spec.type)));
        arrays.push_back(generate_column(spec, rows, rng, null_fraction));
    }

    auto schema = arrow::schema(std::move(fields));
    return arrow::RecordBatch::Make(std::move(schema), rows, std::move(arrays));
}

// ── Arrow PyCapsule protocol export ─────────────────────────────────────────
// Returns an object implementing __arrow_c_stream__, compatible with
// pyarrow.RecordBatchReader.from_stream() and polars.from_arrow().

struct ArrowStreamExporter {
    std::shared_ptr<arrow::RecordBatch> batch;

    /// PyCapsule protocol: called by pyarrow / polars to consume the data.
    py::capsule arrow_c_stream(py::object /*requested_schema*/ = py::none()) const {
        auto reader_result = arrow::RecordBatchReader::Make({batch}, batch->schema());
        if (!reader_result.ok())
            throw std::runtime_error(reader_result.status().ToString());

        auto* c_stream = new ArrowArrayStream;
        std::memset(c_stream, 0, sizeof(*c_stream));

        auto status = arrow::ExportRecordBatchReader(
            std::move(*reader_result), c_stream);
        if (!status.ok()) {
            delete c_stream;
            throw std::runtime_error(status.ToString());
        }

        return py::capsule(
            static_cast<void*>(c_stream), "arrow_array_stream",
            [](void* ptr) {
                auto* s = static_cast<ArrowArrayStream*>(ptr);
                if (s->release) s->release(s);
                delete s;
            });
    }
};

/// Top-level entry point called from Python.
inline ArrowStreamExporter generate(
    const py::dict& schema_dict,
    int64_t rows,
    uint64_t seed = 42,
    double null_fraction = 0.0)
{
    if (rows <= 0)
        throw std::invalid_argument("rows must be positive");
    if (null_fraction < 0.0 || null_fraction > 1.0)
        throw std::invalid_argument("null_fraction must be in [0.0, 1.0]");

    std::vector<ColumnSpec> columns;
    columns.reserve(py::len(schema_dict));

    for (auto item : schema_dict) {
        columns.push_back({
            item.first.cast<std::string>(),
            parse_type(item.second.cast<std::string>()),
        });
    }

    if (columns.empty())
        throw std::invalid_argument("schema must have at least one column");

    auto batch = generate_batch(columns, rows, seed, null_fraction);
    return ArrowStreamExporter{std::move(batch)};
}

/// Return a list of all supported type names (for documentation / introspection).
inline std::vector<std::string> supported_types() {
    return {
        "int8", "int16", "int32", "int64",
        "uint8", "uint16", "uint32", "uint64",
        "bool", "float32", "float64",
        "string", "date", "time", "timestamp", "duration",
        "binary", "uuid", "serial",
    };
}

} // namespace pygim::datagen
