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

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace pygim::strategy::mssql::bcp {

#ifdef PYGIM_BCP_PROFILING

/// Per-worker profiling accumulator.  All durations in seconds.
struct BcpProfiler {
    using clock = std::chrono::steady_clock;
    using tp    = clock::time_point;

    // ── Accumulated durations (seconds) ─────────────────────────────────
    double bind_seconds{0};           ///< Full bind_columns (first batch).
    double rebind_seconds{0};         ///< Fast rebind_columns (subsequent batches).
    double classify_seconds{0};       ///< classify_columns + setup_staging.
    double fixed_copy_seconds{0};     ///< Fixed-width memcpy in row loop.
    double string_copy_seconds{0};    ///< String/binary handle_string_column.
    double sendrow_seconds{0};        ///< bcp_sendrow calls.
    double mid_flush_seconds{0};      ///< Mid-loop bcp_batch flushes.
    double final_flush_seconds{0};    ///< finalize_bcp (final batch + done).
    double init_session_seconds{0};   ///< bcp_init + hints.
    double reader_next_seconds{0};    ///< TableBatchReader::ReadNext.

    // ── Counters ────────────────────────────────────────────────────────
    int64_t sendrow_calls{0};
    int64_t mid_flush_calls{0};
    int64_t string_calls{0};          ///< Per-cell string handling invocations.
    int64_t fixed_calls{0};           ///< Per-cell fixed copy invocations.
    int64_t rebind_calls{0};
    int64_t bind_calls{0};

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
        bind_seconds        = std::max(bind_seconds, other.bind_seconds);
        rebind_seconds      = std::max(rebind_seconds, other.rebind_seconds);
        classify_seconds    = std::max(classify_seconds, other.classify_seconds);
        fixed_copy_seconds  = std::max(fixed_copy_seconds, other.fixed_copy_seconds);
        string_copy_seconds = std::max(string_copy_seconds, other.string_copy_seconds);
        sendrow_seconds     = std::max(sendrow_seconds, other.sendrow_seconds);
        mid_flush_seconds   = std::max(mid_flush_seconds, other.mid_flush_seconds);
        final_flush_seconds = std::max(final_flush_seconds, other.final_flush_seconds);
        init_session_seconds = std::max(init_session_seconds, other.init_session_seconds);
        reader_next_seconds = std::max(reader_next_seconds, other.reader_next_seconds);
        sendrow_calls       += other.sendrow_calls;
        mid_flush_calls     += other.mid_flush_calls;
        string_calls        += other.string_calls;
        fixed_calls         += other.fixed_calls;
        rebind_calls        += other.rebind_calls;
        bind_calls          += other.bind_calls;
        return *this;
    }
};

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
