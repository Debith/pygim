// Compile-time proofs for utils/core_utils.h — the base utilities, constexpr wherever the standard
// library allows. Text formatting stays at run time, because std::format is not constexpr; the
// numbers it formats are computed by constexpr functions and proven here.
//
// This TU is part of the pygim.utils extension's sources (ext.utils.toml): every build compiles it,
// so a violated law cannot produce a binary. It contributes no runtime code.

#include "../../src/_pygim_fast/utils/core_utils.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

consteval bool same(const std::string& made, std::string_view expected) { return made == expected; }

// ── to_csv: joined with ", ", in the given order or sorted ─────────────────────────────────────
static_assert(same(to_csv({"b", "a"}), "b, a"));
static_assert(same(to_csv({"b", "a"}, true), "a, b"));
static_assert(same(to_csv({"only"}), "only"));
static_assert(same(to_csv({}), ""));

// ── to_lower_copy: ASCII letters lowered, every other byte kept ─────────────────────────────────
static_assert(same(to_lower_copy("MiB"), "mib"));
static_assert(same(to_lower_copy("µs"), "µs"));
static_assert(same(to_lower_copy("A-Z_09"), "a-z_09"));

// ── to_bytes: every unit of a family, any case ─────────────────────────────────────────────────
static_assert(to_bytes(3.0, "B") == 3.0);
static_assert(to_bytes(8.0, "bits") == 1.0);
static_assert(to_bytes(1.0, "KiB") == 1024.0);
static_assert(to_bytes(1.0, "kilobytes") == 1024.0);
static_assert(to_bytes(1.0, "MB") == 1024.0 * 1024.0);
static_assert(to_bytes(1.0, "GiB") == 1024.0 * 1024.0 * 1024.0);
static_assert(to_bytes(1.0, "tb") == 1024.0 * 1024.0 * 1024.0 * 1024.0);
static_assert(to_bytes(1.0, "PiB") == 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0);
static_assert(to_bytes(8.0, "kbit") == 1024.0);
static_assert(to_bytes(1.0, "pbit") == 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 / 8.0);

// ── to_seconds ───────────────────────────────────────────────────────────────────────────────────
static_assert(to_seconds(2.0, "s") == 2.0);
static_assert(to_seconds(1500.0, "ms") == 1.5);
static_assert(to_seconds(1.0, "us") == 1.0 / 1'000'000.0);
static_assert(to_seconds(1.0, "µs") == 1.0 / 1'000'000.0);
static_assert(to_seconds(1.0, "ns") == 1.0 / 1'000'000'000.0);
static_assert(to_seconds(2.0, "Minutes") == 120.0);
static_assert(to_seconds(2.0, "h") == 7200.0);

// ── scale_down: divide by the step while a larger unit is left ───────────────────────────────────
static_assert(scale_down(512.0, 6, 1024.0) == scaled_value{512.0, 0});
static_assert(scale_down(2048.0, 6, 1024.0) == scaled_value{2.0, 1});
static_assert(scale_down(-3.0 * 1024.0 * 1024.0, 6, 1024.0) == scaled_value{-3.0, 2});
static_assert(scale_down(1024.0 * 1024.0 * 1024.0, 2, 1024.0).unit == 1);  // stops at the last unit

// ── bytes_per_second: what calculate_rate formats ────────────────────────────────────────────────
static_assert(bytes_per_second(1.0, "MiB", 1.0, "s") == 1024.0 * 1024.0);
static_assert(bytes_per_second(8.0, "mbit", 2.0, "s") == 1024.0 * 1024.0 / 2.0);
static_assert(bytes_per_second(1.0, "KiB", 500.0, "ms") == 2048.0);

}  // namespace
