// StatusPrinter — lightweight, flag-controlled status output for user-facing
// messages produced during repository operations (connection, schema discovery,
// etc.).
//
// These messages are intentionally distinct from structured logging
// (PYGIM_SCOPE_LOG_TAG / the log subsystem).  They are written directly to
// stdout for interactive consumption and can be suppressed per-flag so callers
// keep fine-grained control over what appears on the terminal.
//
// Usage (C++):
//   StatusPrinter p;                         // all flags on (default)
//   p.print_connection("connecting  mssql://host/db");
//
//   StatusPrinter quiet{/*.connection=*/false};  // suppress connection lines
//
// Usage (Python — exposed via pybind11 in _repository):
//   printer = StatusPrinter(connection=False)
//   repo = acquire_repository("mssql://host/db", printer=printer)
#pragma once

#include <iostream>
#include <string>

namespace pygim {

struct StatusPrinter {
    /// Print one status line per connection attempt when true (default).
    bool connection = true;

    void print_connection(const std::string &msg) const {
        if (connection) {
            std::cout << msg << '\n' << std::flush;
        }
    }
};

} // namespace pygim
