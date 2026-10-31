#pragma once
// utils/quick_timer.h — one timer measures a block and the laps inside it. Its CLOCK and its
// REPORT are given to it, never built in.
//
//     QuickTimer timer("load csv");                     // the monotonic clock, the stderr report
//     timer.start_lap("read");   ...              // a lap: starting one stops the one running
//     timer.start_lap("parse");  ...
//     // leaving the scope (or finish()) stops the running lap and tells the report "finished"
//
//     SilentTimer<BcpPhase> timer("bcp_insert");       // measures, tells nobody: metrics from snapshot()
//
//     manual_time time;                                 // a test: exact numbers, your callbacks
//     timer_hooks hooks;
//     hooks.add<lap_stopped>([](std::string_view timer, std::string_view lap, duration took) { ... });
//     QuickTimerT<void, 8, manual_clock, timer_hooks> timer("job", time.clock(), hooks);
//
// The clock is a clock_source (utils/timestamp.h). The report hears four moments, each as plain values:
//
//     timer_started(timer)  lap_started(timer, lap)  lap_stopped(timer, lap, took)  timer_finished(snapshot)
//
// Reports that ship: silent (no_hooks: compiles away, keeping the timer a literal type), timer_hooks
// (your callbacks, utils/hooks.h), stderr_report (text: started lines, stopped lines and the summary,
// each on or off) and both<A, B>. A lap's `took` is all the time that lap has run.
//
// Two modes, by PhaseEnum. String mode (void): laps are named at run time — string_views the caller
// keeps alive. Enum mode: laps are an enum's values, named by phase_name(PhaseEnum) found by ADL,
// capacity PhaseEnum::COUNT. The timer allocates nothing; a snapshot allocates its list of laps.
// Every time is one 64-bit count of nanoseconds (utils/duration.h); seconds are views of it.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "duration.h"
#include "hooks.h"
#include "timestamp.h"

namespace pygim {

namespace detail {
template <typename E> struct timer_capacity {
    static constexpr std::size_t value = static_cast<std::size_t>(E::COUNT);
};
template <> struct timer_capacity<void> {
    static constexpr std::size_t value = 8;
};
} // namespace detail

// ── what a timer measured ────────────────────────────────────────────────

/// One lap: its name and all the time it has run.
class lap_time {
public:
    constexpr lap_time(std::string_view name, duration took) noexcept : m_name(name), m_took(took) {}
    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] constexpr duration took() const noexcept { return m_took; }
    [[nodiscard]] constexpr double seconds() const noexcept { return seconds_of(m_took); }

private:
    std::string_view m_name;
    duration m_took;
};
static_assert(lap_time("read", duration(2'000)).took() == duration(2'000));

/// What a timer measured: its name, its total and its laps in the order they first started. The
/// names are views of the timer's, so a snapshot is read while they live (a Python snapshot copies them).
class timer_snapshot {
public:
    constexpr timer_snapshot(std::string_view name, duration total, std::vector<lap_time> laps)
        : m_name(name), m_total(total), m_laps(std::move(laps)) {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] constexpr duration total() const noexcept { return m_total; }
    [[nodiscard]] constexpr double total_seconds() const noexcept { return seconds_of(m_total); }
    [[nodiscard]] constexpr const std::vector<lap_time>& laps() const noexcept { return m_laps; }

    /// The lap named `name`; std::out_of_range when there is none.
    [[nodiscard]] constexpr duration lap(std::string_view name) const {
        for (const lap_time& l : m_laps) {
            if (l.name() == name) return l.took();
        }
        throw std::out_of_range("no lap of that name in the snapshot");
    }
    /// The lap of phase `phase` (enum mode: one lap per value of the enum).
    template <class Phase> requires std::is_enum_v<Phase>
    [[nodiscard]] constexpr duration lap(Phase phase) const { return m_laps.at(static_cast<std::size_t>(phase)).took(); }
    /// A lap in seconds: what metrics report.
    template <class Which>
    [[nodiscard]] constexpr double lap_seconds(Which which) const { return seconds_of(lap(which)); }

    /// Adds another snapshot's total and laps, lap by lap by name (the workers of one job, which
    /// may have started their laps in other orders); a lap only the other has comes after these.
    constexpr timer_snapshot& merge_with(const timer_snapshot& other) {
        m_total += other.m_total;
        for (const lap_time& theirs : other.m_laps) {
            auto mine = std::find_if(m_laps.begin(), m_laps.end(), [&](const lap_time& l) { return l.name() == theirs.name(); });
            if (mine == m_laps.end()) m_laps.push_back(theirs);
            else *mine = lap_time(mine->name(), mine->took() + theirs.took());
        }
        return *this;
    }
    friend constexpr timer_snapshot operator|(timer_snapshot lhs, const timer_snapshot& rhs) { return lhs.merge_with(rhs); }

private:
    std::string_view m_name;
    duration m_total;
    std::vector<lap_time> m_laps;
};
static_assert([] {
    const timer_snapshot s("job", duration(6), {lap_time("read", duration(2)), lap_time("parse", duration(4))});
    const timer_snapshot t("job", duration(7), {lap_time("parse", duration(1)), lap_time("write", duration(5))});   // another order
    const timer_snapshot u = s | t;
    return s.lap("parse") == duration(4) && (s | s).total() == duration(12) && u.lap("read") == duration(2) &&
           u.lap("parse") == duration(5) && u.lap("write") == duration(5) && u.laps().size() == 3;
}());

// ── the report ───────────────────────────────────────────────────────────

/// The timer started: (timer).
struct timer_started { using signature = void(std::string_view timer); };
/// A lap started: (timer, lap).
struct lap_started { using signature = void(std::string_view timer, std::string_view lap); };
/// A lap stopped — by stop, by the next lap starting, or by the finish: (timer, lap, all its time).
struct lap_stopped { using signature = void(std::string_view timer, std::string_view lap, duration took); };
/// The timer finished: (what it measured).
struct timer_finished { using signature = void(const timer_snapshot& snapshot); };

/// What a timer tells: anything that hears the four moments, and says whether it listens to one.
template <class R>
concept timer_report = requires(const R& report, std::string_view text, duration took, const timer_snapshot& snapshot) {
    report.template run<timer_started>(text);
    report.template run<lap_started>(text, text);
    report.template run<lap_stopped>(text, text, took);
    report.template run<timer_finished>(snapshot);
    { report.template has<timer_finished>() } -> std::convertible_to<bool>;
};

/// Tells nobody, and compiles away.
using silent = no_hooks<timer_started, lap_started, lap_stopped, timer_finished>;
/// Your callbacks at each moment.
using timer_hooks = hooks<timer_started, lap_started, lap_stopped, timer_finished>;

static_assert(timer_report<silent> && timer_report<timer_hooks> && std::is_empty_v<silent>);

/// Which lines a stderr_report prints.
enum class report_lines : unsigned { none = 0, started = 1, stopped = 2, summary = 4, all = 7 };
[[nodiscard]] constexpr report_lines operator|(report_lines a, report_lines b) noexcept {
    return static_cast<report_lines>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
[[nodiscard]] constexpr bool includes(report_lines lines, report_lines line) noexcept {
    return (static_cast<unsigned>(lines) & static_cast<unsigned>(line)) != 0;
}
static_assert(includes(report_lines::started | report_lines::summary, report_lines::summary) &&
              !includes(report_lines::summary, report_lines::stopped));

/// Where a text report writes: anything that takes text, and says whether it is open.
template <class S>
concept text_sink = requires(const S& sink, std::string_view text) {
    sink.write(text);
    { sink.open() } -> std::convertible_to<bool>;
};

/// A C stream, stderr unless given; none (nullptr) is closed.
class file_sink {
public:
    constexpr explicit file_sink(std::FILE* out = stderr) noexcept : m_out(out) {}
    [[nodiscard]] constexpr bool open() const noexcept { return m_out != nullptr; }
    void write(std::string_view text) const { std::fwrite(text.data(), 1, text.size(), m_out); }

private:
    std::FILE* m_out;
};
static_assert(text_sink<file_sink> && !file_sink(nullptr).open());

/// Text, as QuickTimer has always printed it: "job: started", "job [read]: started",
/// "job [read]: 0.002000s", and at the finish "job total: 0.002000s" with a "  - read: ..." line
/// per lap, each line written whole to `Sink` (Python's is its sys.stderr: quick_timer_adapter.h).
template <text_sink Sink = file_sink>
class text_report {
public:
    explicit text_report(report_lines lines = report_lines::all, Sink sink = Sink{}) noexcept
        : m_lines(lines), m_sink(std::move(sink)) {}

    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E, class... Args>
    void run(const Args&... args) const {
        if (has<E>()) print(E{}, args...);
    }
    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E>
    [[nodiscard]] bool has() const noexcept {
        return m_sink.open() && includes(m_lines, line_of(E{}));
    }

private:
    report_lines m_lines;
    [[no_unique_address]] Sink m_sink;

    static constexpr report_lines line_of(timer_started) noexcept { return report_lines::started; }
    static constexpr report_lines line_of(lap_started) noexcept { return report_lines::started; }
    static constexpr report_lines line_of(lap_stopped) noexcept { return report_lines::stopped; }
    static constexpr report_lines line_of(timer_finished) noexcept { return report_lines::summary; }

    void print(timer_started, std::string_view timer) const { line(timer, ": started"); }
    void print(lap_started, std::string_view timer, std::string_view lap) const { line(timer, " [", lap, "]: started"); }
    void print(lap_stopped, std::string_view timer, std::string_view lap, duration took) const {
        line(timer, " [", lap, "]: ", seconds_text(took), "s");
    }
    void print(timer_finished, const timer_snapshot& s) const {
        std::string text = joined(s.name(), " total: ", seconds_text(s.total()), "s\n");
        for (const lap_time& lap : s.laps()) text += joined("  - ", lap.name(), ": ", seconds_text(lap.took()), "s\n");
        m_sink.write(text);
    }

    template <class... Parts>
    void line(const Parts&... parts) const { m_sink.write(joined(parts..., "\n")); }
    template <class... Parts>
    [[nodiscard]] static std::string joined(const Parts&... parts) {
        std::string out;
        (out.append(std::string_view(parts)), ...);
        return out;
    }

    // Seconds with six decimals (std::to_chars: fast, no locale).
    [[nodiscard]] static std::string seconds_text(duration d) {
        char buf[32];
        const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, seconds_of(d), std::chars_format::fixed, 6);
        return std::string(buf, end);
    }
};
/// Text on a C stream: stderr unless given.
using stderr_report = text_report<file_sink>;
static_assert(timer_report<stderr_report>);

/// Two reports at once: each moment goes to the first, then the second.
template <timer_report First, timer_report Second>
class both {
public:
    constexpr both(First first, Second second) : m_first(std::move(first)), m_second(std::move(second)) {}

    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E, class... Args>
    constexpr void run(const Args&... args) const {
        m_first.template run<E>(args...);
        m_second.template run<E>(args...);
    }
    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E>
    [[nodiscard]] constexpr bool has() const noexcept {
        return m_first.template has<E>() || m_second.template has<E>();
    }

private:
    [[no_unique_address]] First m_first;
    [[no_unique_address]] Second m_second;
};
static_assert(timer_report<both<silent, stderr_report>>);

// ── the timer ────────────────────────────────────────────────────────────

template <typename PhaseEnum = void,
          std::size_t MaxLaps = detail::timer_capacity<PhaseEnum>::value,
          clock_source Clock = monotonic_clock,
          timer_report Report = stderr_report>
class QuickTimerT {
public:
    static constexpr bool by_phase = !std::is_void_v<PhaseEnum>;
    using lap_id = std::conditional_t<by_phase, PhaseEnum, std::size_t>;
    using Snapshot = timer_snapshot;
    static constexpr std::size_t max_laps = MaxLaps;

private:
    using moment = basic_timestamp<Clock>;
    static constexpr std::size_t no_lap = static_cast<std::size_t>(-1);

    // String mode: 32 bytes (stores name). Enum mode: 16 bytes (name external).
    struct named_slot {
        std::string_view name;                    // 16
        moment started_at = moment::never();      //  8
        duration elapsed{0};                      //  8
    };
    struct phase_slot {
        moment started_at = moment::never();      //  8
        duration elapsed{0};                      //  8
    };
    using lap_slot = std::conditional_t<by_phase, phase_slot, named_slot>;

    static constexpr std::size_t to_index(lap_id id) {
        if constexpr (by_phase) return static_cast<std::size_t>(id);
        else                return id;
    }

    constexpr std::string_view lap_name(std::size_t index) const {
        if constexpr (by_phase) return phase_name(static_cast<PhaseEnum>(index));
        else                return m_laps[index].name;
    }

public:
    /// Starts timing now, by `clock`, telling `report`.
    constexpr explicit QuickTimerT(std::string_view name = "QuickTimer", Clock clock = Clock{}, Report report = Report{})
        : m_name(name),
          m_clock(clock),
          m_report(std::move(report)),
          m_started_at(moment::now(m_clock)),
          m_lap_count(by_phase ? MaxLaps : 0) {
        m_report.template run<timer_started>(m_name);
    }

    /// Finishes a timer nobody finished; a report that throws then is ignored (a destructor cannot raise).
    constexpr ~QuickTimerT() noexcept {
        if (finished()) return;
        try { finish(); } catch (...) {}
    }

    QuickTimerT(const QuickTimerT&) = delete;
    QuickTimerT& operator=(const QuickTimerT&) = delete;
    QuickTimerT& operator=(QuickTimerT&&) = delete;
    /// The moved-from timer is left finished, so only one of the two reports.
    constexpr QuickTimerT(QuickTimerT&& other) noexcept(std::is_nothrow_move_constructible_v<Report>)
        : m_name(other.m_name),
          m_clock(other.m_clock),
          m_report(std::move(other.m_report)),
          m_started_at(other.m_started_at),
          m_finished_at(other.m_finished_at),
          m_running(other.m_running),
          m_lap_count(other.m_lap_count),
          m_laps(other.m_laps) {
        other.m_finished_at = other.m_started_at;
    }

    // ── laps (string mode) ───────────────────────────────────────

    /// Starts the lap named `name`, made at its first start — refused before it is made when the
    /// timer has finished, so a refused name is never kept (a caller may not keep it alive).
    constexpr void start_lap(std::string_view name) requires (!by_phase) {
        refuse_after_finish();
        start_lap(lap_id_of(name));
    }

    [[nodiscard]] constexpr lap_id lap_id_of(std::string_view name) requires (!by_phase) {
        for (std::size_t i = 0; i < m_lap_count; ++i) {
            if (m_laps[i].name == name) return i;
        }
        if (m_lap_count >= MaxLaps) [[unlikely]] {
            throw std::out_of_range("no room for another lap");
        }
        const lap_id id = m_lap_count++;
        m_laps[id].name = name;
        return id;
    }

    /// Stops the lap named `name` (the running one when empty) and returns all its time.
    constexpr duration stop_lap(std::string_view name = {}) requires (!by_phase) {
        return stop_lap_at(lap_index_of(name));
    }

    // ── laps (both modes) ────────────────────────────────────────

    /// Starts lap `id`, stopping the one running.
    constexpr void start_lap(lap_id id) {
        refuse_after_finish();
        const auto index = checked_index(id);
        if (m_running != no_lap) stop_lap_at(m_running);
        m_laps[index].started_at = moment::now(m_clock);
        m_running = index;
        m_report.template run<lap_started>(m_name, lap_name(index));
    }

    constexpr duration stop_lap(lap_id id) { return stop_lap_at(checked_index(id)); }

    /// Stops the running lap, freezes the total and tells the report: once (later calls do nothing).
    constexpr void finish() {
        if (finished()) return;
        if (m_running != no_lap) stop_lap_at(m_running);
        m_finished_at = moment::now(m_clock);
        if (m_report.template has<timer_finished>()) m_report.template run<timer_finished>(snapshot());
    }

    // ── what it measured ─────────────────────────────────────────

    [[nodiscard]] constexpr bool finished() const noexcept { return m_finished_at != moment::never(); }
    /// From the start to now, or to the finish once finished.
    [[nodiscard]] constexpr duration total() const { return end() - m_started_at; }
    [[nodiscard]] constexpr double total_seconds() const { return seconds_of(total()); }

    [[nodiscard]] constexpr duration lap(std::string_view name) const requires (!by_phase) {
        for (std::size_t i = 0; i < m_lap_count; ++i) {
            if (m_laps[i].name == name) return lap_elapsed(m_laps[i]);
        }
        throw std::out_of_range("no lap of that name");
    }
    [[nodiscard]] constexpr duration lap(lap_id phase) const requires (by_phase) {
        return lap_elapsed(m_laps[to_index(phase)]);
    }
    template <class Which>
    [[nodiscard]] constexpr double lap_seconds(Which which) const { return seconds_of(lap(which)); }

    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] constexpr std::size_t lap_count() const noexcept { return m_lap_count; }

    /// Everything measured so far: the name, the total and every lap, in the order they first started.
    [[nodiscard]] constexpr timer_snapshot snapshot() const {
        std::vector<lap_time> laps;
        laps.reserve(m_lap_count);
        for (std::size_t i = 0; i < m_lap_count; ++i) laps.emplace_back(lap_name(i), lap_elapsed(m_laps[i]));
        return timer_snapshot(m_name, total(), std::move(laps));
    }

private:
    constexpr void refuse_after_finish() const {
        if (finished()) [[unlikely]] throw std::logic_error("the timer has finished; a lap cannot start after it");
    }

    [[nodiscard]] constexpr moment end() const { return finished() ? m_finished_at : moment::now(m_clock); }

    [[nodiscard]] static constexpr bool is_running(const lap_slot& sub) { return sub.started_at != moment::never(); }

    [[nodiscard]] constexpr duration lap_elapsed(const lap_slot& sub) const {
        return is_running(sub) ? sub.elapsed + (moment::now(m_clock) - sub.started_at) : sub.elapsed;
    }

    [[nodiscard]] constexpr std::size_t checked_index(lap_id id) const {
        const auto index = to_index(id);
        if constexpr (!by_phase) {
            if (index >= m_lap_count) [[unlikely]] throw std::out_of_range("lap id out of range");
        }
        return index;
    }

    constexpr duration stop_lap_at(std::size_t index) {
        lap_slot& sub = m_laps[index];
        if (!is_running(sub)) return sub.elapsed;
        sub.elapsed += moment::now(m_clock) - sub.started_at;
        sub.started_at = moment::never();
        if (m_running == index) m_running = no_lap;
        m_report.template run<lap_stopped>(m_name, lap_name(index), sub.elapsed);
        return sub.elapsed;
    }

    [[nodiscard]] constexpr std::size_t lap_index_of(std::string_view name) const requires (!by_phase) {
        if (!name.empty()) {
            for (std::size_t i = 0; i < m_lap_count; ++i) {
                if (m_laps[i].name == name) return i;
            }
            throw std::out_of_range("no lap of that name");
        }
        if (m_running == no_lap) [[unlikely]] throw std::runtime_error("no lap is running");
        return m_running;
    }

    std::string_view m_name;
    [[no_unique_address]] Clock m_clock;     // empty for the OS clocks: no bytes added
    [[no_unique_address]] Report m_report;   // empty when silent
    moment m_started_at;
    moment m_finished_at = moment::never();
    std::size_t m_running = no_lap;
    std::size_t m_lap_count;
    std::array<lap_slot, MaxLaps> m_laps{};
};

static_assert([] {   // a lap and the total, by a clock that moves only when told
    manual_time time;
    QuickTimerT<void, 8, manual_clock, silent> timer("job", time.clock());
    timer.start_lap("read");
    time.advance(duration(5));
    timer.finish();
    return timer.total() == duration(5) && timer.lap("read") == duration(5) && timer.finished();
}());

// String mode, 8 laps, the monotonic clock, text on stderr.
using QuickTimer = QuickTimerT<>;

/// A timer that tells nobody: for metrics read from snapshot() or total(). Its clock is given
/// like any timer's: SilentTimer<void, manual_clock> timer("probe", time.clock()).
template <typename PhaseEnum = void, clock_source Clock = monotonic_clock>
using SilentTimer = QuickTimerT<PhaseEnum, detail::timer_capacity<PhaseEnum>::value, Clock, silent>;

} // namespace pygim
