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

class QuickTimer {
public:
    explicit QuickTimer(std::string name = "QuickTimer",
                        std::ostream &output = std::clog,
                        bool auto_print = true)
        : m_name(std::move(name)),
          m_output(&output),
          m_auto_print(auto_print),
          m_started_at(Clock::now()) {}

    ~QuickTimer() noexcept {
        if (!m_auto_print || m_output == nullptr) {
            return;
        }
        try {
            print_summary();
        } catch (...) {
        }
    }

    void start_sub_timer(const std::string &name) {
        if (name.empty()) {
            throw std::invalid_argument("sub timer name must not be empty");
        }

        if (has_active_sub_timer()) {
            stop_sub_timer(std::string_view{}, false);
        }

        auto it = m_sub_timer_indices.find(name);
        if (it == m_sub_timer_indices.end()) {
            const std::size_t index = m_sub_timers.size();
            m_sub_timers.push_back(SubTimer{name});
            m_sub_timer_indices.emplace(name, index);
            it = m_sub_timer_indices.find(name);
        }

        SubTimer &sub_timer = m_sub_timers[it->second];
        sub_timer.running = true;
        sub_timer.started_at = Clock::now();
        m_active_sub_timer_index = it->second;
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

    double total_seconds() const {
        const auto now = Clock::now();
        return std::chrono::duration<double>(now - m_started_at).count();
    }

    double sub_timer_seconds(std::string_view name) const {
        auto it = m_sub_timer_indices.find(std::string(name));
        if (it == m_sub_timer_indices.end()) {
            throw std::out_of_range("sub timer not found: " + std::string(name));
        }

        const SubTimer &sub_timer = m_sub_timers[it->second];
        double seconds = sub_timer.seconds;
        if (sub_timer.running) {
            const auto now = Clock::now();
            seconds += std::chrono::duration<double>(now - sub_timer.started_at).count();
        }
        return seconds;
    }

    void print_summary() {
        if (has_active_sub_timer()) {
            stop_sub_timer(std::string_view{}, false);
        }

        if (m_output == nullptr) {
            return;
        }

        (*m_output) << m_name << " total: " << format_seconds(total_seconds()) << "s" << std::endl;
        for (const auto &sub_timer : m_sub_timers) {
            (*m_output) << "  - " << sub_timer.name << ": " << format_seconds(sub_timer.seconds) << "s" << std::endl;
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

    static std::string format_seconds(double seconds) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(6) << seconds;
        return stream.str();
    }

    std::string m_name;
    std::ostream *m_output;
    bool m_auto_print;
    Clock::time_point m_started_at;
    std::vector<SubTimer> m_sub_timers;
    std::unordered_map<std::string, std::size_t> m_sub_timer_indices;
    std::optional<std::size_t> m_active_sub_timer_index;
};

} // namespace pygim
