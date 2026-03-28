#pragma once

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace pygim {

// ── QuickTimerT ──────────────────────────────────────────────────
// Zero-heap-allocation RAII timer with fixed-capacity sub-timers.
// All output via FILE* (no <iostream>, no virtual dispatch).
//
// Memory layout (MaxSubTimers=8, typical):
//   m_name            16   string_view
//   m_output           8   FILE*
//   m_started_at       8   time_point
//   m_active_sub       8   size_t sentinel
//   m_sub_timer_count  8   size_t
//   m_auto_print       1   bool
//   m_print_on_start   1   bool
//   (padding)          6
//   m_sub_timers     256   array<SubTimer,8>  (32 bytes each)
//   ─────────────────────
//   Total           ~312 bytes on stack, zero heap.

template <std::size_t MaxSubTimers = 8>
class QuickTimerT {
public:
    using TimerId = std::size_t;
    static constexpr TimerId kInvalidTimerId = static_cast<TimerId>(-1);
    static constexpr std::size_t kMaxSubTimers = MaxSubTimers;

private:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t kNoActive = static_cast<std::size_t>(-1);

    // 32 bytes per sub-timer (no bool — sentinel time_point for "not running").
    struct SubTimer {
        std::string_view name;            // 16
        Clock::time_point started_at{};   //  8  — default-init = not running
        double seconds{0.0};              //  8
        // "running" iff started_at != time_point{}
    };

    static constexpr Clock::time_point kNotRunning{};

public:
    explicit QuickTimerT(std::string_view name = "QuickTimer",
                         std::FILE* output = stderr,
                         bool auto_print = true,
                         bool print_on_start = true)
        : m_name(name),
          m_output(output),
          m_started_at(Clock::now()),
          m_active_sub(kNoActive),
          m_sub_timer_count(0),
          m_auto_print(auto_print),
          m_print_on_start(print_on_start) {
        if (m_print_on_start) {
            print_started(m_name);
        }
    }

    ~QuickTimerT() noexcept {
        if (!m_auto_print || m_output == nullptr) return;
        try { print_summary(); } catch (...) {}
    }

    // Non-copyable, movable.
    QuickTimerT(const QuickTimerT&) = delete;
    QuickTimerT& operator=(const QuickTimerT&) = delete;
    QuickTimerT(QuickTimerT&&) = default;
    QuickTimerT& operator=(QuickTimerT&&) = default;

    // ── Sub-timer API ────────────────────────────────────────────

    // Name must point to storage that outlives this timer (string literals are ideal).
    void start_sub_timer(std::string_view name, bool print_on_start = true) {
        start_sub_timer(get_or_create_sub_timer_id(name), print_on_start);
    }

    [[nodiscard]] TimerId get_or_create_sub_timer_id(std::string_view name) {
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

    void start_sub_timer(TimerId id, bool print_on_start = true) {
        if (id >= m_sub_timer_count) [[unlikely]] {
            throw std::out_of_range("sub timer id out of range");
        }
        if (m_active_sub != kNoActive) {
            stop_sub_timer_impl(m_active_sub, false);
        }
        SubTimer& sub = m_sub_timers[id];
        sub.started_at = Clock::now();
        m_active_sub = id;
        if (print_on_start) {
            print_started(sub.name);
        }
    }

    double stop_sub_timer(std::string_view name = {}, bool print_now = true) {
        return stop_sub_timer_impl(resolve_sub_timer_index(name), print_now);
    }

    double stop_sub_timer(TimerId id, bool print_now = true) {
        if (id >= m_sub_timer_count) [[unlikely]] {
            throw std::out_of_range("sub timer id out of range");
        }
        return stop_sub_timer_impl(id, print_now);
    }

    // ── Queries ──────────────────────────────────────────────────

    double total_seconds() const {
        return std::chrono::duration<double>(Clock::now() - m_started_at).count();
    }

    double sub_timer_seconds(std::string_view name) const {
        for (std::size_t i = 0; i < m_sub_timer_count; ++i) {
            if (m_sub_timers[i].name == name) {
                return snapshot_sub_seconds(m_sub_timers[i]);
            }
        }
        throw std::out_of_range("sub timer not found");
    }

    std::string_view name() const { return m_name; }
    std::size_t sub_timer_count() const { return m_sub_timer_count; }

    // ── Output ───────────────────────────────────────────────────

    void print_summary() {
        if (m_active_sub != kNoActive) {
            stop_sub_timer_impl(m_active_sub, false);
        }
        if (m_output == nullptr) return;

        const double total = total_seconds();
        std::fprintf(m_output, "%.*s total: %ss\n",
                     static_cast<int>(m_name.size()), m_name.data(),
                     fmt_seconds(total));
        for (std::size_t i = 0; i < m_sub_timer_count; ++i) {
            const double secs = snapshot_sub_seconds(m_sub_timers[i]);
            std::fprintf(m_output, "  - %.*s: %ss\n",
                         static_cast<int>(m_sub_timers[i].name.size()),
                         m_sub_timers[i].name.data(),
                         fmt_seconds(secs));
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
            if (print_now) print_single(sub.name, sub.seconds);
            return sub.seconds;
        }
        const double elapsed =
            std::chrono::duration<double>(Clock::now() - sub.started_at).count();
        sub.seconds += elapsed;
        sub.started_at = kNotRunning;     // mark stopped (no bool needed)
        if (m_active_sub == index) {
            m_active_sub = kNoActive;
        }
        if (print_now) print_single(sub.name, sub.seconds);
        return sub.seconds;
    }

    std::size_t resolve_sub_timer_index(std::string_view name) const {
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

    // ── Data members (ordered for packing) ───────────────────────
    std::string_view m_name;                                // 16
    std::FILE* m_output;                                    //  8
    Clock::time_point m_started_at;                         //  8
    std::size_t m_active_sub;                               //  8  (sentinel, not optional)
    std::size_t m_sub_timer_count;                          //  8
    bool m_auto_print;                                      //  1
    bool m_print_on_start;                                  //  1
    // 6 bytes padding
    std::array<SubTimer, MaxSubTimers> m_sub_timers{};      // 32 * MaxSubTimers
};

// Default alias — 8 sub-timers, ~312 bytes, zero heap allocations.
using QuickTimer = QuickTimerT<8>;

} // namespace pygim
