// utils/core_utils.h — base utilities: joining text, data and time units, rates. A base header:
// it depends on the standard library only, so any header may include it. Everything that can be is
// constexpr and proven in tests/static/core_utils_proofs.cpp; text formatting stays at run time,
// because std::format is not constexpr, and the numbers it formats are computed by constexpr
// functions (scale_down, bytes_per_second). Python's naming rules (is_dunder and its family) live
// in python_names.h.
//
// Worked example:
//
//   to_csv({"b", "a"}, true)                        -> "a, b"
//   to_lower_copy("MiB")                             -> "mib"
//   to_bytes(1.0, "KiB")                             -> 1024.0
//   to_seconds(1500.0, "ms")                         -> 1.5
//   scale_down(2048.0, 6, 1024.0)                    -> {2.0, 1}            two of the second unit
//   bytes_per_second(1.0, "KiB", 500.0, "ms")        -> 2048.0
//   calculate_rate(1.0, "KiB", 500.0, "ms")          -> "2.00 KB/s"
//
// Lifetime and threading: free functions over values; nothing is stored, nothing is shared.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

#include "aliases.h"

/// `strings` joined with ", ", sorted first when `sorted`. Cost: one sort when asked, one allocation.
/// Example: to_csv({"b", "a"}) is "b, a"; to_csv({"b", "a"}, true) is "a, b"; to_csv({}) is "".
[[nodiscard]] constexpr std::string to_csv(pygim::Strings strings, bool sorted = false) {
    if (sorted) {
        std::ranges::sort(strings);
    }
    if (strings.empty()) return {};
    std::size_t total = 0;
    for (const auto& s : strings) total += s.size() + 2;
    std::string result;
    result.reserve(total);
    bool first = true;
    for (const auto& s : strings) {
        if (!first) result += ", ";
        first = false;
        result += s;
    }
    return result;
}

/// `value` with its ASCII letters lowered; every other byte, UTF-8 included, kept. This is what
/// std::tolower does in the "C" locale, as a constexpr function. Example: to_lower_copy("MiB") is "mib".
[[nodiscard]] constexpr std::string to_lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; });
    return value;
}

/// `step` raised to the whole power `exponent`, by repeated multiplication: exact for the powers of
/// two the units use, where std::pow is not constexpr. Example: whole_power(1024.0, 2) is 1048576.0.
[[nodiscard]] constexpr double whole_power(double step, int exponent) {
    double result = 1.0;
    for (int i = 0; i < exponent; ++i) result *= step;
    return result;
}

/// `quantity` of `unit_raw` in bytes; a unit is matched in any case. std::invalid_argument for a
/// unit it does not know. Example: to_bytes(1.0, "KiB") is 1024.0; to_bytes(8.0, "bits") is 1.0.
[[nodiscard]] constexpr double to_bytes(double quantity, const std::string& unit_raw) {
    constexpr double kStep = 1024.0;
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
        return quantity * whole_power(kStep, 2);
    }
    if (unit == "gb" || unit == "gigabyte" || unit == "gigabytes" || unit == "gib" || unit == "gibibyte" || unit == "gibibytes") {
        return quantity * whole_power(kStep, 3);
    }
    if (unit == "tb" || unit == "terabyte" || unit == "terabytes" || unit == "tib" || unit == "tebibyte" || unit == "tebibytes") {
        return quantity * whole_power(kStep, 4);
    }
    if (unit == "pb" || unit == "petabyte" || unit == "petabytes" || unit == "pib" || unit == "pebibyte" || unit == "pebibytes") {
        return quantity * whole_power(kStep, 5);
    }
    if (unit == "kbit" || unit == "kbits" || unit == "kilobit" || unit == "kilobits") {
        return quantity * kStep / 8.0;
    }
    if (unit == "mbit" || unit == "mbits" || unit == "megabit" || unit == "megabits") {
        return quantity * whole_power(kStep, 2) / 8.0;
    }
    if (unit == "gbit" || unit == "gbits" || unit == "gigabit" || unit == "gigabits") {
        return quantity * whole_power(kStep, 3) / 8.0;
    }
    if (unit == "tbit" || unit == "tbits" || unit == "terabit" || unit == "terabits") {
        return quantity * whole_power(kStep, 4) / 8.0;
    }
    if (unit == "pbit" || unit == "pbits" || unit == "petabit" || unit == "petabits") {
        return quantity * whole_power(kStep, 5) / 8.0;
    }
    throw std::invalid_argument("Unsupported data unit: " + unit_raw);
}

/// `duration` of `unit_raw` in seconds; a unit is matched in any case. std::invalid_argument for a
/// unit it does not know. Example: to_seconds(1500.0, "ms") is 1.5; to_seconds(2.0, "h") is 7200.0.
[[nodiscard]] constexpr double to_seconds(double duration, const std::string& unit_raw) {
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

/// A value expressed in one of a list of units: `value` of the unit at index `unit`.
struct scaled_value {
    double value;
    std::size_t unit;
    constexpr bool operator==(const scaled_value&) const = default;
};

/// `value` divided by `step` while it is at least one step and a larger unit is left among `units`.
/// Cost: one division per unit climbed. Example: scale_down(2048.0, 6, 1024.0) is {2.0, 1}.
[[nodiscard]] constexpr scaled_value scale_down(double value, std::size_t units, double step) {
    double magnitude = value < 0.0 ? -value : value;
    std::size_t index = 0;
    while (magnitude >= step && index + 1 < units) {
        value /= step;
        magnitude /= step;
        ++index;
    }
    return {value, index};
}

/// `value` written in the largest of `units` it reaches, to `precision` decimals. Runs at run time:
/// std::format is not constexpr. Example: format_scaled_value(2048.0, {"B", "KB"}) is "2.00 KB".
template <std::size_t N>
[[nodiscard]] inline std::string format_scaled_value(double value,
                                                     const std::array<const char*, N>& units,
                                                     double step = 1024.0,
                                                     int precision = 2) {
    const auto scaled = scale_down(value, N, step);
    return std::format("{:.{}f} {}", scaled.value, precision, units[scaled.unit]);
}

/// A rate in bytes per second, written with its unit. Example: format_bytes_per_second(2048.0) is "2.00 KB/s".
[[nodiscard]] inline std::string format_bytes_per_second(double bytes_per_second, int precision = 2) {
    static constexpr std::array<const char*, 6> kUnits{"B/s", "KB/s", "MB/s", "GB/s", "TB/s", "PB/s"};
    return format_scaled_value(bytes_per_second, kUnits, 1024.0, precision);
}

/// `quantity` of `quantity_unit` per `duration` of `duration_unit`, in bytes per second.
/// std::invalid_argument for a duration that is not positive, or a unit it does not know.
/// Example: bytes_per_second(1.0, "KiB", 500.0, "ms") is 2048.0.
[[nodiscard]] constexpr double bytes_per_second(double quantity,
                                                const std::string& quantity_unit,
                                                double duration,
                                                const std::string& duration_unit) {
    if (duration <= 0.0) {
        throw std::invalid_argument("duration must be positive");
    }
    return to_bytes(quantity, quantity_unit) / to_seconds(duration, duration_unit);
}

/// The rate bytes_per_second() computes, written with its unit. Runs at run time: std::format is
/// not constexpr. Example: calculate_rate(1.0, "KiB", 500.0, "ms") is "2.00 KB/s".
[[nodiscard]] inline std::string calculate_rate(double quantity,
                                                const std::string& quantity_unit,
                                                double duration,
                                                const std::string& duration_unit,
                                                int precision = 2) {
    return format_bytes_per_second(bytes_per_second(quantity, quantity_unit, duration, duration_unit), precision);
}
