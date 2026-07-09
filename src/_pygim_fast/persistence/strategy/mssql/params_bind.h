// persistence/strategy/mssql/params_bind.h
// ODBC parameter binding for core::QueryParam values ('?' markers).
//
// Strings are bound as UTF-16 (SQL_C_WCHAR / SQL_WVARCHAR) so non-ASCII
// predicate values round-trip regardless of the client code page. The
// returned storage must outlive statement execution — ODBC reads the bound
// buffers at SQLExecute time.

#pragma once

#include "odbc_error.h"
#include "../../core/query.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pygim::strategy::mssql {

/// Owned storage for one bound parameter (stable address via unique_ptr).
struct BoundParam {
    std::u16string wtext;
    long long      i64{0};
    double         f64{0.0};
    unsigned char  bit{0};
    SQLLEN         indicator{0};
};

/// Minimal, strict UTF-8 → UTF-16 (with surrogate pairs) conversion.
[[nodiscard]] inline std::u16string utf8_to_utf16(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    std::size_t i = 0;
    auto fail = [&] { throw std::runtime_error("invalid UTF-8 in query parameter"); };
    while (i < s.size()) {
        const unsigned char b0 = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        std::size_t len = 0;
        if (b0 < 0x80) { cp = b0; len = 1; }
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; len = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; len = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; len = 4; }
        else fail();
        if (i + len > s.size()) fail();
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char bk = static_cast<unsigned char>(s[i + k]);
            if ((bk & 0xC0) != 0x80) fail();
            cp = (cp << 6) | (bk & 0x3F);
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) fail();
        // Reject overlong encodings: a code point must use the shortest form.
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000)) fail();
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
        i += len;
    }
    return out;
}

/// Bind all parameters to the prepared statement, 1-based, in order.
/// Returns the owned buffers; keep them alive until fetching is complete.
[[nodiscard]] inline std::vector<std::unique_ptr<BoundParam>>
bind_parameters(SQLHSTMT stmt, const std::vector<core::QueryParam>& params) {
    std::vector<std::unique_ptr<BoundParam>> storage;
    storage.reserve(params.size());

    for (std::size_t i = 0; i < params.size(); ++i) {
        auto owned = std::make_unique<BoundParam>();
        const auto ipar = static_cast<SQLUSMALLINT>(i + 1);
        SQLRETURN ret = SQL_SUCCESS;

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    owned->indicator = SQL_NULL_DATA;
                    ret = SQLBindParameter(stmt, ipar, SQL_PARAM_INPUT,
                                           SQL_C_WCHAR, SQL_WVARCHAR, 1, 0,
                                           nullptr, 0, &owned->indicator);
                } else if constexpr (std::is_same_v<T, bool>) {
                    owned->bit = value ? 1 : 0;
                    ret = SQLBindParameter(stmt, ipar, SQL_PARAM_INPUT,
                                           SQL_C_BIT, SQL_BIT, 1, 0,
                                           &owned->bit, 0, nullptr);
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    owned->i64 = value;
                    ret = SQLBindParameter(stmt, ipar, SQL_PARAM_INPUT,
                                           SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
                                           &owned->i64, 0, nullptr);
                } else if constexpr (std::is_same_v<T, double>) {
                    owned->f64 = value;
                    ret = SQLBindParameter(stmt, ipar, SQL_PARAM_INPUT,
                                           SQL_C_DOUBLE, SQL_DOUBLE, 0, 0,
                                           &owned->f64, 0, nullptr);
                } else {  // std::string
                    owned->wtext = utf8_to_utf16(value);
                    owned->indicator = static_cast<SQLLEN>(owned->wtext.size() * 2);
                    // nvarchar(n) caps at 4000 UTF-16 code units; beyond that
                    // SQL_WVARCHAR raises HY104 (invalid precision), so switch
                    // to SQL_WLONGVARCHAR (nvarchar(max)) for long values.
                    const bool is_long = owned->wtext.size() > 4000;
                    const SQLSMALLINT sql_type = is_long ? SQL_WLONGVARCHAR : SQL_WVARCHAR;
                    const SQLULEN column_size = is_long
                        ? owned->wtext.size()
                        : (owned->wtext.empty() ? 1 : owned->wtext.size());
                    // std::u16string::data() is valid (points at the NUL) even
                    // when empty, so it is always safe to pass — no nullptr case.
                    ret = SQLBindParameter(
                        stmt, ipar, SQL_PARAM_INPUT, SQL_C_WCHAR, sql_type,
                        column_size, 0,
                        static_cast<SQLPOINTER>(owned->wtext.data()),
                        owned->indicator, &owned->indicator);
                }
            },
            params[i]);

        odbc::raise_if_error(ret, SQL_HANDLE_STMT, stmt, "SQLBindParameter");
        storage.push_back(std::move(owned));
    }
    return storage;
}

} // namespace pygim::strategy::mssql
