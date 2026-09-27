#pragma once

// pygim.rng adapter — the pybind11 boundary around rng::RngCore: seed and
// thread-count validation, and the Python shape of the output (numpy, list,
// tuple, polars), which utils/py_output.h provides. All generation logic
// lives in core.h.

#include <cstdint>
#include <random>
#include <string>

#include <pybind11/pybind11.h>

#include "../utils/py_output.h"
#include "core.h"

namespace pygim {

namespace py = pybind11;

class Rng {
public:
    explicit Rng(const py::object& seed, int threads = 0, bool simd = true)
        : m_core(resolve_seed(seed), validate_threads(threads), simd) {}

    [[nodiscard]] py::object random(py::ssize_t n, const py::object& format) {
        return output::emit<double>(n, format, [this](double* p, std::size_t c) { m_core.fill_f64(p, c); });
    }

    [[nodiscard]] py::object uint64(py::ssize_t n, const py::object& format) {
        return output::emit<std::uint64_t>(n, format,
                                           [this](std::uint64_t* p, std::size_t c) { m_core.fill_u64(p, c); });
    }

    void fill(const py::object& out) {
        output::fill_buffer<double>(out, "float64", [this](double* p, std::size_t c) { m_core.fill_f64(p, c); });
    }

    void fill_uint64(const py::object& out) {
        output::fill_buffer<std::uint64_t>(out, "uint64",
                                           [this](std::uint64_t* p, std::size_t c) { m_core.fill_u64(p, c); });
    }

    [[nodiscard]] std::uint64_t seed() const noexcept { return m_core.seed(); }

    [[nodiscard]] std::string simd() const { return m_core.simd_active() ? "avx2" : "scalar"; }

    [[nodiscard]] int threads() const noexcept { return m_core.threads_configured(); }

    [[nodiscard]] std::string repr() const {
        return "Rng(seed=" + std::to_string(seed()) + ", simd='" + simd() +
               "', threads=" + (threads() > 0 ? std::to_string(threads()) : std::string("auto")) + ")";
    }

private:
    [[nodiscard]] static int validate_threads(int threads) {
        if (threads < 0) throw py::value_error("threads must be >= 0 (0 = auto)");
        return threads;
    }

    [[nodiscard]] static std::uint64_t resolve_seed(const py::object& seed) {
        if (seed.is_none()) {
            std::random_device rd;
            return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
        }
        // __index__ semantics: accepts int and integer-likes (numpy ints),
        // rejects Decimal/float/str instead of silently truncating them.
        PyObject* as_index = PyNumber_Index(seed.ptr());
        if (as_index == nullptr) {
            PyErr_Clear();
            throw py::type_error("seed must be None or an integer");
        }
        auto index = py::reinterpret_steal<py::object>(as_index);
        try {
            return index.cast<std::uint64_t>();
        } catch (const py::cast_error&) {
            throw py::value_error("seed must be in [0, 2**64)");
        }
    }

    rng::RngCore m_core;  //!< pybind-free generation engine
};

}  // namespace pygim
