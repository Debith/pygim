#pragma once
// utils/core_utils.h — small pieces the bindings share: a list as text, a dunder test, and the data and time units
// calculate_rate reads ("MiB", "ms"), as keyed tables (#131).
//
//     to_csv({"b", "a"}, true) -> "a, b"           is_dunder("__init__") -> true
//     to_bytes(2, "KiB") -> 2048        to_seconds(250, "ms") -> 0.25
//     calculate_rate(10 * 1024 * 1024, "bytes", 3, "s")  -> "3.33 MB/s"
//
// A unit's name is read in ASCII lower case (chars::lower: no locale). CORE layer: pybind-free.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../wiring/registry/core.h"
#include "chars.h"
#include "string_list.h"

namespace pygim {

/// The strings with ", " between each two, sorted first when `sorted`: what pygim.utils.to_csv returns.
[[nodiscard]] inline std::string to_csv(std::vector<std::string> strings, bool sorted = false) {
    if (sorted) std::ranges::sort(strings);
    return joined(strings, ", ");
}

/// Whether `attr` is a dunder name (`__x__`): four characters at least, two underscores at each end.
[[nodiscard]] constexpr bool is_dunder(std::string_view attr) noexcept {
    return attr.size() >= 4 && attr.starts_with("__") && attr.ends_with("__");
}
static_assert(is_dunder("__init__") && !is_dunder("__") && !is_dunder("_x_") && !is_dunder("__x"));

namespace detail {

// A unit as a ratio of the base unit, value * mul / div: a division kept a division (250 ms is
// 250 / 1000 s, not 250 * 0.001), so a result is what it has always been.
class unit_ratio {
public:
    constexpr unit_ratio(double mul = 0, double div = 1) noexcept : m_mul(mul), m_div(div) {}
    [[nodiscard]] constexpr bool known() const noexcept { return m_mul != 0; }
    [[nodiscard]] constexpr double of(double value) const noexcept { return value * m_mul / m_div; }

private:
    double m_mul;
    double m_div;
};
static_assert(unit_ratio(1, 1000).of(250) == 0.25 && !unit_ratio().known());

using unit_table = core::StaticRegistryCore<std::string_view, unit_ratio>;

[[nodiscard]] inline unit_table units(std::initializer_list<std::pair<std::initializer_list<std::string_view>, unit_ratio>> groups) {
    unit_table t;
    for (const auto& [names, ratio] : groups) {
        for (std::string_view name : names) t.register_or_override(name, ratio, false);
    }
    return t;
}

// Every spelling of a data unit, in bytes (a bit an eighth of one; K is 1024).
[[nodiscard]] inline const unit_table& data_units() {
    static const unit_table table = [] {
        constexpr double k = 1024.0;
        return units({{{"b", "byte", "bytes"}, unit_ratio(1)},
                      {{"bit", "bits"}, unit_ratio(1, 8)},
                      {{"kb", "kilobyte", "kilobytes", "kib", "kibibyte", "kibibytes"}, unit_ratio(k)},
                      {{"mb", "megabyte", "megabytes", "mib", "mebibyte", "mebibytes"}, unit_ratio(k * k)},
                      {{"gb", "gigabyte", "gigabytes", "gib", "gibibyte", "gibibytes"}, unit_ratio(k * k * k)},
                      {{"tb", "terabyte", "terabytes", "tib", "tebibyte", "tebibytes"}, unit_ratio(k * k * k * k)},
                      {{"pb", "petabyte", "petabytes", "pib", "pebibyte", "pebibytes"}, unit_ratio(k * k * k * k * k)},
                      {{"kbit", "kbits", "kilobit", "kilobits"}, unit_ratio(k, 8)},
                      {{"mbit", "mbits", "megabit", "megabits"}, unit_ratio(k * k, 8)},
                      {{"gbit", "gbits", "gigabit", "gigabits"}, unit_ratio(k * k * k, 8)},
                      {{"tbit", "tbits", "terabit", "terabits"}, unit_ratio(k * k * k * k, 8)},
                      {{"pbit", "pbits", "petabit", "petabits"}, unit_ratio(k * k * k * k * k, 8)}});
    }();
    return table;
}

// Every spelling of a time unit, in seconds.
[[nodiscard]] inline const unit_table& time_units() {
    static const unit_table table = units({{{"s", "sec", "secs", "second", "seconds"}, unit_ratio(1)},
                                           {{"ms", "millisecond", "milliseconds"}, unit_ratio(1, 1'000)},
                                           {{"us", "microsecond", "microseconds", "µs"}, unit_ratio(1, 1'000'000)},
                                           {{"ns", "nanosecond", "nanoseconds"}, unit_ratio(1, 1'000'000'000)},
                                           {{"m", "min", "mins", "minute", "minutes"}, unit_ratio(60)},
                                           {{"h", "hr", "hrs", "hour", "hours"}, unit_ratio(3'600)}});
    return table;
}

// `value` of the unit `name` names in `table`; std::invalid_argument naming `what` for none.
[[nodiscard]] inline double in_base_unit(const unit_table& table, double value, std::string_view name, std::string_view what) {
    const unit_ratio ratio = table.value_or(chars::lower(name), unit_ratio());
    if (!ratio.known()) throw std::invalid_argument("Unsupported " + std::string(what) + " unit: " + std::string(name));
    return ratio.of(value);
}

}  // namespace detail

/// `quantity` of a data unit ("KiB", "megabits"), in bytes.
[[nodiscard]] inline double to_bytes(double quantity, std::string_view unit) { return detail::in_base_unit(detail::data_units(), quantity, unit, "data"); }

/// `duration` of a time unit ("ms", "hours"), in seconds.
[[nodiscard]] inline double to_seconds(double duration, std::string_view unit) { return detail::in_base_unit(detail::time_units(), duration, unit, "duration"); }

/// `value` scaled down by `step` while it is that large and a bigger unit is left, with its unit.
template <std::size_t N>
[[nodiscard]] inline std::string format_scaled_value(double value, const std::array<const char*, N>& units, double step = 1024.0, int precision = 2) {
    std::size_t index = 0;
    for (double magnitude = std::fabs(value); magnitude >= step && index + 1 < N; magnitude /= step, ++index) value /= step;
    return std::format("{:.{}f} {}", value, precision, units[index]);
}

/// Bytes a second, in the biggest unit under 1024 of it: "3.33 MB/s".
[[nodiscard]] inline std::string format_bytes_per_second(double bytes_per_second, int precision = 2) {
    static constexpr std::array<const char*, 6> units{"B/s", "KB/s", "MB/s", "GB/s", "TB/s", "PB/s"};
    return format_scaled_value(bytes_per_second, units, 1024.0, precision);
}

/// `quantity` of a data unit over `duration` of a time unit, as format_bytes_per_second writes it.
[[nodiscard]] inline std::string calculate_rate(double quantity, std::string_view quantity_unit, double duration, std::string_view duration_unit,
                                                int precision = 2) {
    if (duration <= 0.0) throw std::invalid_argument("duration must be positive");
    return format_bytes_per_second(to_bytes(quantity, quantity_unit) / to_seconds(duration, duration_unit), precision);
}

}  // namespace pygim
