#include "../mssql_strategy.h"
#include "helpers.h"
#include "../../../utils/logging.h"

#define PYGIM_HAVE_ARROW 1
#define PYGIM_HAVE_ODBC 1
#if PYGIM_HAVE_ODBC && PYGIM_HAVE_ARROW
#if !defined(_WIN32) && !defined(_WIN64)
#include <dlfcn.h>
#endif
#include <arrow/ipc/api.h>
#include <arrow/io/api.h>
#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/buffer.h>
#if __has_include(<arrow/c/bridge.h>)
#include <arrow/c/bridge.h>
#define PYGIM_HAVE_ARROW_C_BRIDGE 1
#else
#define PYGIM_HAVE_ARROW_C_BRIDGE 0
#endif
#include <chrono>
#include <cstdio>
#include <limits>

#if defined(ARROW_VERSION_MAJOR) && (ARROW_VERSION_MAJOR >= 15)
#define PYGIM_HAVE_ARROW_STRING_VIEW 1
#else
#define PYGIM_HAVE_ARROW_STRING_VIEW 0
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <msodbcsql.h>
#else
#ifndef SQL_COPT_SS_BCP
#define SQL_COPT_SS_BASE 1200
#define SQL_COPT_SS_BCP (SQL_COPT_SS_BASE + 19)
#define SQL_BCP_ON ((SQLULEN)1L)
#define SQL_BCP_OFF ((SQLULEN)0L)
#endif
#define DB_IN 1
typedef int DBINT;
typedef unsigned char BYTE;
typedef BYTE* LPCBYTE;
#define SUCCEED 1
#define FAIL 0
#define SQLBIGINT 0x7f
#define SQLINT4 0x38
#define SQLBIT 0x32
#define SQLFLT8 0x3e
#define SQLCHARACTER 0x2f
#define SQLVARCHAR 0x27
#define SQLNCHAR 0xef
#define SQLDATEN 0x28
#define SQLDATETIME2N 0x2a
#define SQL_VARLEN_DATA (-10)
typedef RETCODE (*bcp_initW_t)(HDBC, LPCWSTR, LPCWSTR, LPCWSTR, int);
typedef RETCODE (*bcp_bind_t)(HDBC, LPCBYTE, int, DBINT, LPCBYTE, int, int, int);
typedef RETCODE (*bcp_sendrow_t)(HDBC);
typedef DBINT (*bcp_batch_t)(HDBC);
typedef DBINT (*bcp_done_t)(HDBC);
typedef RETCODE (*bcp_collen_t)(HDBC, DBINT, int);
typedef RETCODE (*bcp_colptr_t)(HDBC, LPCBYTE, int);
static bcp_initW_t g_bcp_init = nullptr;
static bcp_bind_t g_bcp_bind = nullptr;
static bcp_sendrow_t g_bcp_sendrow = nullptr;
static bcp_batch_t g_bcp_batch = nullptr;
static bcp_done_t g_bcp_done = nullptr;
static bcp_collen_t g_bcp_collen = nullptr;
static bcp_colptr_t g_bcp_colptr = nullptr;
#define bcp_init g_bcp_init
#define bcp_bind g_bcp_bind
#define bcp_sendrow g_bcp_sendrow
#define bcp_batch g_bcp_batch
#define bcp_done g_bcp_done
#define bcp_collen g_bcp_collen
#define bcp_colptr g_bcp_colptr
#endif
#endif

namespace py = pybind11;

namespace pygim {

void MssqlStrategyNative::bulk_insert_arrow_bcp(const std::string &table,
                                                const py::object &arrow_ipc_payload,
                                                int batch_size,
                                                const std::string &table_hint) {
    PYGIM_SCOPE_LOG_TAG("repo.bcp");
#if PYGIM_HAVE_ODBC && PYGIM_HAVE_ARROW
(void)table_hint;
#if !defined(_WIN32) && !defined(_WIN64)
    static bool bcp_attempted = false;
    if (!bcp_attempted) {
        bcp_attempted = true;
        void *handle = dlopen("/opt/microsoft/msodbcsql18/lib64/libmsodbcsql-18.5.so.1.1", RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            g_bcp_init = (bcp_initW_t)dlsym(handle, "bcp_initW");
            g_bcp_bind = (bcp_bind_t)dlsym(handle, "bcp_bind");
            g_bcp_sendrow = (bcp_sendrow_t)dlsym(handle, "bcp_sendrow");
            g_bcp_batch = (bcp_batch_t)dlsym(handle, "bcp_batch");
            g_bcp_done = (bcp_done_t)dlsym(handle, "bcp_done");
            g_bcp_collen = (bcp_collen_t)dlsym(handle, "bcp_collen");
            g_bcp_colptr = (bcp_colptr_t)dlsym(handle, "bcp_colptr");
        }
    }
    if (!g_bcp_init || !g_bcp_bind || !g_bcp_sendrow || !g_bcp_batch || !g_bcp_done || !g_bcp_collen || !g_bcp_colptr) {
        throw std::runtime_error(
            "BCP functions not available. SQL Server ODBC Driver 17/18+ required.\n"
            "Install: sudo ACCEPT_EULA=Y apt-get install -y msodbcsql18");
    }
#endif
    ensure_connected();
    SQLULEN bcp_status = SQL_BCP_OFF;
    SQLRETURN attr_status = SQLGetConnectAttr(m_dbc, SQL_COPT_SS_BCP, &bcp_status, 0, nullptr);
    if (!SQL_SUCCEEDED(attr_status) || bcp_status != SQL_BCP_ON) {
        SQLRETURN attr_ret = SQLSetConnectAttr(m_dbc, SQL_COPT_SS_BCP, (SQLPOINTER)SQL_BCP_ON, SQL_IS_UINTEGER);
        if (!SQL_SUCCEEDED(attr_ret)) {
            raise_if_error(attr_ret, SQL_HANDLE_DBC, m_dbc, "SQLSetConnectAttr(BCP_ON, runtime)");
        }
    }
    auto is_valid_identifier_or_schema = [](const std::string &name) {
        if (detail::is_valid_identifier(name)) {
            return true;
        }
        auto dot = name.find('.');
        if (dot == std::string::npos) {
            return false;
        }
        std::string schema = name.substr(0, dot);
        std::string obj = name.substr(dot + 1);
        return !schema.empty() && !obj.empty() && detail::is_valid_identifier(schema) && detail::is_valid_identifier(obj);
    };
    if (!is_valid_identifier_or_schema(table)) {
        throw std::runtime_error("Invalid table identifier");
    }
    std::string qualified_table = table;
    if (qualified_table.find('.') == std::string::npos) {
        qualified_table = "dbo." + qualified_table;
    }
    std::shared_ptr<arrow::Buffer> buffer;
    py::object payload_owner = py::none();

    const bool is_arrow_c_stream_capsule =
        PyCapsule_CheckExact(arrow_ipc_payload.ptr()) &&
        PyCapsule_IsValid(arrow_ipc_payload.ptr(), "arrow_array_stream");

    SQLRETURN ret = SQL_SUCCESS;
    std::u16string table_w;
    table_w.reserve(qualified_table.size());
    for (char ch : qualified_table) {
        table_w.push_back(static_cast<char16_t>(ch));
    }
    ret = bcp_init(m_dbc, reinterpret_cast<LPCWSTR>(table_w.c_str()), nullptr, nullptr, DB_IN);
    if (ret != SUCCEED) {
        std::string label = "bcp_init(table=" + qualified_table + ")";
        raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, label.c_str());
    }

    struct ColumnBinding {
        int ordinal;
        arrow::Type::type arrow_type;
        std::shared_ptr<arrow::Array> array;
        const void *data_ptr{nullptr};
        const int32_t *offsets32{nullptr};
        const int64_t *offsets64{nullptr};
        const uint8_t *str_data{nullptr};
        DBINT max_length{0};
        std::shared_ptr<std::vector<std::u16string>> utf16_cache;
        std::shared_ptr<std::vector<std::string>> utf8_cache;
        size_t value_stride{0};
        std::shared_ptr<std::vector<SQL_DATE_STRUCT>> date_buffer;
        std::shared_ptr<std::vector<SQL_TIMESTAMP_STRUCT>> timestamp_buffer;
    };

    auto days_to_sql_date = [](int32_t days_since_epoch) {
        using namespace std::chrono;
        constexpr sys_days epoch = sys_days{year{1970}/1/1};
        sys_days day_point = epoch + days{days_since_epoch};
        year_month_day ymd(day_point);
        SQL_DATE_STRUCT out{};
        out.year = static_cast<SQLSMALLINT>(static_cast<int>(ymd.year()));
        out.month = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.month()));
        out.day = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.day()));
        return out;
    };

    auto micros_to_sql_timestamp = [](int64_t micros_since_epoch) {
        using namespace std::chrono;
        sys_time<microseconds> tp(microseconds{micros_since_epoch});
        sys_days day_point = floor<days>(tp);
        year_month_day ymd(day_point);
        auto time = tp - day_point;
        auto hours = duration_cast<std::chrono::hours>(time);
        time -= hours;
        auto minutes = duration_cast<std::chrono::minutes>(time);
        time -= minutes;
        auto seconds = duration_cast<std::chrono::seconds>(time);
        time -= seconds;
        auto micros = duration_cast<microseconds>(time);
        SQL_TIMESTAMP_STRUCT out{};
        out.year = static_cast<SQLSMALLINT>(static_cast<int>(ymd.year()));
        out.month = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.month()));
        out.day = static_cast<SQLUSMALLINT>(static_cast<unsigned>(ymd.day()));
        out.hour = static_cast<SQLUSMALLINT>(hours.count());
        out.minute = static_cast<SQLUSMALLINT>(minutes.count());
        out.second = static_cast<SQLUSMALLINT>(seconds.count());
        out.fraction = static_cast<SQLUINTEGER>(micros.count() * 1000); // nanoseconds
        return out;
    };

    const int64_t batch = (batch_size > 0) ? static_cast<int64_t>(batch_size) : 100000;
    int64_t sent_rows = 0;
    int64_t processed_rows = 0;

    auto process_record_batch = [&](const std::shared_ptr<arrow::RecordBatch> &record_batch) {
        if (!record_batch) {
            return;
        }

        const int64_t num_rows = record_batch->num_rows();
        const int num_cols = record_batch->num_columns();
        if (num_rows == 0 || num_cols == 0) {
            return;
        }

        processed_rows += num_rows;

        std::vector<ColumnBinding> bindings;
        bindings.reserve(num_cols);

        for (int col_idx = 0; col_idx < num_cols; ++col_idx) {
            auto field = record_batch->schema()->field(col_idx);
            auto array = record_batch->column(col_idx);
            ColumnBinding binding;
            binding.ordinal = col_idx + 1;
            binding.arrow_type = field->type()->id();
            binding.array = array;
            switch (binding.arrow_type) {
            case arrow::Type::INT64: {
                auto typed = std::static_pointer_cast<arrow::Int64Array>(array);
                binding.data_ptr = typed->raw_values();
                binding.value_stride = sizeof(int64_t);
                ret = bcp_bind(m_dbc, (LPCBYTE)binding.data_ptr, 0, sizeof(int64_t), nullptr, 0, SQLBIGINT, binding.ordinal);
                break;
            }
            case arrow::Type::INT32: {
                auto typed = std::static_pointer_cast<arrow::Int32Array>(array);
                binding.data_ptr = typed->raw_values();
                binding.value_stride = sizeof(int32_t);
                ret = bcp_bind(m_dbc, (LPCBYTE)binding.data_ptr, 0, sizeof(int32_t), nullptr, 0, SQLINT4, binding.ordinal);
                break;
            }
            case arrow::Type::UINT8: {
                auto typed = std::static_pointer_cast<arrow::UInt8Array>(array);
                binding.data_ptr = typed->raw_values();
                binding.value_stride = sizeof(uint8_t);
                ret = bcp_bind(m_dbc, (LPCBYTE)binding.data_ptr, 0, sizeof(uint8_t), nullptr, 0, SQLBIT, binding.ordinal);
                break;
            }
            case arrow::Type::DOUBLE: {
                auto typed = std::static_pointer_cast<arrow::DoubleArray>(array);
                binding.data_ptr = typed->raw_values();
                binding.value_stride = sizeof(double);
                ret = bcp_bind(m_dbc, (LPCBYTE)binding.data_ptr, 0, sizeof(double), nullptr, 0, SQLFLT8, binding.ordinal);
                break;
            }
            case arrow::Type::DATE32: {
                auto typed = std::static_pointer_cast<arrow::Date32Array>(array);
                auto date_values = std::make_shared<std::vector<SQL_DATE_STRUCT>>(typed->length());
                auto text_values = std::make_shared<std::vector<std::string>>(typed->length());
                const int32_t *raw = typed->raw_values();
                for (int64_t i = 0; i < typed->length(); ++i) {
                    const auto d = days_to_sql_date(raw[i]);
                    (*date_values)[static_cast<size_t>(i)] = d;
                    char buffer_text[16]{};
                    std::snprintf(buffer_text,
                                  sizeof(buffer_text),
                                  "%04d-%02u-%02u",
                                  static_cast<int>(d.year),
                                  static_cast<unsigned>(d.month),
                                  static_cast<unsigned>(d.day));
                    (*text_values)[static_cast<size_t>(i)] = buffer_text;
                }
                binding.date_buffer = date_values;
                binding.utf8_cache = text_values;
                static const uint8_t dummy = 0;
                static const uint8_t term = 0;
                ret = bcp_bind(m_dbc,
                               (LPCBYTE)&dummy,
                               0,
                               SQL_VARLEN_DATA,
                               (LPCBYTE)&term,
                               1,
                               SQLCHARACTER,
                               binding.ordinal);
                break;
            }
            case arrow::Type::TIMESTAMP: {
                auto typed = std::static_pointer_cast<arrow::TimestampArray>(array);
                auto timestamp_values = std::make_shared<std::vector<SQL_TIMESTAMP_STRUCT>>(typed->length());
                auto text_values = std::make_shared<std::vector<std::string>>(typed->length());
                const int64_t *raw = typed->raw_values();
                for (int64_t i = 0; i < typed->length(); ++i) {
                    const auto t = micros_to_sql_timestamp(raw[i]);
                    (*timestamp_values)[static_cast<size_t>(i)] = t;
                    const unsigned micros = static_cast<unsigned>(t.fraction / 1000U);
                    char buffer_text[40]{};
                    std::snprintf(buffer_text,
                                  sizeof(buffer_text),
                                  "%04d-%02u-%02u %02u:%02u:%02u.%06u",
                                  static_cast<int>(t.year),
                                  static_cast<unsigned>(t.month),
                                  static_cast<unsigned>(t.day),
                                  static_cast<unsigned>(t.hour),
                                  static_cast<unsigned>(t.minute),
                                  static_cast<unsigned>(t.second),
                                  micros);
                    (*text_values)[static_cast<size_t>(i)] = buffer_text;
                }
                binding.timestamp_buffer = timestamp_values;
                binding.utf8_cache = text_values;
                static const uint8_t dummy = 0;
                static const uint8_t term = 0;
                ret = bcp_bind(m_dbc,
                               (LPCBYTE)&dummy,
                               0,
                               SQL_VARLEN_DATA,
                               (LPCBYTE)&term,
                               1,
                               SQLCHARACTER,
                               binding.ordinal);
                break;
            }
            case arrow::Type::STRING: {
                auto typed = std::static_pointer_cast<arrow::StringArray>(array);
                auto text_values = std::make_shared<std::vector<std::string>>(typed->length());
                for (int64_t i = 0; i < typed->length(); ++i) {
                    if (typed->IsNull(i)) {
                        continue;
                    }
                    (*text_values)[static_cast<size_t>(i)] = typed->GetString(i);
                }
                binding.utf8_cache = text_values;
                static const uint8_t dummy = 0;
                static const uint8_t term = 0;
                ret = bcp_bind(m_dbc,
                               (LPCBYTE)&dummy,
                               0,
                               SQL_VARLEN_DATA,
                               (LPCBYTE)&term,
                               1,
                               SQLCHARACTER,
                               binding.ordinal);
                break;
            }
            case arrow::Type::LARGE_STRING: {
                auto typed = std::static_pointer_cast<arrow::LargeStringArray>(array);
                auto text_values = std::make_shared<std::vector<std::string>>(typed->length());
                for (int64_t i = 0; i < typed->length(); ++i) {
                    if (typed->IsNull(i)) {
                        continue;
                    }
                    (*text_values)[static_cast<size_t>(i)] = typed->GetString(i);
                }
                binding.utf8_cache = text_values;
                static const uint8_t dummy = 0;
                static const uint8_t term = 0;
                ret = bcp_bind(m_dbc,
                               (LPCBYTE)&dummy,
                               0,
                               SQL_VARLEN_DATA,
                               (LPCBYTE)&term,
                               1,
                               SQLCHARACTER,
                               binding.ordinal);
                break;
            }
#if PYGIM_HAVE_ARROW_STRING_VIEW
            case arrow::Type::STRING_VIEW: {
                auto typed = std::static_pointer_cast<arrow::StringViewArray>(array);
                auto text_values = std::make_shared<std::vector<std::string>>(typed->length());
                for (int64_t i = 0; i < typed->length(); ++i) {
                    if (typed->IsNull(i)) {
                        continue;
                    }
                    auto view = typed->GetView(i);
                    (*text_values)[static_cast<size_t>(i)] = std::string(view.data(), view.size());
                }
                binding.utf8_cache = text_values;
                static const uint8_t dummy = 0;
                static const uint8_t term = 0;
                ret = bcp_bind(m_dbc,
                               (LPCBYTE)&dummy,
                               0,
                               SQL_VARLEN_DATA,
                               (LPCBYTE)&term,
                               1,
                               SQLCHARACTER,
                               binding.ordinal);
                break;
            }
#endif
            default:
                throw std::runtime_error("Unsupported Arrow type: " + field->type()->ToString());
            }
            if (ret != SUCCEED) {
                std::string label = "bcp_bind(" + field->name() + ")";
                raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, label.c_str());
            }
            bindings.push_back(binding);
        }

        for (int64_t row_idx = 0; row_idx < num_rows; ++row_idx) {
            for (auto &binding : bindings) {
                if (binding.utf8_cache) {
                    if (binding.array->IsNull(row_idx)) {
                        ret = bcp_collen(m_dbc, SQL_NULL_DATA, binding.ordinal);
                        if (ret != SUCCEED) {
                            raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_collen");
                        }
                        ret = bcp_colptr(m_dbc, nullptr, binding.ordinal);
                        if (ret != SUCCEED) {
                            raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_colptr");
                        }
                        continue;
                    }

                    const auto &value = (*binding.utf8_cache)[static_cast<size_t>(row_idx)];
                    if (value.size() > static_cast<size_t>(std::numeric_limits<DBINT>::max())) {
                        throw std::runtime_error("Text value exceeds DBINT length limits for BCP");
                    }
                    DBINT len = static_cast<DBINT>(value.size());
                    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(value.c_str());
                    ret = bcp_collen(m_dbc, len, binding.ordinal);
                    if (ret != SUCCEED) {
                        raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_collen");
                    }
                    ret = bcp_colptr(m_dbc, (LPCBYTE)ptr, binding.ordinal);
                    if (ret != SUCCEED) {
                        raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_colptr");
                    }
                } else if (binding.value_stride > 0) {
                    if (binding.array->IsNull(row_idx)) {
                        ret = bcp_collen(m_dbc, SQL_NULL_DATA, binding.ordinal);
                        if (ret != SUCCEED) {
                            raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_collen");
                        }
                        ret = bcp_colptr(m_dbc, nullptr, binding.ordinal);
                        if (ret != SUCCEED) {
                            raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_colptr");
                        }
                        continue;
                    }

                    const uint8_t *ptr = static_cast<const uint8_t *>(binding.data_ptr)
                                       + (static_cast<size_t>(row_idx) * binding.value_stride);
                    if (binding.value_stride > static_cast<size_t>(std::numeric_limits<DBINT>::max())) {
                        throw std::runtime_error("Fixed-width value exceeds DBINT length limits for BCP");
                    }
                    DBINT len = static_cast<DBINT>(binding.value_stride);
                    ret = bcp_collen(m_dbc, len, binding.ordinal);
                    if (ret != SUCCEED) {
                        raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_collen");
                    }
                    ret = bcp_colptr(m_dbc, (LPCBYTE)ptr, binding.ordinal);
                    if (ret != SUCCEED) {
                        raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_colptr");
                    }
                }
            }
            ret = bcp_sendrow(m_dbc);
            if (ret != SUCCEED) {
                raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_sendrow");
                throw std::runtime_error("bcp_sendrow failed at row " + std::to_string(static_cast<long long>(row_idx)));
            }
            ++sent_rows;
            if (sent_rows > 0 && (sent_rows % batch == 0)) {
                ret = bcp_batch(m_dbc);
                if (ret == -1) {
                    raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_batch");
                }
            }
        }

        if (sent_rows > 0 && (sent_rows % batch != 0)) {
            ret = bcp_batch(m_dbc);
            if (ret == -1) {
                raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_batch");
            }
        }
    };

    if (is_arrow_c_stream_capsule) {
#if PYGIM_HAVE_ARROW_C_BRIDGE
        auto *stream = static_cast<ArrowArrayStream *>(
            PyCapsule_GetPointer(arrow_ipc_payload.ptr(), "arrow_array_stream"));
        if (stream == nullptr) {
            throw std::runtime_error("Invalid Arrow C stream capsule (null pointer)");
        }

        auto reader_result = arrow::ImportRecordBatchReader(stream);
        if (!reader_result.ok()) {
            throw std::runtime_error(
                "Failed to import Arrow C stream: " + reader_result.status().ToString());
        }

        std::shared_ptr<arrow::RecordBatchReader> record_batch_reader = reader_result.ValueOrDie();
        while (true) {
            std::shared_ptr<arrow::RecordBatch> batch_data;
            auto read_status = record_batch_reader->ReadNext(&batch_data);
            if (!read_status.ok()) {
                throw std::runtime_error(
                    "Failed reading Arrow C stream batch: " + read_status.ToString());
            }
            if (!batch_data) {
                break;
            }
            process_record_batch(batch_data);
        }
#else
        throw std::runtime_error(
            "Arrow C stream capsule provided, but Arrow C bridge headers are unavailable at build time");
#endif
    } else {
        // Stability-first path: normalize to owned bytes and parse from owned memory.
        py::bytes payload_bytes;
        if (py::isinstance<py::bytes>(arrow_ipc_payload)) {
            payload_bytes = py::reinterpret_borrow<py::bytes>(arrow_ipc_payload);
        } else {
            try {
                py::object normalized = py::bytes(arrow_ipc_payload);
                payload_bytes = py::reinterpret_borrow<py::bytes>(normalized);
            } catch (const py::error_already_set &) {
                throw std::runtime_error(
                    "arrow_ipc_payload must be bytes or bytes-convertible for IPC parsing");
            }
        }

        std::string ipc_data = payload_bytes.cast<std::string>();
        if (ipc_data.empty()) {
            return;
        }
        buffer = arrow::Buffer::FromString(std::move(ipc_data));
        payload_owner = payload_bytes;

        auto reader = std::make_shared<arrow::io::BufferReader>(buffer);
        auto file_reader_result = arrow::ipc::RecordBatchFileReader::Open(reader);
        if (file_reader_result.ok()) {
            auto file_reader = file_reader_result.ValueOrDie();
            const int num_batches = file_reader->num_record_batches();
            for (int i = 0; i < num_batches; ++i) {
                auto batch_result = file_reader->ReadRecordBatch(i);
                if (!batch_result.ok()) {
                    throw std::runtime_error(
                        "Failed to read file batch " + std::to_string(i) + ": " + batch_result.status().ToString());
                }
                process_record_batch(batch_result.ValueOrDie());
            }
        } else {
            reader = std::make_shared<arrow::io::BufferReader>(buffer);
            auto stream_reader_result = arrow::ipc::RecordBatchStreamReader::Open(reader);
            if (!stream_reader_result.ok()) {
                throw std::runtime_error(
                    "Failed to open Arrow IPC as file or stream: file=" +
                    file_reader_result.status().ToString() +
                    ", stream=" + stream_reader_result.status().ToString());
            }

            auto stream_reader = stream_reader_result.ValueOrDie();
            while (true) {
                std::shared_ptr<arrow::RecordBatch> batch_data;
                auto read_status = stream_reader->ReadNext(&batch_data);
                if (!read_status.ok()) {
                    throw std::runtime_error(
                        "Failed reading Arrow IPC stream batch: " + read_status.ToString());
                }
                if (!batch_data) {
                    break;
                }
                process_record_batch(batch_data);
            }
        }
    }
    if (processed_rows == 0) {
        throw std::runtime_error("Arrow BCP received zero rows from payload");
    }
    DBINT done = bcp_done(m_dbc);
    if (done == -1) {
        raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_done");
    }
#elif PYGIM_HAVE_ODBC
    throw std::runtime_error("bulk_insert_arrow_bcp requires Arrow C++ library (not detected at build time)");
#else
    throw std::runtime_error("MssqlStrategyNative built without ODBC headers; feature unavailable");
#endif
}

} // namespace pygim
