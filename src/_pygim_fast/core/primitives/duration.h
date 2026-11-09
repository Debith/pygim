#pragma once
// core/primitives/duration.h — a length of time is one signed 64-bit count of nanoseconds; every
// unit is a conversion into it.
//
//     using namespace pygim::primitives::time_literals;
//     20_us, 1_min + 90_s, 1.5_s                                  -> duration: 20'000ns, 150 s, 1.5 s
//     time_unit::named("yr")->of(0.286)                           -> 9'025'288'271'999'999ns
//     spelt_in_units(150_s)                                       -> {min 2, s 30}
//
// The units are yr, wk, day, hr, min, s, ms, us and ns (D143), in C++ as literals (1_min) and in
// Python as Duration's keywords (Duration(min=1)): one table names them both.
//
// `duration` is pygim's own class, not std::chrono's (D142): a primitive puts what it builds on
// behind a class of pygim's, so std::chrono is named here and in timestamp.h only, and the layering
// check holds every other file to that. The lengths are std::chrono's all the same, a year its
// 365.2425 days, so C++ and Python agree. An amount may be a fraction: the float's exact value times
// the unit, rounded to the nearest nanosecond, ties to even. That is computed from the float's bits
// with integer arithmetic, because a year is 55 bits of nanoseconds and a float product would miss
// by one (yr=0.286 rounds to ...272000000 instead of ...271999999). Past 64 bits is
// std::overflow_error and a non-finite amount std::domain_error: never a wrapped or invented value.
//
// CORE layer: pybind-free and constexpr.

#include <array>
#include <bit>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pygim::primitives {

/// A length of time: one signed 64-bit count of nanoseconds. Plain + and - wrap past 64 bits as
/// integers do; sum_exactly refuses to.
class duration {
public:
    using rep = std::int64_t;

    constexpr duration() noexcept = default;
    /// `nanoseconds` nanoseconds.
    constexpr explicit duration(rep nanoseconds) noexcept : m_ns(nanoseconds) {}
    /// A std::chrono length, exactly: how the unit table takes std::chrono's lengths, and the one
    /// door from the standard library in.
    template <class Rep, class Period>
    [[nodiscard]] static constexpr duration from_chrono(std::chrono::duration<Rep, Period> d) noexcept {
        return duration(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    }
    /// The same length as std::chrono's, for a standard library call that takes one (a condition
    /// variable's wait): the one door out.
    [[nodiscard]] constexpr std::chrono::nanoseconds as_chrono() const noexcept { return std::chrono::nanoseconds(m_ns); }

    /// The count itself.
    [[nodiscard]] constexpr rep nanoseconds() const noexcept { return m_ns; }

    [[nodiscard]] constexpr duration operator-() const noexcept { return duration(-m_ns); }
    constexpr duration& operator+=(duration other) noexcept {
        m_ns += other.m_ns;
        return *this;
    }
    constexpr duration& operator-=(duration other) noexcept {
        m_ns -= other.m_ns;
        return *this;
    }
    [[nodiscard]] friend constexpr duration operator+(duration a, duration b) noexcept { return duration(a.m_ns + b.m_ns); }
    [[nodiscard]] friend constexpr duration operator-(duration a, duration b) noexcept { return duration(a.m_ns - b.m_ns); }
    [[nodiscard]] friend constexpr duration operator*(duration d, rep k) noexcept { return duration(d.m_ns * k); }
    [[nodiscard]] friend constexpr duration operator*(rep k, duration d) noexcept { return duration(k * d.m_ns); }
    [[nodiscard]] friend constexpr duration operator/(duration d, rep k) noexcept { return duration(d.m_ns / k); }
    /// How many whole `b` fit in `a`, truncated toward zero: 1_ms / 1_us is 1'000.
    [[nodiscard]] friend constexpr rep operator/(duration a, duration b) noexcept { return a.m_ns / b.m_ns; }
    [[nodiscard]] friend constexpr duration operator%(duration a, duration b) noexcept { return duration(a.m_ns % b.m_ns); }
    friend constexpr bool operator==(duration, duration) noexcept = default;
    friend constexpr std::strong_ordering operator<=>(duration, duration) noexcept = default;

private:
    rep m_ns = 0;
};
static_assert(duration(3) + duration(4) == duration(7) && duration(10) / duration(3) == 3 && -duration(2) < duration(0) &&
              duration::from_chrono(std::chrono::microseconds(2)) == duration(2'000) &&
              duration(5).as_chrono() == std::chrono::nanoseconds(5));

/// The duration in seconds, as a float: a view for printing and for metrics, never a store.
[[nodiscard]] constexpr double seconds_of(duration d) noexcept { return static_cast<double>(d.nanoseconds()) / 1e9; }
static_assert(seconds_of(duration(2'500)) == 2.5e-6);

/// a + b, or std::overflow_error: the class's own + wraps past 64 bits, as an integer's does.
[[nodiscard]] constexpr duration sum_exactly(duration a, duration b) {
    constexpr auto max = std::numeric_limits<duration::rep>::max();
    constexpr auto min = std::numeric_limits<duration::rep>::min();
    const auto x = a.nanoseconds(), y = b.nanoseconds();
    if ((y > 0 && x > max - y) || (y < 0 && x < min - y)) {
        throw std::overflow_error("a duration past 64 bits of nanoseconds (about 292 years)");
    }
    return a + b;
}
static_assert(sum_exactly(duration(1), duration(2)) == duration(3));

namespace detail {

/// m × u as an unsigned 128-bit value, and its quotient by a power of two rounded half to even:
/// what exact scaling needs, and nothing more.
class wide_product {
public:
    constexpr wide_product(std::uint64_t m, std::uint64_t u) noexcept {
        constexpr std::uint64_t low32 = 0xffff'ffffu;
        const std::uint64_t ll = (m & low32) * (u & low32);
        const std::uint64_t hl = (m >> 32) * (u & low32);
        const std::uint64_t lh = (m & low32) * (u >> 32);
        const std::uint64_t hh = (m >> 32) * (u >> 32);
        const std::uint64_t cross = (ll >> 32) + (hl & low32) + lh;
        m_hi = hh + (hl >> 32) + (cross >> 32);
        m_lo = (cross << 32) | (ll & low32);
    }

    /// round(this / 2^s) for s > 0, ties to even; nothing when the result needs more than 64 bits.
    [[nodiscard]] constexpr std::optional<std::uint64_t> over_power_of_two(int s) const noexcept {
        if (s > 128) return std::uint64_t{0};   // this < 2^128 <= 2^(s-1): below a half
        if (s == 128) {                           // a half is 2^127: above it rounds to 1, the tie to 0 (even)
            constexpr std::uint64_t top = std::uint64_t{1} << 63;
            return std::uint64_t{m_hi > top || (m_hi == top && m_lo != 0)};
        }
        std::uint64_t q = 0, r_hi = 0, r_lo = 0, half_hi = 0, half_lo = 0;
        if (s >= 64) {
            const int t = s - 64;
            q = t == 0 ? m_hi : m_hi >> t;
            r_hi = t == 0 ? 0 : m_hi & ((std::uint64_t{1} << t) - 1);
            r_lo = m_lo;
            if (s - 1 >= 64) half_hi = std::uint64_t{1} << (s - 1 - 64);
            else half_lo = std::uint64_t{1} << 63;
        } else {
            if ((m_hi >> s) != 0) return std::nullopt;
            q = (m_hi << (64 - s)) | (m_lo >> s);
            r_lo = m_lo & ((std::uint64_t{1} << s) - 1);
            half_lo = std::uint64_t{1} << (s - 1);
        }
        const bool above = r_hi > half_hi || (r_hi == half_hi && r_lo > half_lo);
        const bool tie = r_hi == half_hi && r_lo == half_lo;
        if (above || (tie && (q & 1u) != 0)) {
            if (q == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
            ++q;
        }
        return q;
    }

private:
    std::uint64_t m_hi = 0;
    std::uint64_t m_lo = 0;
};

static_assert(wide_product(3, 5).over_power_of_two(1) == 8u);   // 15 / 2 = 7.5 -> 8 (to even)
static_assert(wide_product(5, 5).over_power_of_two(1) == 12u);  // 25 / 2 = 12.5 -> 12 (to even)
static_assert(wide_product(~std::uint64_t{0}, ~std::uint64_t{0}).over_power_of_two(128) == 1u &&      // 2^128 - 2^65 + 1: above 2^127
              wide_product(std::uint64_t{1} << 63, ~std::uint64_t{0}).over_power_of_two(128) == 0u &&  // 2^127 - 2^63: below
              wide_product(~std::uint64_t{0}, ~std::uint64_t{0}).over_power_of_two(129) == 0u);        // below a half of 2^129
static_assert(wide_product(std::uint64_t{1} << 63, 4).over_power_of_two(64) == 2u);   // 2^65 / 2^64

}  // namespace detail

/// A unit of time — its name and its length — and the conversion of an amount of it into a duration.
class time_unit {
public:
    constexpr time_unit(std::string_view name, duration length) noexcept : m_name(name), m_length(length) {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] constexpr duration length() const noexcept { return m_length; }

    /// A whole `amount` of this unit, exactly; std::overflow_error past 64 bits.
    template <std::integral Whole>
    [[nodiscard]] constexpr duration of(Whole amount) const {
        if constexpr (std::is_unsigned_v<Whole>) {
            if (amount > static_cast<std::make_unsigned_t<duration::rep>>(std::numeric_limits<duration::rep>::max())) throw_past_64_bits();
        }
        return of_count(static_cast<std::int64_t>(amount));
    }

    /// A fractional `amount` of this unit: its exact value rounded to the nearest nanosecond, ties
    /// to even; std::domain_error for a NaN or an infinity, std::overflow_error past 64 bits.
    template <std::floating_point Real>
    [[nodiscard]] constexpr duration of(Real amount) const {
        return of_real(static_cast<double>(amount));
    }

    /// The unit spelt `name` ("y" ... "ns"), or nothing.
    [[nodiscard]] static constexpr std::optional<time_unit> named(std::string_view name) noexcept;

private:
    std::string_view m_name;
    duration m_length;

    [[noreturn]] static void throw_past_64_bits() {
        throw std::overflow_error("a duration past 64 bits of nanoseconds (about 292 years)");
    }

    [[nodiscard]] constexpr duration of_count(std::int64_t amount) const {
        constexpr auto max = std::numeric_limits<duration::rep>::max();
        constexpr auto min = std::numeric_limits<duration::rep>::min();
        const auto unit = m_length.nanoseconds();
        if (amount > max / unit || amount < min / unit) throw_past_64_bits();
        return duration(amount * unit);
    }

    [[nodiscard]] constexpr duration of_real(double amount) const {
        const auto bits = std::bit_cast<std::uint64_t>(amount);
        const bool negative = (bits >> 63) != 0;
        const int biased = static_cast<int>((bits >> 52) & 0x7ffu);
        if (biased == 0x7ff) throw std::domain_error("a duration of a NaN or an infinity");
        const std::uint64_t fraction = bits & ((std::uint64_t{1} << 52) - 1);
        const std::uint64_t mantissa = biased == 0 ? fraction : fraction | (std::uint64_t{1} << 52);
        const int exponent = (biased == 0 ? 1 : biased) - 1075;   // amount = ±mantissa × 2^exponent
        if (exponent >= 0) return of_whole(amount);               // a float this large is a whole number
        const auto magnitude = detail::wide_product(mantissa, static_cast<std::uint64_t>(m_length.nanoseconds()))
                                   .over_power_of_two(-exponent);
        constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<duration::rep>::max());
        if (!magnitude || *magnitude > limit + (negative ? 1 : 0)) throw_past_64_bits();
        const auto ns = negative ? static_cast<duration::rep>(0 - *magnitude) : static_cast<duration::rep>(*magnitude);
        return duration(ns);
    }

    [[nodiscard]] constexpr duration of_whole(double amount) const {
        if (amount >= 0x1p63 || amount < -0x1p63) throw_past_64_bits();
        return of_count(static_cast<std::int64_t>(amount));
    }
};
static_assert(time_unit("s", duration(1'000'000'000)).of(2) == duration(2'000'000'000) && time_unit("s", duration(1)).name() == "s");

/// Every unit, longest first: the names Duration(**units) takes and the literals spell (D143), the
/// order repr spells them in. Each length is std::chrono's, a year its 365.2425 days.
inline constexpr std::array<time_unit, 9> time_units{{
    {"yr", duration::from_chrono(std::chrono::years(1))},
    {"wk", duration::from_chrono(std::chrono::weeks(1))},
    {"day", duration::from_chrono(std::chrono::days(1))},
    {"hr", duration::from_chrono(std::chrono::hours(1))},
    {"min", duration::from_chrono(std::chrono::minutes(1))},
    {"s", duration::from_chrono(std::chrono::seconds(1))},
    {"ms", duration::from_chrono(std::chrono::milliseconds(1))},
    {"us", duration::from_chrono(std::chrono::microseconds(1))},
    {"ns", duration(1)},
}};

constexpr std::optional<time_unit> time_unit::named(std::string_view name) noexcept {
    for (const time_unit& u : time_units) {
        if (u.name() == name) return u;
    }
    return std::nullopt;
}

static_assert(time_unit::named("yr")->length() == duration(31'556'952'000'000'000));
static_assert(!time_unit::named("months") && !time_unit::named("m"));
static_assert(time_unit::named("min")->of(1) + time_unit::named("s")->of(90) == duration(150'000'000'000));
static_assert(time_unit::named("ns")->of(2.5) == duration(2) && time_unit::named("ns")->of(-1.5) == duration(-2));
static_assert(time_unit::named("yr")->of(0.286) == duration(9'025'288'271'999'999));

/// One amount of one unit: a part of how a duration is spelt.
class unit_amount {
public:
    constexpr unit_amount() noexcept = default;
    constexpr unit_amount(const time_unit& unit, std::int64_t amount) noexcept : m_unit(unit), m_amount(amount) {}
    [[nodiscard]] constexpr std::string_view unit_name() const noexcept { return m_unit.name(); }
    [[nodiscard]] constexpr std::int64_t amount() const noexcept { return m_amount; }

private:
    time_unit m_unit{"", duration(0)};
    std::int64_t m_amount = 0;
};
static_assert(unit_amount(time_unit("min", duration(60'000'000'000)), 3).unit_name() == "min" && unit_amount().amount() == 0);

/// How a duration is spelt in whole units, longest first, each amount the most that fits: 150 s is
/// {min 2, s 30}; a negative duration's amounts are all negative; zero is spelt with none.
class spelt_in_units {
public:
    constexpr explicit spelt_in_units(duration d) noexcept {
        auto rest = d.nanoseconds();
        for (const time_unit& u : time_units) {
            const auto amount = rest / u.length().nanoseconds();   // truncates toward zero: same sign as rest
            if (amount == 0) continue;
            m_parts[m_size++] = unit_amount(u, amount);
            rest -= amount * u.length().nanoseconds();
        }
    }
    [[nodiscard]] constexpr const unit_amount* begin() const noexcept { return m_parts.data(); }
    [[nodiscard]] constexpr const unit_amount* end() const noexcept { return m_parts.data() + m_size; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }

private:
    std::array<unit_amount, time_units.size()> m_parts{};
    std::size_t m_size = 0;
};

static_assert([] {
    const spelt_in_units s(duration(150'000'000'000));
    return s.size() == 2 && s.begin()[0].unit_name() == "min" && s.begin()[0].amount() == 2 &&
           s.begin()[1].unit_name() == "s" && s.begin()[1].amount() == 30;
}());
static_assert(spelt_in_units(duration(0)).size() == 0);

/// The units as C++ literals, the names Python's Duration takes (D143): 20_us, 1_min, 1.5_s. A whole
/// amount is exact; a fraction rounds as time_unit::of does. Evaluated at compile time, so a literal
/// past 64 bits does not compile. In namespace pygim::primitives they are in scope; elsewhere,
/// `using namespace pygim::primitives::time_literals;`.
inline namespace time_literals {

namespace detail_literals {
[[nodiscard]] consteval const time_unit& unit(std::string_view name) {
    for (const time_unit& u : time_units) {
        if (u.name() == name) return u;
    }
    throw std::logic_error("no such unit");
}
}  // namespace detail_literals

[[nodiscard]] consteval duration operator""_yr(unsigned long long n) { return detail_literals::unit("yr").of(n); }
[[nodiscard]] consteval duration operator""_yr(long double x) { return detail_literals::unit("yr").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_wk(unsigned long long n) { return detail_literals::unit("wk").of(n); }
[[nodiscard]] consteval duration operator""_wk(long double x) { return detail_literals::unit("wk").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_day(unsigned long long n) { return detail_literals::unit("day").of(n); }
[[nodiscard]] consteval duration operator""_day(long double x) { return detail_literals::unit("day").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_hr(unsigned long long n) { return detail_literals::unit("hr").of(n); }
[[nodiscard]] consteval duration operator""_hr(long double x) { return detail_literals::unit("hr").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_min(unsigned long long n) { return detail_literals::unit("min").of(n); }
[[nodiscard]] consteval duration operator""_min(long double x) { return detail_literals::unit("min").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_s(unsigned long long n) { return detail_literals::unit("s").of(n); }
[[nodiscard]] consteval duration operator""_s(long double x) { return detail_literals::unit("s").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_ms(unsigned long long n) { return detail_literals::unit("ms").of(n); }
[[nodiscard]] consteval duration operator""_ms(long double x) { return detail_literals::unit("ms").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_us(unsigned long long n) { return detail_literals::unit("us").of(n); }
[[nodiscard]] consteval duration operator""_us(long double x) { return detail_literals::unit("us").of(static_cast<double>(x)); }
[[nodiscard]] consteval duration operator""_ns(unsigned long long n) { return detail_literals::unit("ns").of(n); }
[[nodiscard]] consteval duration operator""_ns(long double x) { return detail_literals::unit("ns").of(static_cast<double>(x)); }

}  // namespace time_literals

static_assert(20_us == duration(20'000) && 1_min + 90_s == 2_min + 30_s && 1.5_s == duration(1'500'000'000) && 1_yr == time_units[0].length());

}  // namespace pygim::primitives
