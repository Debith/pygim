// memory/core/ids.h — identity and arithmetic for the problem-space memory.
//
// Strong ids, the 128-bit digest that names content and audit rows, and the
// integer units every weight and score is kept in. Pybind-free and constexpr;
// the laws are proven in tests/static/memory_proofs.cpp. The design is
// docs/design/memory/01_domain_model.md, sections 2 and 3.
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace pygim::memory {

// ── strong ids ────────────────────────────────────────────────────────────

/// One template, a phantom tag per kind, so ids of different kinds never mix:
/// a `tag_id` passed where a `memory_id` belongs does not compile. The default
/// value is the invalid sentinel.
///
///     memory_id m{2};   m.value() -> 2   m.valid() -> true
///     memory_id{}.valid()            -> false
template <class Tag, std::unsigned_integral T = std::uint32_t>
class basic_id {
public:
    using value_type = T;
    static constexpr T npos = std::numeric_limits<T>::max();

    constexpr basic_id() noexcept = default;
    constexpr explicit basic_id(T v) noexcept : m_value(v) {}

    [[nodiscard]] constexpr T value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool valid() const noexcept { return m_value != npos; }

    friend constexpr auto operator<=>(basic_id, basic_id) noexcept = default;
    friend constexpr bool operator==(basic_id, basic_id) noexcept = default;

private:
    T m_value = npos;
};

struct memory_tag_;
struct tag_tag_;
struct dimension_tag_;

/// A memory's place in one snapshot — assigned in replay order, never written down.
using memory_id = basic_id<memory_tag_, std::uint32_t>;
/// A tag's place in one loaded vocabulary — assigned in load order, never written down.
using tag_id = basic_id<tag_tag_, std::uint16_t>;
/// A dimension's place in one loaded vocabulary.
using dimension_id = basic_id<dimension_tag_, std::uint8_t>;

// ── the digest ────────────────────────────────────────────────────────────

namespace detail {

/// One lane of the digest: the bytes taken eight at a time (assembled byte by
/// byte, so it is the same value at compile time and on any endianness), each
/// word folded into the state through the splitmix64 finaliser, the length
/// and the seed folded in at both ends. A different seed is a different
/// function, which is what makes the lanes of one digest independent.
[[nodiscard]] constexpr std::uint64_t mix(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

[[nodiscard]] constexpr std::uint64_t lane(std::string_view s, std::uint64_t seed) noexcept {
    std::uint64_t h = mix(seed ^ (0x9e3779b97f4a7c15ull * (s.size() + 1)));
    std::size_t i = 0;
    for (; i + 8 <= s.size(); i += 8) {
        std::uint64_t w = 0;
        for (std::size_t b = 0; b < 8; ++b)
            w |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i + b])) << (8 * b);
        h = mix(h ^ w) + 0x632be59bd9b4e019ull;
    }
    std::uint64_t tail = 0;
    for (std::size_t b = 0; i + b < s.size(); ++b)
        tail |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i + b])) << (8 * b);
    h = mix(h ^ tail ^ (static_cast<std::uint64_t>(s.size() - i) << 56));
    return mix(h ^ seed);
}

inline constexpr std::array<std::uint64_t, 4> lane_seeds{
    0x243f6a8885a308d3ull, 0x13198a2e03707344ull, 0xa4093822299f31d0ull, 0x082efa98ec4e6c89ull};

}  // namespace detail

/// A content hash: `Bits / 64` independent 64-bit lanes. It names a memory's
/// text, an audit row, and a vocabulary version (01 §2.2, 03 §2.1). Not a
/// cryptographic hash: a repository's trust model is git's (01, Decision 2.2).
///
///     digest::of("abc") == digest::of("abc")        // same bytes, same digest
///     digest::of("abc").hex().size() == 32
///     digest::from_hex(d.hex()) == d                // round trip
template <std::size_t Bits>
    requires(Bits % 64 == 0 && Bits >= 64 && Bits <= 256)
class basic_digest {
public:
    static constexpr std::size_t lanes = Bits / 64;

    constexpr basic_digest() noexcept = default;

    [[nodiscard]] static constexpr basic_digest of(std::string_view bytes) noexcept {
        basic_digest d;
        for (std::size_t i = 0; i < lanes; ++i) d.m_lanes[i] = detail::lane(bytes, detail::lane_seeds[i]);
        return d;
    }

    /// Lowercase hex, lane by lane, most significant nibble first.
    [[nodiscard]] constexpr std::string hex() const {
        constexpr char digits[] = "0123456789abcdef";
        std::string out(lanes * 16, '0');
        for (std::size_t i = 0; i < lanes; ++i)
            for (std::size_t n = 0; n < 16; ++n)
                out[i * 16 + n] = digits[(m_lanes[i] >> (60 - 4 * n)) & 0xf];
        return out;
    }

    /// The digest a `hex()` string names, or nullopt for anything else.
    [[nodiscard]] static constexpr std::optional<basic_digest> from_hex(std::string_view s) noexcept {
        if (s.size() != lanes * 16) return std::nullopt;
        basic_digest d;
        for (std::size_t i = 0; i < lanes; ++i) {
            std::uint64_t v = 0;
            for (std::size_t n = 0; n < 16; ++n) {
                const char c = s[i * 16 + n];
                std::uint64_t x;
                if (c >= '0' && c <= '9') x = static_cast<std::uint64_t>(c - '0');
                else if (c >= 'a' && c <= 'f') x = static_cast<std::uint64_t>(c - 'a' + 10);
                else return std::nullopt;
                v = (v << 4) | x;
            }
            d.m_lanes[i] = v;
        }
        return d;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        for (const auto l : m_lanes)
            if (l) return false;
        return true;
    }
    [[nodiscard]] constexpr std::uint64_t lane(std::size_t i) const noexcept { return m_lanes[i]; }

    friend constexpr auto operator<=>(const basic_digest&, const basic_digest&) noexcept = default;
    friend constexpr bool operator==(const basic_digest&, const basic_digest&) noexcept = default;

private:
    std::array<std::uint64_t, lanes> m_lanes{};
};

using digest = basic_digest<128>;
/// An audit row's id: the digest of the row (03 §2.1). A memory's key is the
/// id of the row that created it (03 §2.2).
using row_id = digest;

struct digest_hash {
    [[nodiscard]] constexpr std::size_t operator()(const digest& d) const noexcept {
        return static_cast<std::size_t>(d.lane(0) ^ (d.lane(1) * 0x9e3779b97f4a7c15ull));
    }
};

// ── canonical encoding ────────────────────────────────────────────────────

/// Appends `s` to `out` as `<length>:<bytes>`. Length-prefixing makes a
/// sequence of strings unambiguous whatever bytes they hold, which is what a
/// digest over several fields needs: ("ab","c") and ("a","bc") differ.
constexpr void encode_field(std::string& out, std::string_view s) {
    std::size_t n = s.size();
    char digits[24];
    std::size_t k = 0;
    do {
        digits[k++] = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n);
    while (k) out.push_back(digits[--k]);
    out.push_back(':');
    out.append(s);
}

// ── integer weights and scores ────────────────────────────────────────────

/// A dimension's weight in milli-units: 2.0 is 2000, 0.5 is 500 (01 §3).
using weight_t = std::int32_t;
/// A sum of weights, in milli-units; exact and order-independent.
using score_t = std::int64_t;

inline constexpr weight_t weight_scale = 1000;

/// A weight's text as milli-units, read digit by digit — never through a
/// floating-point number (02 §1.4). Accepts `2`, `2.0`, `1.5`, `0.125`;
/// refuses a sign, an exponent, more than three decimals, zero, and anything
/// above 1000.
///
///     parse_weight("1.5")    -> 1500
///     parse_weight("0.125")  -> 125
///     parse_weight("0.0001") -> nullopt       (not exact in milli-units)
[[nodiscard]] constexpr std::optional<weight_t> parse_weight(std::string_view s) noexcept {
    if (s.empty()) return std::nullopt;
    std::int64_t whole = 0;
    std::size_t i = 0;
    bool any_digit = false;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        whole = whole * 10 + (s[i] - '0');
        any_digit = true;
        if (whole > 1000) return std::nullopt;
    }
    std::int64_t frac = 0;
    std::size_t places = 0;
    if (i < s.size() && s[i] == '.') {
        ++i;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
            if (++places > 3) return std::nullopt;
            frac = frac * 10 + (s[i] - '0');
            any_digit = true;
        }
    }
    if (i != s.size() || !any_digit) return std::nullopt;
    while (places < 3) {
        frac *= 10;
        ++places;
    }
    const std::int64_t milli = whole * 1000 + frac;
    if (milli <= 0 || milli > 1000 * 1000) return std::nullopt;
    return static_cast<weight_t>(milli);
}

/// A non-negative integer as decimal text — `std::to_string` is not constexpr yet.
[[nodiscard]] constexpr std::string decimal(std::uint64_t n) {
    char digits[24];
    std::size_t k = 0;
    do {
        digits[k++] = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n);
    std::string out;
    while (k) out.push_back(digits[--k]);
    return out;
}

/// A weight in milli-units as the shortest decimal text that parses back to it.
///
///     weight_text(1500) -> "1.5"     weight_text(2000) -> "2.0"
[[nodiscard]] constexpr std::string weight_text(weight_t w) {
    std::string out = decimal(static_cast<std::uint64_t>(w / 1000));
    int frac = w % 1000;
    out.push_back('.');
    if (frac == 0) {
        out.push_back('0');
        return out;
    }
    char d[3] = {static_cast<char>('0' + frac / 100), static_cast<char>('0' + frac / 10 % 10),
                 static_cast<char>('0' + frac % 10)};
    std::size_t n = 3;
    while (n > 1 && d[n - 1] == '0') --n;
    out.append(d, n);
    return out;
}

}  // namespace pygim::memory
