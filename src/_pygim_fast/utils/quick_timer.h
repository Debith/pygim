#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "../mapping/dynamic_merge_map.h"

namespace pygim {

// ── QuickTimerT ──────────────────────────────────────────────────
// Zero-heap-allocation RAII timer with fixed-capacity sub-timers.
// All output via FILE* (no <iostream>, no virtual dispatch).
//
// Two modes selected by the PhaseEnum template parameter:
//
//  String mode (PhaseEnum = void, default):
//    Sub-timers created at runtime, identified by string_view names.
//    32 bytes per sub-timer. Bounds checking on start/stop.
//    ~312 bytes total (MaxSubTimers=8).
//
//  Enum mode (PhaseEnum = enum with COUNT sentinel):
//    Sub-timers indexed by enum value — zero bounds checking.
//    16 bytes per sub-timer (no name field).
//    Names resolved at print time via ADL: phase_name(PhaseEnum).
//    Capacity = static_cast<size_t>(PhaseEnum::COUNT).

namespace detail {
template <typename E> struct timer_capacity {
    static constexpr std::size_t value = static_cast<std::size_t>(E::COUNT);
};
template <> struct timer_capacity<void> {
    static constexpr std::size_t value = 8;
};
} // namespace detail

template <typename PhaseEnum = void,
          std::size_t MaxSubTimers = detail::timer_capacity<PhaseEnum>::value>
struct TimerSnapshotT {
    static constexpr bool kEnumMode = !std::is_void_v<PhaseEnum>;
    using TimerId = std::conditional_t<kEnumMode, PhaseEnum, std::size_t>;
    using PhaseMap = mapping::DynamicMergeMap<std::size_t, double>;

    std::string_view name{};
    double total_seconds{0.0};
    std::size_t sub_timer_count{0};
    PhaseMap phase_seconds{};
    std::array<std::string_view, MaxSubTimers> phase_names{};

    double sub_timer_seconds(TimerId phase) const
        requires (kEnumMode) {
        return phase_seconds.at(static_cast<std::size_t>(phase));
    }

    double sub_timer_seconds(std::string_view phase_name) const
        requires (!kEnumMode) {
        for (std::size_t i = 0; i < sub_timer_count; ++i) {
            if (phase_names[i] == phase_name) return phase_seconds.at(i);
        }
        throw std::out_of_range("sub timer not found in snapshot");
    }

    TimerSnapshotT& merge_with(const TimerSnapshotT& other) {
        total_seconds += other.total_seconds;
        phase_seconds = phase_seconds | other.phase_seconds;
        const auto count = std::max(sub_timer_count, other.sub_timer_count);
        for (std::size_t i = 0; i < count; ++i) {
            if (phase_names[i].empty()) phase_names[i] = other.phase_names[i];
        }
        sub_timer_count = count;
        return *this;
    }

    friend TimerSnapshotT operator|(TimerSnapshotT lhs, const TimerSnapshotT& rhs) {
        return lhs.merge_with(rhs);
    }
};

template <typename PhaseEnum = void,
          std::size_t MaxSubTimers = detail::timer_capacity<PhaseEnum>::value>
class QuickTimerT {
public:
    static constexpr bool kEnumMode = !std::is_void_v<PhaseEnum>;
    using TimerId = std::conditional_t<kEnumMode, PhaseEnum, std::size_t>;
    using Snapshot = TimerSnapshotT<PhaseEnum, MaxSubTimers>;
    static constexpr std::size_t kMaxSubTimers = MaxSubTimers;

private:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t kNoActive = static_cast<std::size_t>(-1);
    static constexpr Clock::time_point kNotRunning{};

    // String mode: 32 bytes (stores name). Enum mode: 16 bytes (name external).
    struct StringSub {
        std::string_view name;            // 16
        Clock::time_point started_at{};   //  8
        double seconds{0.0};              //  8
    };
    struct EnumSub {
        Clock::time_point started_at{};   //  8
        double seconds{0.0};              //  8
    };
    using SubTimer = std::conditional_t<kEnumMode, EnumSub, StringSub>;

    static constexpr std::size_t to_index(TimerId id) {
        if constexpr (kEnumMode) return static_cast<std::size_t>(id);
        else                     return id;
    }

    // Cold path only — resolves name for printing.
    std::string_view get_sub_name(std::size_t index) const {
        if constexpr (kEnumMode) return phase_name(static_cast<PhaseEnum>(index));
        else                     return m_sub_timers[index].name;
    }

public:
    explicit QuickTimerT(std::string_view name = "QuickTimer",
                         std::FILE* output = stderr,
                         bool print_on_end = true,
                         bool print_on_start = true)
        : m_name(name),
          m_output(output),
          m_started_at(Clock::now()),
          m_active_sub(kNoActive),
          m_sub_timer_count(kEnumMode ? MaxSubTimers : 0),
          m_print_on_end(print_on_end),
          m_print_on_start(print_on_start) {
        if (m_print_on_start) {
            print_started(m_name);
        }
    }

    ~QuickTimerT() noexcept {
        if (!m_print_on_end || m_output == nullptr) return;
        try { print_summary(); } catch (...) {}
    }

    // Non-copyable, movable.
    QuickTimerT(const QuickTimerT&) = delete;
    QuickTimerT& operator=(const QuickTimerT&) = delete;
    QuickTimerT(QuickTimerT&&) = default;
    QuickTimerT& operator=(QuickTimerT&&) = default;

    // ── Sub-timer API (string mode only) ─────────────────────────

    void start_sub_timer(std::string_view name, bool print_on_start = true)
        requires (!kEnumMode) {
        start_sub_timer(get_or_create_sub_timer_id(name), print_on_start);
    }

    [[nodiscard]] TimerId get_or_create_sub_timer_id(std::string_view name)
        requires (!kEnumMode) {
        for (std::size_t i = 0; i < m_sub_timer_count; ++i) {
            if (m_sub_timers[i].name == name) return i;
        }
        if (m_sub_timer_count >= MaxSubTimers) [[unlikely]] {
            throw std::out_of_range("sub timer capacity exceeded");
        }
        const TimerId id = m_sub_timer_count++;
        m_sub_timers[id].name = name;
        return id;
    }

    // ── Sub-timer API (common — takes TimerId) ──────────────────

    void start_sub_timer(TimerId id, bool print_on_start = true) {
        const auto index = to_index(id);
        if constexpr (!kEnumMode) {
            if (index >= m_sub_timer_count) [[unlikely]] {
                throw std::out_of_range("sub timer id out of range");
            }
        }
        if (m_active_sub != kNoActive) {
            stop_sub_timer_impl(m_active_sub, false);
        }
        m_sub_timers[index].started_at = Clock::now();
        m_active_sub = index;
        if (print_on_start) {
            print_started(get_sub_name(index));
        }
    }

    double stop_sub_timer(std::string_view name = {}, bool print_now = true)
        requires (!kEnumMode) {
        return stop_sub_timer_impl(resolve_sub_timer_index(name), print_now);
    }

    double stop_sub_timer(TimerId id, bool print_now = true) {
        const auto index = to_index(id);
        if constexpr (!kEnumMode) {
            if (index >= m_sub_timer_count) [[unlikely]] {
                throw std::out_of_range("sub timer id out of range");
            }
        }
        return stop_sub_timer_impl(index, print_now);
    }

    // ── Queries ──────────────────────────────────────────────────

    double total_seconds() const {
        return std::chrono::duration<double>(Clock::now() - m_started_at).count();
    }

    double sub_timer_seconds(std::string_view name) const
        requires (!kEnumMode) {
        for (std::size_t i = 0; i < m_sub_timer_count; ++i) {
            if (m_sub_timers[i].name == name) {
                return snapshot_sub_seconds(m_sub_timers[i]);
            }
        }
        throw std::out_of_range("sub timer not found");
    }

    double sub_timer_seconds(TimerId phase) const
        requires (kEnumMode) {
        return snapshot_sub_seconds(m_sub_timers[to_index(phase)]);
    }

    std::string_view name() const { return m_name; }
    std::size_t sub_timer_count() const { return m_sub_timer_count; }
    Snapshot snapshot() const {
        Snapshot report;
        report.name = m_name;
        report.total_seconds = total_seconds();
        report.sub_timer_count = m_sub_timer_count;
        for (std::size_t i = 0; i < m_sub_timer_count; ++i) {
            report.phase_seconds.set(i, snapshot_sub_seconds(m_sub_timers[i]));
            report.phase_names[i] = get_sub_name(i);
        }
        return report;
    }

    // ── Output ───────────────────────────────────────────────────

    void print_summary() {
        if (m_output == nullptr) return;
        const auto report = snapshot();
        std::fprintf(m_output, "%.*s total: %ss\n",
                     static_cast<int>(report.name.size()), report.name.data(),
                     fmt_seconds(report.total_seconds));
        for (std::size_t i = 0; i < report.sub_timer_count; ++i) {
            const auto sub_name = report.phase_names[i];
            std::fprintf(m_output, "  - %.*s: %ss\n",
                         static_cast<int>(sub_name.size()),
                         sub_name.data(),
                         fmt_seconds(report.phase_seconds.value_or(i, 0.0)));
        }
    }

private:
    // ── Helpers ──────────────────────────────────────────────────

    static bool is_running(const SubTimer& sub) {
        return sub.started_at != kNotRunning;
    }

    static double snapshot_sub_seconds(const SubTimer& sub) {
        double secs = sub.seconds;
        if (is_running(sub)) {
            secs += std::chrono::duration<double>(Clock::now() - sub.started_at).count();
        }
        return secs;
    }

    double stop_sub_timer_impl(std::size_t index, bool print_now) {
        SubTimer& sub = m_sub_timers[index];
        if (!is_running(sub)) {
            if (print_now) print_single(get_sub_name(index), sub.seconds);
            return sub.seconds;
        }
        const double elapsed =
            std::chrono::duration<double>(Clock::now() - sub.started_at).count();
        sub.seconds += elapsed;
        sub.started_at = kNotRunning;
        if (m_active_sub == index) {
            m_active_sub = kNoActive;
        }
        if (print_now) print_single(get_sub_name(index), sub.seconds);
        return sub.seconds;
    }

    std::size_t resolve_sub_timer_index(std::string_view name) const
        requires (!kEnumMode) {
        if (!name.empty()) {
            for (std::size_t i = 0; i < m_sub_timer_count; ++i) {
                if (m_sub_timers[i].name == name) return i;
            }
            throw std::out_of_range("sub timer not found");
        }
        if (m_active_sub == kNoActive) [[unlikely]] {
            throw std::runtime_error("no active sub timer");
        }
        return m_active_sub;
    }

    void print_single(std::string_view sv_name, double seconds) {
        if (m_output == nullptr) return;
        std::fprintf(m_output, "%.*s [%.*s]: %ss\n",
                     static_cast<int>(m_name.size()), m_name.data(),
                     static_cast<int>(sv_name.size()), sv_name.data(),
                     fmt_seconds(seconds));
    }

    void print_started(std::string_view sv_name) {
        if (m_output == nullptr) return;
        std::fprintf(m_output, "%.*s [%.*s]: started\n",
                     static_cast<int>(m_name.size()), m_name.data(),
                     static_cast<int>(sv_name.size()), sv_name.data());
    }

    // Format seconds into a thread-local buffer via std::to_chars (fast, no locale).
    static const char* fmt_seconds(double seconds) {
        thread_local char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf) - 1, seconds,
                                       std::chars_format::fixed, 6);
        *ptr = '\0';
        return buf;
    }

    // ── Data members ─────────────────────────────────────────────
    std::string_view m_name;
    std::FILE* m_output;
    Clock::time_point m_started_at;
    std::size_t m_active_sub;
    std::size_t m_sub_timer_count;
    bool m_print_on_end;
    bool m_print_on_start;
    std::array<SubTimer, MaxSubTimers> m_sub_timers{};
};

// Default alias — string mode, 8 sub-timers, ~312 bytes, zero heap.
using QuickTimer = QuickTimerT<>;

} // namespace pygim
