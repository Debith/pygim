// repository/odbc_compat.h
// Shared ODBC type definitions for platform compatibility.
// SQL Server extended types not defined in standard unixODBC headers.

#pragma once

#include <sql.h>
#include <sqltypes.h>

// ODBC headers pollute the macro namespace — clean up.
#ifdef BOOL
#  undef BOOL
#endif
#ifdef INT
#  undef INT
#endif

/// SQL Server extended TIME type (not in standard ODBC headers on Linux).
inline constexpr SQLSMALLINT kSQL_SS_TIME2 = -154;

// SQL_SS_TIME2_STRUCT — SQL Server TIME(n) with fractional seconds.
// Defined in msodbcsql.h on Windows; replicated for Linux portability.
#ifndef SQL_SS_TIME2_STRUCT_DEFINED
#define SQL_SS_TIME2_STRUCT_DEFINED
typedef struct tagSS_TIME2_STRUCT {
    SQLUSMALLINT hour;
    SQLUSMALLINT minute;
    SQLUSMALLINT second;
    SQLUINTEGER  fraction;  // in 100-nanosecond units
} SQL_SS_TIME2_STRUCT;
#endif
