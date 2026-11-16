// persistence/odbc_headers.h
// The one true way to include the ODBC headers.
//
// On Windows the SDK's <sqltypes.h> uses DWORD/WORD/INT64 etc. without
// declaring them: <windows.h> MUST be included first or it fails with
// "missing type specifier - int assumed" (observed on CI, SDK 10.0.26100).
// Include this wrapper instead of <sql.h>/<sqlext.h>/<sqltypes.h> directly.

#pragma once

#include "../platform/windows_headers.h"   // first: the SDK's headers need it on Windows

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
