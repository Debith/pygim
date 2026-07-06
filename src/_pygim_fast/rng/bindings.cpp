#include <pybind11/pybind11.h>

#include "adapter.h"

namespace py = pybind11;

PYBIND11_MODULE(rng, m) {
    m.doc() =
        "High-throughput pseudo-random number generation.\n\n"
        "16 interleaved xoshiro256++ streams evaluated with AVX2 (4 groups x 4\n"
        "lanes) when available, with block-parallel multithreaded fills. The\n"
        "emitted sequence is a pure function of the seed: identical across the\n"
        "SIMD and scalar paths, thread counts, and call-size splits.";

    py::class_<pygim::Rng>(m, "Rng",
        R"doc(Deterministic high-throughput random generator.

float64 outputs use the same 53-bit mapping as NumPy
((x >> 11) * 2**-53), drawn from xoshiro256++ streams seeded via
SplitMix64. Arrays returned by random()/uint64() are 64-byte aligned.

Thread safety
-------------
Instances are internally locked: concurrent calls from Python threads
are safe but serialize (and their interleaving order is not
deterministic). For parallel workloads prefer one Rng per thread with
distinct seeds; a single fill already parallelizes internally.
)doc")
        .def(py::init<const py::object&, int, bool>(),
             py::arg("seed") = py::none(),
             py::kw_only(),
             py::arg("threads") = 0,
             py::arg("simd") = true,
             R"doc(Create a generator.

Parameters
----------
seed : int or None
    None draws fresh entropy; otherwise an integer in [0, 2**64).
    Integer-likes supporting __index__ (e.g. numpy ints) are accepted.
threads : int, keyword-only
    Worker threads for large fills; 0 = auto, 1 = single-threaded.
simd : bool, keyword-only
    Allow the AVX2 path. Results are bit-identical either way.
)doc")
        .def("random", &pygim::Rng::random, py::arg("n"),
             "Return a new 1-D float64 array of n uniforms in [0, 1).")
        .def("uint64", &pygim::Rng::uint64, py::arg("n"),
             "Return a new 1-D uint64 array of n raw 64-bit draws.")
        .def("fill", &pygim::Rng::fill, py::arg("out"),
             "Fill a C-contiguous float64 array (any shape) in place with uniforms in [0, 1).")
        .def("fill_uint64", &pygim::Rng::fill_uint64, py::arg("out"),
             "Fill a C-contiguous uint64 array (any shape) in place with raw draws.")
        .def_property_readonly("seed", &pygim::Rng::seed, "The 64-bit seed in use.")
        .def_property_readonly("simd", &pygim::Rng::simd, "'avx2' or 'scalar'.")
        .def_property_readonly("threads", &pygim::Rng::threads, "Configured thread count (0 = auto).")
        .def("__repr__", &pygim::Rng::repr);
}
