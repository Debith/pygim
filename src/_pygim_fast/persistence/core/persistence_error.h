// persistence/core/persistence_error.h
// The error a persistence operation raises when it fails: a connection refused, a driver call
// failed, a pool exhausted, a batch Arrow could not read.
//
// Example: acquire_datastore("Driver={missing};").load("t") raises
//   pygim.persistence.PersistenceError: OdbcConnection: SQLDriverConnect failed: [01000] ...
// a RuntimeError (a failed operation) and a pygim.core.explib.GimError (one of pygim's own),
// registered by adapter/bindings.cpp for this module only.

#pragma once

#include <stdexcept>

namespace pygim::core {

/// A failed persistence operation. Derives from std::runtime_error, so C++ that catches one keeps
/// catching it; costs what the message costs, once, on the failure path.
struct persistence_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace pygim::core
