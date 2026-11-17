// wiring/common/errors.h
// What wiring raises when a lookup finds nothing (a key with no provider, an unknown creator, a
// registry key never registered), and when a value is none of its options (an unknown lifecycle).
// Pybind-free; the adapters raise them as pygim's MissingKeyError and GimOptionError
// (adapter_errors.h), which are also Python's KeyError and ValueError.
//
// Example: Container().resolve(IService) with nothing registered raises
//   MissingKeyError: No provider for key [key: IService]

#pragma once

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pygim::core {

/// A lookup that found nothing. Derives from std::runtime_error, so a module that registers no
/// translation still raises RuntimeError, as before.
struct missing_key : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// A value that is none of the options it must be one of: what was given, and every option.
/// Derives from std::invalid_argument, so a module that registers no translation still raises
/// ValueError.
class unknown_option : public std::invalid_argument {
public:
    unknown_option(std::string value, std::initializer_list<const char*> options)
        : std::invalid_argument("'" + value + "' is none of the options"), m_value(std::move(value)), m_options(options.begin(), options.end()) {}

    [[nodiscard]] const std::string& value() const noexcept { return m_value; }
    [[nodiscard]] const std::vector<std::string>& options() const noexcept { return m_options; }

private:
    std::string m_value;
    std::vector<std::string> m_options;
};

} // namespace pygim::core
