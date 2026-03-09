#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pygim {

struct QuickTimerEntry {
    std::string name;
    double seconds{0.0};
};

struct QuickTimerReport {
    std::string name;
    double total_seconds{0.0};
    std::vector<QuickTimerEntry> sub_timers;

    [[nodiscard]] double sub_timer_seconds(std::string_view timer_name) const noexcept {
        for (const auto& entry : sub_timers) {
            if (entry.name == timer_name) return entry.seconds;
        }
        return 0.0;
    }
};

class QuickTimer {
public:
    using TimerId = std::size_t;
    static constexpr TimerId kInvalidTimerId = static_cast<TimerId>(-1);

    explicit QuickTimer(std::string name = "QuickTimer",
                        std::ostream &output = std::clog,
                        bool auto_print = true,
                        bool print_on_start = true)
        : m_name(std::move(name)),
          m_output(&output),
          m_auto_print(auto_print),
          m_print_on_start(print_on_start),
          m_started_at(Clock::now()) {
        if (m_print_on_start) {
            print_started(m_name);
        }
    }

    ~QuickTimer() noexcept {
        if (!m_auto_print || m_output == nullptr) {
            return;
        }
        try {
            print_summary();
        } catch (...) {
        }
    }

    void start_sub_timer(const std::string &name, bool print_on_start = true) {
        if (name.empty()) {
            throw std::invalid_argument("sub timer name must not be empty");
        }

        start_sub_timer(get_or_create_sub_timer_id(name), print_on_start);
    }

    [[nodiscard]] TimerId get_or_create_sub_timer_id(const std::string& name) {
        if (name.empty()) {
            throw std::invalid_argument("sub timer name must not be empty");
        }

        auto it = m_sub_timer_indices.find(name);
        if (it == m_sub_timer_indices.end()) {
            const TimerId index = m_sub_timers.size();
            m_sub_timers.push_back(SubTimer{name});
            m_sub_timer_indices.emplace(name, index);
            return index;
        }
        return it->second;
    }

    void start_sub_timer(TimerId id, bool print_on_start = true) {
        if (id >= m_sub_timers.size()) {
            throw std::out_of_range("sub timer id out of range");
        }

        if (has_active_sub_timer()) {
            stop_sub_timer(std::string_view{}, false);
        }

        SubTimer &sub_timer = m_sub_timers[id];
        sub_timer.running = true;
        sub_timer.started_at = Clock::now();
        m_active_sub_timer_index = id;
        if (print_on_start) {
            print_started(sub_timer.name);
        }
    }

    double stop_sub_timer(std::string_view name = {}, bool print_now = true) {
        std::size_t index = resolve_sub_timer_index(name);
        SubTimer &sub_timer = m_sub_timers[index];

        if (!sub_timer.running) {
            if (print_now) {
                print_single(sub_timer.name, sub_timer.seconds);
            }
            return sub_timer.seconds;
        }

        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - sub_timer.started_at).count();
        sub_timer.seconds += elapsed;
        sub_timer.running = false;

        if (m_active_sub_timer_index.has_value() && m_active_sub_timer_index.value() == index) {
            m_active_sub_timer_index.reset();
        }

        if (print_now) {
            print_single(sub_timer.name, sub_timer.seconds);
        }
        return sub_timer.seconds;
    }

    double stop_sub_timer(TimerId id, bool print_now = true) {
        if (id >= m_sub_timers.size()) {
            throw std::out_of_range("sub timer id out of range");
        }

        SubTimer &sub_timer = m_sub_timers[id];

        if (!sub_timer.running) {
            if (print_now) {
                print_single(sub_timer.name, sub_timer.seconds);
            }
            return sub_timer.seconds;
        }

        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - sub_timer.started_at).count();
        sub_timer.seconds += elapsed;
        sub_timer.running = false;

        if (m_active_sub_timer_index.has_value() && m_active_sub_timer_index.value() == id) {
            m_active_sub_timer_index.reset();
        }

        if (print_now) {
            print_single(sub_timer.name, sub_timer.seconds);
        }
        return sub_timer.seconds;
    }

    double total_seconds() const {
        const auto now = Clock::now();
        return std::chrono::duration<double>(now - m_started_at).count();
    }

    [[nodiscard]] QuickTimerReport report() const {
        QuickTimerReport out;
        out.name = m_name;
        out.total_seconds = total_seconds();
        out.sub_timers.reserve(m_sub_timers.size());

        const auto now = Clock::now();
        for (const auto& sub_timer : m_sub_timers) {
            double seconds = sub_timer.seconds;
            if (sub_timer.running) {
                seconds += std::chrono::duration<double>(now - sub_timer.started_at).count();
            }
            out.sub_timers.push_back(QuickTimerEntry{sub_timer.name, seconds});
        }
        return out;
    }

    double sub_timer_seconds(std::string_view name) const {
        auto it = m_sub_timer_indices.find(std::string(name));
        if (it == m_sub_timer_indices.end()) {
            throw std::out_of_range("sub timer not found: " + std::string(name));
        }
        const auto snapshot = report();
        return snapshot.sub_timer_seconds(name);
    }

    void print_summary() {
        if (has_active_sub_timer()) {
            stop_sub_timer(std::string_view{}, false);
        }

        if (m_output == nullptr) {
            return;
        }

        const auto snapshot = report();
        (*m_output) << snapshot.name << " total: " << format_seconds(snapshot.total_seconds) << "s" << std::endl;
        for (const auto &entry : snapshot.sub_timers) {
            (*m_output) << "  - " << entry.name << ": " << format_seconds(entry.seconds) << "s" << std::endl;
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    struct SubTimer {
        explicit SubTimer(std::string timer_name)
            : name(std::move(timer_name)) {}

        std::string name;
        double seconds{0.0};
        bool running{false};
        Clock::time_point started_at{};
    };

    std::size_t resolve_sub_timer_index(std::string_view name) const {
        if (!name.empty()) {
            auto it = m_sub_timer_indices.find(std::string(name));
            if (it == m_sub_timer_indices.end()) {
                throw std::out_of_range("sub timer not found: " + std::string(name));
            }
            return it->second;
        }

        if (!m_active_sub_timer_index.has_value()) {
            throw std::runtime_error("no active sub timer");
        }

        return m_active_sub_timer_index.value();
    }

    bool has_active_sub_timer() const {
        return m_active_sub_timer_index.has_value();
    }

    void print_single(const std::string &name, double seconds) {
        if (m_output == nullptr) {
            return;
        }
        (*m_output) << m_name << " [" << name << "]: " << format_seconds(seconds) << "s" << std::endl;
    }

    void print_started(const std::string &name) {
        if (m_output == nullptr) {
            return;
        }
        (*m_output) << m_name << " [" << name << "]: started" << std::endl;
    }

    static std::string format_seconds(double seconds) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(6) << seconds;
        return stream.str();
    }

    std::string m_name;
    std::ostream *m_output;
    bool m_auto_print;
    bool m_print_on_start;
    Clock::time_point m_started_at;
    std::vector<SubTimer> m_sub_timers;
    std::unordered_map<std::string, std::size_t> m_sub_timer_indices;
    std::optional<std::size_t> m_active_sub_timer_index;
};

} // namespace pygim
