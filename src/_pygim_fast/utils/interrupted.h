#pragma once
// utils/interrupted.h — a native run that owns Ctrl-C and SIGTERM for its length, and how one that a
// signal stopped is told to Python (#272, Q28). Every operation that runs a job on a pool with the
// GIL released — a glob that may list many directories, a PathSet read, code.parse, Index.add_files —
// runs it here.
//
//     std::vector<read_result> all = run_interruptible("read_all_files",
//         [&] { return batch_read(paths, std::move(width), every); },             // made with the GIL released
//         [](batch_read& job) { return job.collect(); });                          // taken once it finished
//
// ADAPTER layer (pybind11, the C API's signals): the core's interrupt_scope (utils/interrupt.h) owns
// the signals; this releases the GIL around the run and raises what Python's handler asks for.

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <pybind11/pybind11.h>

#include "interrupt.h"

namespace pygim {

namespace py = pybind11;

/// An interrupted run, told to Python after its clean stop: the signal has been raised again to
/// Python's own handler, which raises what the program asked for (KeyboardInterrupt, by default);
/// if it raised nothing — a handler of the program's own, or a call off the main thread — the call
/// raises InterruptedError, so a partial result is never taken for a whole one.
[[noreturn]] inline void raise_interrupted(int sig, std::string_view who) {
    if (PyErr_CheckSignals() != 0) throw py::error_already_set();
    PyErr_SetString(PyExc_InterruptedError, (std::string(who) + " was interrupted by signal " + std::to_string(sig)).c_str());
    throw py::error_already_set();
}

/// make() — a job with run_all(finished) — run with the GIL released and Ctrl-C and SIGTERM the
/// run's, then collect(job) once it finished. A signal stops the job at an item's edge (its workers
/// are joined as it goes), gives the handlers back, raises the signal again to them and then
/// raise_interrupted(`who`). One Python's handler took already — the caller's work before the run
/// let the GIL go — is raised before the run starts. No Python runs from make() to collect()'s
/// return: what they touch must be their own or copied (the GIL is the caller's again only after).
template <class Make, class Collect>
[[nodiscard]] auto run_interruptible(std::string_view who, Make&& make, Collect&& collect) {
    using job_type = std::invoke_result_t<Make&>;
    using result_type = std::invoke_result_t<Collect&, job_type&>;
    if (PyErr_CheckSignals() != 0) throw py::error_already_set();   // Ctrl-C before the run: no run
    std::optional<result_type> result;
    int interrupted_by = 0;
    {
        interrupt_scope scope;
        {
            py::gil_scoped_release nogil;
            job_type job = make();
            job.run_all([&scope] { scope.finish(); });
            if (scope.wait()) interrupted_by = scope.signal();
            else result.emplace(collect(job));
        }   // the job stops: an item in hand finishes, the workers are joined
    }       // the handlers come back; the signal is raised again to them
    if (interrupted_by != 0) raise_interrupted(interrupted_by, who);
    return std::move(*result);
}

}  // namespace pygim
