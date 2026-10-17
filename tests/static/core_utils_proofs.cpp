// Compile-time proofs for utils/core_utils.h — the base utilities, constexpr wherever the standard
// library allows. Text formatting stays at run time, because std::format is not constexpr; the
// numbers it formats are computed by constexpr functions and proven here.
//
// Each function is proven by a case table (case_table.h, global #118): one row per call, the
// arguments first, the expected result after them, a case id last; a baseline row first, then
// groups that change one argument. The expected values are the units' definitions (1 KiB = 1024
// bytes, 8 bits = 1 byte, 1 ms = 1/1000 s), written out by hand.
//
// This TU is part of the pygim.utils extension's sources (ext.utils.toml): every build compiles it,
// so a violated row cannot produce a binary, and the diagnostic names the row's index. It
// contributes no runtime code.

#include "../../src/_pygim_fast/utils/core_utils.h"
#include "case_table.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using pygim::proofs::every_row_holds;
using pygim::proofs::first_failing_row;

constexpr double KiB = 1024.0;

// ── to_csv(strings, sorted) -> joined with ", " ──────────────────────────────────────────────────
// strings[i] are the arguments, of which the first `count` are passed.
struct csv_row {
    std::array<std::string_view, 3> strings;
    std::size_t count;
    bool sorted;
    std::string_view expected;
    std::string_view id;
};
constexpr csv_row to_csv_rows[] = {
    //  strings[0..2]          count  sorted   expected     id
    {{"b", "a", ""},           2,     false,   "b, a",      "baseline"},
    {{"b", "a", ""},           2,     true,    "a, b",      "sorted"},          // vary sorted
    {{"only", "", ""},         1,     false,   "only",      "one_string"},      // vary the strings passed
    {{"", "", ""},             0,     false,   "",          "no_strings"},
    {{"c", "a", "b"},          3,     false,   "c, a, b",   "three_in_order"},
    {{"c", "a", "b"},          3,     true,    "a, b, c",   "three_sorted"},    // interaction: three and sorted
};
static_assert(first_failing_row(to_csv_rows, [](const auto& r) {
                  std::vector<std::string> strings;
                  for (std::size_t i = 0; i < r.count; ++i) strings.emplace_back(r.strings[i]);
                  return to_csv(strings, r.sorted) == r.expected;
              }) == every_row_holds);

// ── to_lower_copy(value) -> ASCII letters lowered ────────────────────────────────────────────────
struct text_row {
    std::string_view value;
    std::string_view expected;
    std::string_view id;
};
constexpr text_row to_lower_copy_rows[] = {
    //  value       expected    id
    {"MiB",       "mib",      "baseline"},
    {"mib",       "mib",      "already_lower"},       // vary the letters
    {"A-Z_09",    "a-z_09",   "letters_among_others"},
    {"µs",        "µs",       "non_ascii_kept"},      // UTF-8 bytes are not letters
    {"",          "",         "empty"},
};
static_assert(first_failing_row(to_lower_copy_rows,
                                [](const auto& r) { return to_lower_copy(std::string{r.value}) == r.expected; })
              == every_row_holds);

// ── to_bytes(quantity, unit) -> bytes ────────────────────────────────────────────────────────────
struct quantity_row {
    double quantity;
    std::string_view unit;
    double expected;
    std::string_view id;
};
constexpr quantity_row to_bytes_rows[] = {
    //  quantity  unit          expected                          id
    {1.0,       "KiB",        KiB,                              "baseline"},
    {1.0,       "kilobytes",  KiB,                              "unit_spelt_out"},       // vary the unit's spelling
    {1.0,       "KB",         KiB,                              "unit_decimal_name"},
    {1.0,       "KIB",        KiB,                              "unit_upper_case"},
    {1.0,       "B",          1.0,                              "unit_byte"},            // vary the unit's size
    {1.0,       "MB",         KiB * KiB,                        "unit_mega"},
    {1.0,       "GiB",        KiB * KiB * KiB,                  "unit_giga"},
    {1.0,       "tb",         KiB * KiB * KiB * KiB,            "unit_tera"},
    {1.0,       "PiB",        KiB * KiB * KiB * KiB * KiB,      "unit_peta"},
    {1.0,       "bits",       1.0 / 8.0,                        "unit_bit"},             // vary bits against bytes
    {1.0,       "kbit",       KiB / 8.0,                        "unit_kilobit"},
    {1.0,       "pbit",       KiB * KiB * KiB * KiB * KiB / 8.0, "unit_petabit"},
    {3.0,       "KiB",        3.0 * KiB,                        "quantity_three"},       // vary quantity
    {0.0,       "KiB",        0.0,                              "quantity_zero"},
};
static_assert(first_failing_row(to_bytes_rows,
                                [](const auto& r) { return to_bytes(r.quantity, std::string{r.unit}) == r.expected; })
              == every_row_holds);

// ── to_seconds(duration, unit) -> seconds ────────────────────────────────────────────────────────
constexpr quantity_row to_seconds_rows[] = {
    //  duration  unit        expected                 id
    {1500.0,    "ms",       1.5,                     "baseline"},
    {1500.0,    "s",        1500.0,                  "unit_second"},         // vary the unit
    {1.0,       "us",       1.0 / 1'000'000.0,       "unit_microsecond"},
    {1.0,       "µs",       1.0 / 1'000'000.0,       "unit_microsecond_sign"},
    {1.0,       "ns",       1.0 / 1'000'000'000.0,   "unit_nanosecond"},
    {2.0,       "Minutes",  120.0,                   "unit_minute_capitalised"},
    {2.0,       "h",        7200.0,                  "unit_hour"},
    {0.0,       "ms",       0.0,                     "duration_zero"},       // vary the duration
};
static_assert(first_failing_row(to_seconds_rows,
                                [](const auto& r) { return to_seconds(r.quantity, std::string{r.unit}) == r.expected; })
              == every_row_holds);

// ── scale_down(value, units, step) -> {value, unit} ─────────────────────────────────────────────
struct scale_row {
    double value;
    std::size_t units;
    double step;
    scaled_value expected;
    std::string_view id;
};
constexpr scale_row scale_down_rows[] = {
    //  value                 units  step   expected.value  expected.unit   id
    {2048.0,                6,     KiB,   {2.0,           1},             "baseline"},
    {512.0,                 6,     KiB,   {512.0,         0},             "value_below_a_step"},   // vary value
    {-3.0 * KiB * KiB,      6,     KiB,   {-3.0,          2},             "value_negative"},
    {0.0,                   6,     KiB,   {0.0,           0},             "value_zero"},
    {KiB * KiB * KiB,       2,     KiB,   {KiB * KiB,     1},             "units_run_out"},        // vary units: one division left
    {2048.0,                1,     KiB,   {2048.0,        0},             "units_one"},
    {2000.0,                6,     1000.0, {2.0,          1},             "step_decimal"},         // vary step
};
static_assert(first_failing_row(scale_down_rows,
                                [](const auto& r) { return scale_down(r.value, r.units, r.step) == r.expected; })
              == every_row_holds);

// ── bytes_per_second(quantity, quantity_unit, duration, duration_unit) -> bytes per second ──────
struct rate_row {
    double quantity;
    std::string_view quantity_unit;
    double duration;
    std::string_view duration_unit;
    double expected;
    std::string_view id;
};
constexpr rate_row bytes_per_second_rows[] = {
    //  quantity  quantity_unit  duration  duration_unit  expected            id
    {1.0,       "MiB",         1.0,      "s",           KiB * KiB,          "baseline"},
    {8.0,       "mbit",        1.0,      "s",           KiB * KiB,          "quantity_in_bits"},    // vary the quantity
    {1.0,       "MiB",         2.0,      "s",           KiB * KiB / 2.0,    "duration_two"},        // vary the duration
    {1.0,       "MiB",         500.0,    "ms",          2.0 * KiB * KiB,    "duration_unit_ms"},    // vary its unit
    {1.0,       "KiB",         500.0,    "ms",          2.0 * KiB,          "both_units_changed"},  // interaction
};
static_assert(first_failing_row(bytes_per_second_rows, [](const auto& r) {
                  return bytes_per_second(r.quantity, std::string{r.quantity_unit}, r.duration,
                                          std::string{r.duration_unit}) == r.expected;
              }) == every_row_holds);

}  // namespace
