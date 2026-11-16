#pragma once
// core/primitives/parallel/crew.h — the threads the pools start.
//
// CORE layer: pybind-free.



#include "../../../platform/process.h"

namespace pygim::primitives::parallel {

// ── the threads the pools start ─────────────────────────────────────────

// The process's calls, from behind the platform boundary (platform/process.h).
using platform::name_this_thread;
using platform::signals_blocked;

}  // namespace pygim::primitives::parallel
