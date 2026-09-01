// persistence/odbc_headers.h
// The one true way to include the ODBC headers.
//
// On Windows the SDK's <sqltypes.h> uses DWORD/WORD/INT64 etc. without
// declaring them: <windows.h> MUST be included first or it fails with
// "missing type specifier - int assumed" (observed on CI, SDK 10.0.26100).
// Include this wrapper instead of <sql.h>/<sqlext.h>/<sqltypes.h> directly.

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // keep std::min/std::max usable
#endif
#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

// ODBC/Windows headers pollute the macro namespace — clean up what collides
// with C++ identifiers used in this codebase.
#ifdef BOOL
#  undef BOOL
#endif
#ifdef INT
#  undef INT
#endif
