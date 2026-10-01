#include <pybind11/pybind11.h>

#include "adapter.h"

namespace py = pybind11;

PYBIND11_MODULE(_rng, m) {
    m.doc() =
        "The engine behind pygim.rng: 16 interleaved xoshiro256++ streams evaluated\n"
        "with AVX2 (4 groups x 4 lanes) when available, with block-parallel\n"
        "multithreaded fills. The emitted sequence is a pure function of the seed:\n"
        "identical across the SIMD and scalar paths, thread counts, and call-size\n"
        "splits. Use pygim.rng; this module only fills buffers.";

    py::class_<pygim::Rng>(m, "Rng")
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
        .def("fill", &pygim::Rng::fill, py::arg("out"),
             R"doc(Fill a buffer in place: float64 elements with uniforms in [0, 1),
uint64 elements with raw 64-bit draws.

Any writable, C-contiguous buffer of either type works, of any shape:
array.array('d' or 'Q'), a numpy array, a memoryview, ...)doc")
        .def_property_readonly("seed", &pygim::Rng::seed, "The 64-bit seed in use.")
        .def_property_readonly("simd", &pygim::Rng::simd, "'avx2' or 'scalar'.")
        .def_property_readonly("threads", &pygim::Rng::threads, "Configured thread count (0 = auto).")
        .def("__repr__", &pygim::Rng::repr);
}
