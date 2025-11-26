#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <string>
#include <unordered_map>
#include <sstream>
#include <ranges>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <cstddef>
#include <array>
#include <stdexcept>
#include <cctype>

namespace py = pybind11;


inline std::string to_csv(std::vector<std::string> strings, bool sorted = false) {
    if (sorted) {
        std::ranges::sort(strings);
    }
    std::ostringstream oss;
    bool first = true;
    for (auto&& s : strings) {
        if (!first) oss << ", ";
        first = false;
        oss << s;
    }
    return oss.str();
}

// Primary overload: works on std::string
inline bool is_dunder(const std::string &attr) {
    // Must be at least 4 characters: two leading and two trailing underscores
    if (attr.size() < 4) {
        return false;
    }
    // Check first two and last two characters
    return attr[0] == '_' &&
           attr[1] == '_' &&
           attr[attr.size() - 2] == '_' &&
           attr[attr.size() - 1] == '_';
}

// Overload for py::str
inline bool is_dunder(const py::str &attr) {
    // Convert to std::string and reuse string overload
    std::string s = static_cast<std::string>(attr);
    return is_dunder(s);
}

// (Optional) Overload for const char*
inline bool is_dunder(const char *attr) {
    if (!attr) {
        return false;
    }
    std::string s(attr);
    return is_dunder(s);
}


inline bool is_generator(const py::object &instance) {
    // Import types.GeneratorType and check isinstance
    static py::object generator_type = py::module::import("types").attr("GeneratorType");
    return py::isinstance(instance, generator_type);
}


inline std::string to_lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline double to_bytes(double quantity, const std::string &unit_raw) {
    static constexpr double kStep = 1024.0;
    const std::string unit = to_lower_copy(unit_raw);

    if (unit == "b" || unit == "byte" || unit == "bytes") {
        return quantity;
    }
    if (unit == "bit" || unit == "bits") {
        return quantity / 8.0;
    }
    if (unit == "kb" || unit == "kilobyte" || unit == "kilobytes" || unit == "kib" || unit == "kibibyte" || unit == "kibibytes") {
        return quantity * kStep;
    }
    if (unit == "mb" || unit == "megabyte" || unit == "megabytes" || unit == "mib" || unit == "mebibyte" || unit == "mebibytes") {
        return quantity * std::pow(kStep, 2);
    }
    if (unit == "gb" || unit == "gigabyte" || unit == "gigabytes" || unit == "gib" || unit == "gibibyte" || unit == "gibibytes") {
        return quantity * std::pow(kStep, 3);
    }
    if (unit == "tb" || unit == "terabyte" || unit == "terabytes" || unit == "tib" || unit == "tebibyte" || unit == "tebibytes") {
        return quantity * std::pow(kStep, 4);
    }
    if (unit == "pb" || unit == "petabyte" || unit == "petabytes" || unit == "pib" || unit == "pebibyte" || unit == "pebibytes") {
        return quantity * std::pow(kStep, 5);
    }
    if (unit == "kbit" || unit == "kbits" || unit == "kilobit" || unit == "kilobits") {
        return quantity * kStep / 8.0;
    }
    if (unit == "mbit" || unit == "mbits" || unit == "megabit" || unit == "megabits") {
        return quantity * std::pow(kStep, 2) / 8.0;
    }
    if (unit == "gbit" || unit == "gbits" || unit == "gigabit" || unit == "gigabits") {
        return quantity * std::pow(kStep, 3) / 8.0;
    }
    if (unit == "tbit" || unit == "tbits" || unit == "terabit" || unit == "terabits") {
        return quantity * std::pow(kStep, 4) / 8.0;
    }
    if (unit == "pbit" || unit == "pbits" || unit == "petabit" || unit == "petabits") {
        return quantity * std::pow(kStep, 5) / 8.0;
    }
    throw std::invalid_argument("Unsupported data unit: " + unit_raw);
}

inline double to_seconds(double duration, const std::string &unit_raw) {
    const std::string unit = to_lower_copy(unit_raw);
    if (unit == "s" || unit == "sec" || unit == "secs" || unit == "second" || unit == "seconds") {
        return duration;
    }
    if (unit == "ms" || unit == "millisecond" || unit == "milliseconds") {
        return duration / 1'000.0;
    }
    if (unit == "us" || unit == "microsecond" || unit == "microseconds" || unit == "µs") {
        return duration / 1'000'000.0;
    }
    if (unit == "ns" || unit == "nanosecond" || unit == "nanoseconds") {
        return duration / 1'000'000'000.0;
    }
    if (unit == "m" || unit == "min" || unit == "mins" || unit == "minute" || unit == "minutes") {
        return duration * 60.0;
    }
    if (unit == "h" || unit == "hr" || unit == "hrs" || unit == "hour" || unit == "hours") {
        return duration * 3'600.0;
    }
    throw std::invalid_argument("Unsupported duration unit: " + unit_raw);
}

template <std::size_t N>
inline std::string format_scaled_value(double value,
                                       const std::array<const char *, N> &units,
                                       double step = 1024.0,
                                       int precision = 2) {
    double abs_value = std::fabs(value);
    std::size_t index = 0;
    while (abs_value >= step && index + 1 < N) {
        value /= step;
        abs_value /= step;
        ++index;
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << std::setprecision(precision) << value << ' ' << units[index];
    return oss.str();
}

inline std::string format_bytes_per_second(double bytes_per_second, int precision = 2) {
    static constexpr std::array<const char *, 6> kUnits{"B/s", "KB/s", "MB/s", "GB/s", "TB/s", "PB/s"};
    return format_scaled_value(bytes_per_second, kUnits, 1024.0, precision);
}

inline std::string calculate_rate(double quantity,
                                  const std::string &quantity_unit,
                                  double duration,
                                  const std::string &duration_unit,
                                  int precision = 2) {
    if (duration <= 0.0) {
        throw std::invalid_argument("duration must be positive");
    }
    double bytes = to_bytes(quantity, quantity_unit);
    double seconds = to_seconds(duration, duration_unit);
    double rate = bytes / seconds;
    return format_bytes_per_second(rate, precision);
}