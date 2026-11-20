#pragma once
// errors/core.h — the errors pygim's C++ throws, as classes of its own: one per meaning, each the
// counterpart of the pygim.errors class a Python caller catches (errors/table.h), and each saying
// its own message. Code with no Python throws them, a C++-only consumer catches them by class, and
// the translator errors::load() registers turns each into its Python class (errors/adapter.h).
//
// The messages are made here, once: what() says them in C++, and the Python classes' __str__
// calls the same functions, so the two cannot drift apart.
//
// CORE layer: the standard library only.
//
//     throw pygim::errors::missing_key("hostt", {"host", "port"});
//     // what(): Key ``hostt`` not among: host, port      Python: MissingKeyError, also a KeyError

#include <algorithm>
#include <cstddef>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace pygim::errors {

/// How many keys a message lists before it says only how many there are, and the closest.
inline constexpr std::size_t listed_at_most = 10;
/// How many of the closest a message names.
inline constexpr std::size_t closest_at_most = 3;

namespace detail {

/// `parts` joined by `separator`.
constexpr std::string joined(std::span<const std::string> parts, std::string_view separator) {
    std::string said;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) said += separator;
        said += parts[i];
    }
    return said;
}

/// `count` in decimal digits (std::to_string is not constexpr before C++26).
constexpr std::string decimal(std::size_t count) {
    std::string digits;
    do {
        digits.insert(digits.begin(), static_cast<char>('0' + count % 10));
        count /= 10;
    } while (count != 0);
    return digits;
}

/// CPython's measure for "Did you mean" (Python/suggestions.c), byte for byte over UTF-8: a
/// character moved, added or removed costs 2, a case flip 1.
inline constexpr std::size_t move_cost = 2;
inline constexpr std::size_t case_cost = 1;
/// Past this many bytes, after the common ends are trimmed, Python suggests nothing.
inline constexpr std::size_t compared_at_most = 40;
/// Among this many candidates or more, Python suggests nothing.
inline constexpr std::size_t suggested_among_fewer_than = 750;

constexpr std::size_t substitution_cost(char a, char b) {
    if ((a & 31) != (b & 31)) return move_cost;
    if (a == b) return 0;
    if ('A' <= a && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
    if ('A' <= b && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
    return a == b ? case_cost : move_cost;
}

/// The cost of turning `a` into `b`, or more than `max_cost` once it is certain to exceed it.
constexpr std::size_t edit_cost(std::string_view a, std::string_view b, std::size_t max_cost) {
    while (!a.empty() && !b.empty() && a.front() == b.front()) a.remove_prefix(1), b.remove_prefix(1);
    while (!a.empty() && !b.empty() && a.back() == b.back()) a.remove_suffix(1), b.remove_suffix(1);
    if (a.empty() || b.empty()) return (a.size() + b.size()) * move_cost;
    if (a.size() > compared_at_most || b.size() > compared_at_most) return max_cost + 1;
    if (b.size() < a.size()) std::swap(a, b);
    if ((b.size() - a.size()) * move_cost > max_cost) return max_cost + 1;
    std::vector<std::size_t> row(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) row[i] = (i + 1) * move_cost;
    std::size_t result = 0;
    for (std::size_t j = 0; j < b.size(); ++j) {
        std::size_t distance = result = j * move_cost;
        std::size_t minimum = static_cast<std::size_t>(-1);
        for (std::size_t i = 0; i < a.size(); ++i) {
            const std::size_t substitute = distance + substitution_cost(b[j], a[i]);
            distance = row[i];
            result = std::min(std::min(result, distance) + move_cost, substitute);
            row[i] = result;
            minimum = std::min(minimum, result);
        }
        if (minimum > max_cost) return max_cost + 1;
    }
    return result;
}

}  // namespace detail

/// The candidates closest to `name`, at most `at_most`, by the measure and the limit CPython's
/// "Did you mean" uses: no more than a third of the characters involved may change. The closest
/// first, in the candidates' own order among equals; none when there are 750 candidates or more.
constexpr std::vector<std::string> closest(std::string_view name, std::span<const std::string> candidates,
                                           std::size_t at_most = closest_at_most) {
    if (candidates.size() >= detail::suggested_among_fewer_than) return {};
    std::vector<std::pair<std::size_t, std::size_t>> close_ones;   // (cost, index)
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::string_view candidate = candidates[i];
        if (candidate == name) continue;
        const std::size_t max_cost = (name.size() + candidate.size() + 3) * detail::move_cost / 6;
        const std::size_t cost = detail::edit_cost(name, candidate, max_cost);
        if (cost <= max_cost) close_ones.emplace_back(cost, i);
    }
    // By cost, then by the candidates' order (std::stable_sort is not constexpr).
    std::sort(close_ones.begin(), close_ones.end());
    std::vector<std::string> names;
    for (std::size_t i = 0; i < close_ones.size() && i < at_most; ++i) names.push_back(candidates[close_ones[i].second]);
    return names;
}

/// "Given input ``AVG`` not among: sum, max, min, replace"
constexpr std::string option_message(std::string_view value, std::span<const std::string> options) {
    return "Given input ``" + std::string(value) + "`` not among: " + detail::joined(options, ", ");
}

/// "Key ``hots`` not among: host, port", up to listed_at_most keys; past that, how many and the
/// closest: "Key ``hostt`` not among 13 keys; closest: host".
/// With no keys: "Key ``hots`` not found".
constexpr std::string missing_key_message(std::string_view key, std::span<const std::string> keys) {
    std::string said = "Key ``" + std::string(key) + "``";
    if (keys.empty()) return said + " not found";
    if (keys.size() <= listed_at_most) return said + " not among: " + detail::joined(keys, ", ");
    said += " not among " + detail::decimal(keys.size()) + " keys";
    const std::vector<std::string> close_ones = closest(key, keys);
    if (!close_ones.empty()) said += "; closest: " + detail::joined(close_ones, ", ");
    return said;
}

/// "Key ``hots`` not found in `Registry`": where the keys could not be read.
constexpr std::string missing_key_message(std::string_view key, std::string_view where) {
    return "Key ``" + std::string(key) + "`` not found in `" + std::string(where) + "`";
}

/// What each of pygim's C++ errors means: one value for each pygim.errors class it becomes, which
/// the translator in errors/adapter.h maps, and refuses to build without.
enum class kind : unsigned char {
    error,                     ///< GimError
    missing_key,               ///< MissingKeyError, also a KeyError
    option,                    ///< GimOptionError, also a ValueError
    dispatch,                  ///< DispatchError
    argument,                  ///< ArgumentError, also a TypeError
    signature,                 ///< SignatureError
    invalid_value,             ///< InvalidValueError, also a ValueError
    parse,                     ///< ParseError, also a ValueError
    invalid_state,             ///< InvalidStateError
    duplicate_key,             ///< DuplicateKeyError
    circular_dependency,       ///< CircularDependencyError
    unsupported_column_type,   ///< UnsupportedColumnTypeError
    backend,                   ///< BackendError
    file,                      ///< GimOSError, or the one of its family the errno names
    overflow,                  ///< GimOverflowError, also an OverflowError
    index_out_of_range,        ///< IndexOutOfRangeError, also an IndexError
    missing_attribute,         ///< MissingAttributeError, also an AttributeError
};
/// How many kinds there are: the translator's table has one row for each.
inline constexpr std::size_t kinds = static_cast<std::size_t>(kind::missing_attribute) + 1;

/// constexpr where constant evaluation can throw and catch an exception (C++26, P3068: GCC 16), as it
/// can std::runtime_error there; elsewhere a constructor over std::exception's is not one.
#if defined(__cpp_constexpr_exceptions)
#define PYGIM_ERRORS_CONSTEXPR constexpr
#else
#define PYGIM_ERRORS_CONSTEXPR
#endif

/// Everything pygim's C++ throws; its Python counterpart is GimError, or the class its kind names.
/// A frame the error passes through may add a note, as Python's add_note does (PEP 678): what it was
/// doing, such as which key it was resolving. The notes reach Python as the exception's __notes__,
/// shown under its message in a traceback, and leave the message itself as the class says it.
/// constexpr, as std::runtime_error is in C++26: constant evaluation can throw and catch it there.
class error : public std::exception {
public:
    PYGIM_ERRORS_CONSTEXPR explicit error(std::string message, kind meaning = kind::error) : m_message(std::move(message)), m_kind(meaning) {}
    [[nodiscard]] PYGIM_ERRORS_CONSTEXPR const char* what() const noexcept override { return m_message.c_str(); }
    [[nodiscard]] PYGIM_ERRORS_CONSTEXPR kind meaning() const noexcept { return m_kind; }
    PYGIM_ERRORS_CONSTEXPR void add_note(std::string note) { m_notes.push_back(std::move(note)); }
    [[nodiscard]] PYGIM_ERRORS_CONSTEXPR std::span<const std::string> notes() const noexcept { return m_notes; }

private:
    std::string m_message;
    kind m_kind;
    std::vector<std::string> m_notes;
};

/// A key as a message says it: its own text where it has one; a core generic over its key, which
/// cannot say any other, leaves naming it to the adapter that can.
template <class Key>
constexpr std::string key_text(const Key& key) {
    if constexpr (std::is_convertible_v<const Key&, std::string_view>) return std::string(std::string_view(key));
    else return "(a key this core cannot name)";
}

/// An error whose message is all it carries, of the kind `Meaning`.
template <kind Meaning>
class error_of : public error {
public:
    PYGIM_ERRORS_CONSTEXPR explicit error_of(std::string message) : error(std::move(message), Meaning) {}
};

using dispatch_error = error_of<kind::dispatch>;                   ///< a call none of its implementations takes
using argument_error = error_of<kind::argument>;                   ///< a call pygim cannot take as given
using signature_error = error_of<kind::signature>;                 ///< a callable whose signature pygim cannot use
using invalid_value = error_of<kind::invalid_value>;               ///< the right type, outside what is accepted
using parse_error = error_of<kind::parse>;                         ///< text that is not its format
using invalid_state = error_of<kind::invalid_state>;               ///< not allowed in the object's state
using duplicate_key = error_of<kind::duplicate_key>;               ///< a key registered twice
using circular_dependency = error_of<kind::circular_dependency>;   ///< wiring that depends on itself
using unsupported_column_type = error_of<kind::unsupported_column_type>;   ///< a column type pygim cannot map
using backend_error = error_of<kind::backend>;                     ///< a library or driver pygim calls failed
using overflow = error_of<kind::overflow>;                         ///< past what its type holds
using index_out_of_range = error_of<kind::index_out_of_range>;     ///< a position past the end
using missing_attribute = error_of<kind::missing_attribute>;       ///< an attribute that is not there

/// A lookup found nothing: the key, and the keys it was looked for among (none when not known).
/// Python: MissingKeyError(key, within), also a KeyError.
class missing_key : public error {
public:
    PYGIM_ERRORS_CONSTEXPR explicit missing_key(std::string key, std::vector<std::string> keys = {})
        : error(missing_key_message(key, keys), kind::missing_key), m_key(std::move(key)), m_keys(std::move(keys)) {}
    [[nodiscard]] PYGIM_ERRORS_CONSTEXPR const std::string& key() const noexcept { return m_key; }
    [[nodiscard]] PYGIM_ERRORS_CONSTEXPR std::span<const std::string> keys() const noexcept { return m_keys; }

private:
    std::string m_key;
    std::vector<std::string> m_keys;
};

/// A value none of its options. Python: GimOptionError(value, options), also a ValueError.
class option_error : public error {
public:
    PYGIM_ERRORS_CONSTEXPR option_error(std::string value, std::vector<std::string> options)
        : error(option_message(value, options), kind::option), m_value(std::move(value)), m_options(std::move(options)) {}
    [[nodiscard]] PYGIM_ERRORS_CONSTEXPR const std::string& value() const noexcept { return m_value; }
    [[nodiscard]] PYGIM_ERRORS_CONSTEXPR std::span<const std::string> options() const noexcept { return m_options; }

private:
    std::string m_value;
    std::vector<std::string> m_options;
};

/// The system refused a file operation: its errno, said as the system says it, and the file.
/// Python: GimOSError, or GimFileNotFoundError and the rest of its family by the errno, as pathlib
/// raises them: "[Errno 2] No such file or directory: 'x.toml'".
class file_error : public error {
public:
    file_error(int number, std::string file)
        : error(said(number, file), kind::file), m_number(number), m_reason(reason(number)), m_file(std::move(file)) {}
    [[nodiscard]] int number() const noexcept { return m_number; }
    [[nodiscard]] const std::string& reason() const noexcept { return m_reason; }
    [[nodiscard]] const std::string& file() const noexcept { return m_file; }

private:
    static std::string reason(int number) { return std::error_code(number, std::generic_category()).message(); }
    static std::string said(int number, const std::string& file) {
        return "[Errno " + std::to_string(number) + "] " + reason(number) + ": '" + file + "'";
    }

    int m_number;
    std::string m_reason;
    std::string m_file;
};

namespace proofs {

constexpr std::vector<std::string> thirteen_keys() {
    std::vector<std::string> keys{"host"};
    for (std::size_t i = 0; i < 12; ++i) keys.push_back("port" + detail::decimal(i));
    return keys;
}

/// The keys `make()` gives, kept alive while `check` reads them (a span over a temporary is not
/// a constant expression).
constexpr bool holds(auto make, auto check) {
    const std::vector<std::string> values = make();
    return check(std::span<const std::string>(values));
}

constexpr std::vector<std::string> host_port() { return {"host", "port"}; }
constexpr std::vector<std::string> sum_max() { return {"sum", "max"}; }
constexpr std::vector<std::string> none() { return {}; }

}  // namespace proofs

static_assert(proofs::holds(proofs::sum_max, [](auto v) { return detail::joined(v, ", ") == "sum, max"; }));
static_assert(detail::decimal(0) == "0" && detail::decimal(750) == "750");
static_assert(detail::substitution_cost('a', 'A') == detail::case_cost && detail::substitution_cost('a', 'b') == detail::move_cost);
static_assert(detail::edit_cost("hst", "host", 3) == 2 && detail::edit_cost("hots", "host", 3) > 3);
static_assert(proofs::holds(proofs::thirteen_keys, [](auto keys) { return closest("hostt", keys) == std::vector<std::string>{"host"}; }));
static_assert(proofs::holds(proofs::thirteen_keys, [](auto keys) {
    return closest("port1x", keys) == std::vector<std::string>{"port1", "port10", "port11"};
}));
static_assert(proofs::holds(proofs::thirteen_keys, [](auto keys) { return closest("hots", keys).empty(); }),
              "Python suggests nothing for a swap of two letters");
static_assert(proofs::holds(proofs::sum_max, [](auto v) { return option_message("AVG", v) == "Given input ``AVG`` not among: sum, max"; }));
static_assert(proofs::holds(proofs::host_port, [](auto keys) { return missing_key_message("hots", keys) == "Key ``hots`` not among: host, port"; }));
static_assert(proofs::holds(proofs::none, [](auto keys) { return missing_key_message("hots", keys) == "Key ``hots`` not found"; }));
static_assert(proofs::holds(proofs::thirteen_keys, [](auto keys) {
    return missing_key_message("hostt", keys) == "Key ``hostt`` not among 13 keys; closest: host";
}));
static_assert(proofs::holds(proofs::thirteen_keys, [](auto keys) { return missing_key_message("hots", keys) == "Key ``hots`` not among 13 keys"; }));
static_assert(missing_key_message("port", std::string_view("Registry")) == "Key ``port`` not found in `Registry`");

}  // namespace pygim::errors
