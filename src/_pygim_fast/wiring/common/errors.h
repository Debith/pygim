// wiring/common/errors.h
// What a wiring lookup raises when it finds nothing: a key with no provider, an unknown creator,
// a registry key never registered. Pybind-free; the adapters raise it as Python's KeyError
// (adapter_errors.h), the way a mapping does.
//
// Example: Container().resolve(IService) with nothing registered raises
//   KeyError: 'No provider for key [key: IService]'

#pragma once

#include <stdexcept>

namespace pygim::core {

/// A lookup that found nothing. Derives from std::runtime_error, so a module that registers no
/// translation still raises RuntimeError, as before.
struct missing_key : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace pygim::core
