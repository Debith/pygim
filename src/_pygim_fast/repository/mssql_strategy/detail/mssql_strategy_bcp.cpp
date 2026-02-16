#include "../mssql_strategy.h"
#include "helpers.h"
#include "../../../utils/logging.h"

#if PYGIM_HAVE_ODBC && PYGIM_HAVE_ARROW
#if !defined(_WIN32) && !defined(_WIN64)
#include <dlfcn.h>
#endif
#include <arrow/ipc/api.h>
#include <arrow/io/api.h>
#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/buffer.h>
#include <chrono>
#include <codecvt>
#include <limits>
#include <locale>

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
typedef short DBINT;
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
                                                const py::bytes &arrow_ipc_bytes,
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
    if (!g_bcp_init || !g_bcp_bind || !g_bcp_sendrow || !g_bcp_batch || !g_bcp_done) {
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
    std::string ipc_data = arrow_ipc_bytes;
    auto buffer = arrow::Buffer::FromString(std::move(ipc_data));
    auto reader = std::make_shared<arrow::io::BufferReader>(buffer);
    auto reader_result = arrow::ipc::RecordBatchFileReader::Open(reader);
    if (!reader_result.ok()) {
        throw std::runtime_error("Failed to open Arrow IPC: " + reader_result.status().ToString());
    }
    auto file_reader = reader_result.ValueOrDie();
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    const int num_batches = file_reader->num_record_batches();
    for (int i = 0; i < num_batches; ++i) {
        auto batch_result = file_reader->ReadRecordBatch(i);
        if (!batch_result.ok()) {
            throw std::runtime_error("Failed to read batch " + std::to_string(i) + ": " + batch_result.status().ToString());
        }
        batches.push_back(batch_result.ValueOrDie());
    }
    auto table_result = arrow::Table::FromRecordBatches(file_reader->schema(), batches);
    if (!table_result.ok()) {
        throw std::runtime_error("Failed to create Arrow table: " + table_result.status().ToString());
    }
    std::shared_ptr<arrow::Table> arrow_table = table_result.ValueOrDie();
    const int64_t num_rows = arrow_table->num_rows();
    const int num_cols = arrow_table->num_columns();
    if (num_rows == 0 || num_cols == 0) {
        return;
    }

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

    std::vector<ColumnBinding> bindings;
    bindings.reserve(num_cols);

    for (int col_idx = 0; col_idx < num_cols; ++col_idx) {
        auto column = arrow_table->column(col_idx);
        auto field = arrow_table->schema()->field(col_idx);
        auto array = column->chunk(0);
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
            auto buffer = std::make_shared<std::vector<SQL_DATE_STRUCT>>(typed->length());
            const int32_t *raw = typed->raw_values();
            for (int64_t i = 0; i < typed->length(); ++i) {
                (*buffer)[static_cast<size_t>(i)] = days_to_sql_date(raw[i]);
            }
            binding.date_buffer = buffer;
            binding.data_ptr = buffer->data();
            binding.value_stride = sizeof(SQL_DATE_STRUCT);
            ret = bcp_bind(m_dbc,
                           (LPCBYTE)binding.data_ptr,
                           0,
                           sizeof(SQL_DATE_STRUCT),
                           nullptr,
                           0,
                           SQLDATEN,
                           binding.ordinal);
            break;
        }
        case arrow::Type::TIMESTAMP: {
            auto typed = std::static_pointer_cast<arrow::TimestampArray>(array);
            auto buffer = std::make_shared<std::vector<SQL_TIMESTAMP_STRUCT>>(typed->length());
            const int64_t *raw = typed->raw_values();
            for (int64_t i = 0; i < typed->length(); ++i) {
                (*buffer)[static_cast<size_t>(i)] = micros_to_sql_timestamp(raw[i]);
            }
            binding.timestamp_buffer = buffer;
            binding.data_ptr = buffer->data();
            binding.value_stride = sizeof(SQL_TIMESTAMP_STRUCT);
            ret = bcp_bind(m_dbc,
                           (LPCBYTE)binding.data_ptr,
                           0,
                           sizeof(SQL_TIMESTAMP_STRUCT),
                           nullptr,
                           0,
                           SQLDATETIME2N,
                           binding.ordinal);
            break;
        }
        case arrow::Type::STRING: {
            auto typed = std::static_pointer_cast<arrow::StringArray>(array);
            binding.offsets32 = typed->raw_value_offsets();
            binding.str_data = typed->value_data()->data();
            DBINT max_len = 0;
            int64_t value_count = typed->length();
            auto utf16_values = std::make_shared<std::vector<std::u16string>>();
            utf16_values->reserve(static_cast<size_t>(value_count));
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
            for (int64_t i = 0; i < value_count; ++i) {
                const char *start = reinterpret_cast<const char *>(binding.str_data + binding.offsets32[i]);
                const char *end = reinterpret_cast<const char *>(binding.str_data + binding.offsets32[i + 1]);
                std::u16string converted = converter.from_bytes(start, end);
                DBINT byte_len = static_cast<DBINT>(converted.size() * sizeof(char16_t));
                if (byte_len > max_len) {
                    max_len = byte_len;
                }
                utf16_values->push_back(std::move(converted));
            }
            binding.utf16_cache = utf16_values;
            binding.max_length = std::max<DBINT>(static_cast<DBINT>(sizeof(char16_t)), max_len);
            const uint8_t *initial_ptr = nullptr;
            if (!utf16_values->empty()) {
                initial_ptr = reinterpret_cast<const uint8_t *>((*utf16_values)[0].data());
            } else {
                static const char16_t empty_char = u'\0';
                initial_ptr = reinterpret_cast<const uint8_t *>(&empty_char);
            }
            ret = bcp_bind(m_dbc, (LPCBYTE)initial_ptr, 0, binding.max_length, nullptr, 0, SQLNCHAR, binding.ordinal);
            break;
        }
        case arrow::Type::LARGE_STRING: {
            auto typed = std::static_pointer_cast<arrow::LargeStringArray>(array);
            binding.offsets64 = typed->raw_value_offsets();
            binding.str_data = typed->value_data()->data();
            DBINT max_len = 0;
            int64_t value_count = typed->length();
            auto utf16_values = std::make_shared<std::vector<std::u16string>>();
            utf16_values->reserve(static_cast<size_t>(value_count));
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
            for (int64_t i = 0; i < value_count; ++i) {
                int64_t len64 = binding.offsets64[i + 1] - binding.offsets64[i];
                if (len64 > static_cast<int64_t>(std::numeric_limits<DBINT>::max())) {
                    throw std::runtime_error("String length exceeds DBINT limits for BCP");
                }
                const char *start = reinterpret_cast<const char *>(binding.str_data + binding.offsets64[i]);
                const char *end = reinterpret_cast<const char *>(binding.str_data + binding.offsets64[i + 1]);
                std::u16string converted = converter.from_bytes(start, end);
                DBINT byte_len = static_cast<DBINT>(converted.size() * sizeof(char16_t));
                if (byte_len > max_len) {
                    max_len = byte_len;
                }
                utf16_values->push_back(std::move(converted));
            }
            binding.utf16_cache = utf16_values;
            binding.max_length = std::max<DBINT>(static_cast<DBINT>(sizeof(char16_t)), max_len);
            const uint8_t *initial_ptr = nullptr;
            if (!utf16_values->empty()) {
                initial_ptr = reinterpret_cast<const uint8_t *>((*utf16_values)[0].data());
            } else {
                static const char16_t empty_char = u'\0';
                initial_ptr = reinterpret_cast<const uint8_t *>(&empty_char);
            }
            ret = bcp_bind(m_dbc, (LPCBYTE)initial_ptr, 0, binding.max_length, nullptr, 0, SQLNCHAR, binding.ordinal);
            break;
        }
        default:
            throw std::runtime_error("Unsupported Arrow type: " + field->type()->ToString());
        }
        if (ret != SUCCEED) {
            std::string label = "bcp_bind(" + field->name() + ")";
            raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, label.c_str());
        }
        bindings.push_back(binding);
    }

    const DBINT batch = (batch_size > 0) ? batch_size : 100000;
    for (DBINT row_idx = 0; row_idx < static_cast<DBINT>(num_rows); ++row_idx) {
        for (auto &binding : bindings) {
            if (binding.utf16_cache) {
                const auto &value = (*binding.utf16_cache)[row_idx];
                DBINT len = static_cast<DBINT>(value.size() * sizeof(char16_t));
                const uint8_t *ptr = reinterpret_cast<const uint8_t *>(value.data());
                ret = bcp_collen(m_dbc, len, binding.ordinal);
                if (ret != SUCCEED) {
                    raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_collen");
                }
                ret = bcp_colptr(m_dbc, (LPCBYTE)ptr, binding.ordinal);
                if (ret != SUCCEED) {
                    raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_colptr");
                }
            } else if (binding.data_ptr && binding.value_stride > 0) {
                auto base = reinterpret_cast<const uint8_t *>(binding.data_ptr);
                const uint8_t *ptr = base + (row_idx * binding.value_stride);
                ret = bcp_colptr(m_dbc, (LPCBYTE)ptr, binding.ordinal);
                if (ret != SUCCEED) {
                    raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_colptr");
                }
            }
        }
        ret = bcp_sendrow(m_dbc);
        if (ret != SUCCEED) {
            raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_sendrow");
            throw std::runtime_error("bcp_sendrow failed at row " + std::to_string(row_idx));
        }
        if (row_idx > 0 && (row_idx % batch == 0)) {
            ret = bcp_batch(m_dbc);
            if (ret == -1) {
                raise_if_error(SQL_ERROR, SQL_HANDLE_DBC, m_dbc, "bcp_batch");
            }
        }
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
