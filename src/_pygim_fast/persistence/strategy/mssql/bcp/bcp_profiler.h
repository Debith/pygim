#pragma once
// BCP Profiler: exhaustive per-section timing instrumentation.
//
// Compile with -DPYGIM_BCP_PROFILING to enable.  When disabled, all macros
// expand to nothing — zero runtime overhead.
//
// Usage in hot paths:
//   BCP_PROF_ACCUM(prof, bind)    { ... expensive bind ... }
//   BCP_PROF_ACCUM(prof, sendrow) { bcp.sendrow(dbc); }
//
// Each worker owns a BcpProfiler instance.  After the row loop, call
// prof.dump() for stderr output or read fields directly.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace pygim::strategy::mssql::bcp {

#ifdef PYGIM_BCP_PROFILING

#define PYGIM_BCP_APPLY_sum(lhs, rhs, field) ((lhs).field += (rhs).field)
#define PYGIM_BCP_APPLY_max(lhs, rhs, field) ((lhs).field = std::max((lhs).field, (rhs).field))

#define PYGIM_BCP_PROFILER_FIELDS(X) \
    X(bind_seconds, double, max) \
    X(rebind_seconds, double, max) \
    X(classify_seconds, double, max) \
    X(fixed_copy_seconds, double, max) \
    X(string_copy_seconds, double, max) \
    X(sendrow_seconds, double, max) \
    X(mid_flush_seconds, double, max) \
    X(final_flush_seconds, double, max) \
    X(init_session_seconds, double, max) \
    X(reader_next_seconds, double, max) \
    X(sendrow_calls, int64_t, sum) \
    X(mid_flush_calls, int64_t, sum) \
    X(string_calls, int64_t, sum) \
    X(fixed_calls, int64_t, sum) \
    X(rebind_calls, int64_t, sum) \
    X(bind_calls, int64_t, sum)

/// Per-worker profiling accumulator.  All durations in seconds.
struct BcpProfiler {
    using clock = std::chrono::steady_clock;
    using tp    = clock::time_point;

    // Durations (seconds): bind, rebind, classify, fixed/string copy,
    // sendrow, flushes, session init, and reader-next.
    // Counters: sendrow, mid_flush, string/fixed cell handling, rebind, bind.
#define PYGIM_BCP_DECLARE_PROFILER_FIELD(name, type, merge_policy) type name{0};
    PYGIM_BCP_PROFILER_FIELDS(PYGIM_BCP_DECLARE_PROFILER_FIELD)
#undef PYGIM_BCP_DECLARE_PROFILER_FIELD

    /// Dump to stderr for quick inspection.
    void dump(int worker_id = -1) const noexcept {
        const char* prefix = (worker_id >= 0)
            ? ""  // will format with worker id
            : "  ";
        char hdr[64]{};
        if (worker_id >= 0)
            std::snprintf(hdr, sizeof(hdr), "  [worker %d] ", worker_id);
        else
            std::snprintf(hdr, sizeof(hdr), "  [profile] ");

        std::fprintf(stderr,
            "%sinit_session:  %8.4fs\n"
            "%sbind:          %8.4fs  (%lld calls)\n"
            "%srebind:        %8.4fs  (%lld calls)\n"
            "%sclassify:      %8.4fs\n"
            "%sfixed_copy:    %8.4fs  (%lld cells)\n"
            "%sstring_copy:   %8.4fs  (%lld cells)\n"
            "%ssendrow:       %8.4fs  (%lld calls)\n"
            "%smid_flush:     %8.4fs  (%lld calls)\n"
            "%sfinal_flush:   %8.4fs\n"
            "%sreader_next:   %8.4fs\n",
            hdr, init_session_seconds,
            hdr, bind_seconds,     static_cast<long long>(bind_calls),
            hdr, rebind_seconds,   static_cast<long long>(rebind_calls),
            hdr, classify_seconds,
            hdr, fixed_copy_seconds, static_cast<long long>(fixed_calls),
            hdr, string_copy_seconds, static_cast<long long>(string_calls),
            hdr, sendrow_seconds,  static_cast<long long>(sendrow_calls),
            hdr, mid_flush_seconds, static_cast<long long>(mid_flush_calls),
            hdr, final_flush_seconds,
            hdr, reader_next_seconds);
    }

    /// Merge another profiler (max for durations, sum for counts).
    BcpProfiler& merge_parallel(const BcpProfiler& other) noexcept {
#define PYGIM_BCP_MERGE_PROFILER_FIELD(name, type, merge_policy) \
        PYGIM_BCP_APPLY_##merge_policy(*this, other, name);
        PYGIM_BCP_PROFILER_FIELDS(PYGIM_BCP_MERGE_PROFILER_FIELD)
#undef PYGIM_BCP_MERGE_PROFILER_FIELD
        return *this;
    }
};

#undef PYGIM_BCP_PROFILER_FIELDS
#undef PYGIM_BCP_APPLY_max
#undef PYGIM_BCP_APPLY_sum

/// RAII scope timer: accumulates elapsed time into a target double on destruction.
struct ProfileScope {
    using clock = std::chrono::steady_clock;
    double& target;
    clock::time_point start;

    explicit ProfileScope(double& t) noexcept
        : target(t), start(clock::now()) {}

    ~ProfileScope() noexcept {
        target += std::chrono::duration<double>(clock::now() - start).count();
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
};

// ── Macros ──────────────────────────────────────────────────────────────────

/// Declare a BcpProfiler local variable.
#define BCP_PROF_DECL(name)  ::pygim::strategy::mssql::bcp::BcpProfiler name{}

/// Time a code block, accumulating into prof.field_seconds.
/// Usage: BCP_PROF_SCOPE(prof, bind) { ... }   — note: wrap body in braces
#define BCP_PROF_SCOPE(prof, field) \
    ::pygim::strategy::mssql::bcp::ProfileScope _prof_##field##_(prof.field##_seconds)

/// Increment a counter: BCP_PROF_COUNT(prof, sendrow_calls)
#define BCP_PROF_COUNT(prof, field) ++(prof).field

/// Dump profiler to stderr.
#define BCP_PROF_DUMP(prof, worker_id) (prof).dump(worker_id)

#else // PYGIM_BCP_PROFILING not defined — everything compiles away

struct BcpProfiler {
    void dump(int = -1) const noexcept {}
    BcpProfiler& merge_parallel(const BcpProfiler&) noexcept { return *this; }
};

struct ProfileScope {
    explicit ProfileScope(double&) noexcept {}
};

#define BCP_PROF_DECL(name)                 ::pygim::strategy::mssql::bcp::BcpProfiler name{}
#define BCP_PROF_SCOPE(prof, field)         ((void)0)
#define BCP_PROF_COUNT(prof, field)         ((void)0)
#define BCP_PROF_DUMP(prof, worker_id)      ((void)0)

#endif // PYGIM_BCP_PROFILING

} // namespace pygim::strategy::mssql::bcp
