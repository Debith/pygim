#pragma once
// platform/windows_headers.h — <windows.h> as pygim includes it: lean (WIN32_LEAN_AND_MEAN), and
// without its min and max macros (NOMINMAX), which would break std::min and std::max. On every
// other system, nothing. Every header that calls the Windows API includes this, never <windows.h>
// itself, so the first to be included cannot bring in the macros the others keep out.
//
// CORE layer: pybind-free.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
