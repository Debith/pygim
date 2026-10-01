#pragma once

// pygim._rng adapter — the pybind11 boundary around rng::RngCore. It checks
// the seed and thread count and fills caller-owned buffers in place; the
// containers random()/uint64() return are built in pygim/rng.py. All
// generation logic lives in core.h.

#include <bit>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "core.h"

namespace pygim {

namespace py = pybind11;

class Rng {
public:
    explicit Rng(const py::object& seed, int threads = 0, bool simd = true)
        : m_core(resolve_seed(seed), validate_threads(threads), simd) {}

    // Fills `out` in place: float64 elements with uniforms in [0, 1), uint64
    // elements with raw draws. Any writable, C-contiguous buffer of either
    // type qualifies (array.array, numpy, a memoryview, ...); never a
    // converted copy, whose writes would be silently lost.
    void fill(const py::object& out) {
        Buffer view(out);
        const char* format = view.format != nullptr ? view.format : "B";
        std::string_view code = format;
        if (!code.empty() && (code[0] == '@' || code[0] == '=' || code[0] == kNativeOrder)) {
            code.remove_prefix(1);  // only restates native byte order
        }
        const bool f64 = code == "d";
        if (view.itemsize != 8 || !(f64 || code == "Q" || code == "L")) {
            throw py::type_error("out must hold float64 or uint64 values, not format '" + std::string(format) + "'");
        }
        // (An empty buffer may point anywhere: array.array's points at a static "".)
        if (view.len != 0 && reinterpret_cast<std::uintptr_t>(view.buf) % 8 != 0) {
            throw py::type_error("out must be 8-byte aligned");
        }
        const auto n = static_cast<std::size_t>(view.len / 8);
        py::gil_scoped_release release;
        if (f64) {
            m_core.fill(static_cast<double*>(view.buf), n);
        } else {
            m_core.fill(static_cast<std::uint64_t*>(view.buf), n);
        }
    }

    [[nodiscard]] std::uint64_t seed() const noexcept { return m_core.seed(); }

    [[nodiscard]] std::string simd() const { return m_core.simd_active() ? "avx2" : "scalar"; }

    [[nodiscard]] int threads() const noexcept { return m_core.threads_configured(); }

    [[nodiscard]] std::string repr() const {
        return "Rng(seed=" + std::to_string(seed()) + ", simd='" + simd() +
               "', threads=" + (threads() > 0 ? std::to_string(threads()) : std::string("auto")) + ")";
    }

private:
    static constexpr char kNativeOrder = std::endian::native == std::endian::little ? '<' : '>';

    // A writable, C-contiguous view of an object's memory, held for the
    // duration of a fill (the exporter cannot resize it meanwhile).
    struct Buffer : Py_buffer {
        explicit Buffer(const py::object& obj) : Py_buffer{} {
            if (PyObject_GetBuffer(obj.ptr(), this, PyBUF_WRITABLE | PyBUF_FORMAT | PyBUF_C_CONTIGUOUS) != 0) {
                py::raise_from(PyExc_TypeError, "out must be a writable, C-contiguous buffer");
                throw py::error_already_set();
            }
        }
        ~Buffer() { PyBuffer_Release(this); }
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
    };

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
