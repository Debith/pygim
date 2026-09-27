#pragma once

// pygim.rng adapter — pybind11/NumPy boundary around rng::RngCore.
// Owns seed normalization, output-array allocation/validation, and GIL
// release around the pure-C++ fills. All generation logic lives in core.h.

#include <cstdint>
#include <new>
#include <random>
#include <string>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include "core.h"

namespace pygim {

namespace py = pybind11;

class Rng {
public:
    explicit Rng(const py::object& seed, int threads = 0, bool simd = true)
        : m_core(resolve_seed(seed), validate_threads(threads), simd) {}

    [[nodiscard]] py::array_t<double> random(py::ssize_t n) {
        ensure_size(n);
        auto out = aligned_empty<double>(n);
        fill_released(static_cast<double*>(out.mutable_data()), static_cast<std::size_t>(n),
                      [this](double* p, std::size_t c) { m_core.fill_f64(p, c); });
        return out;
    }

    [[nodiscard]] py::array_t<std::uint64_t> uint64(py::ssize_t n) {
        ensure_size(n);
        auto out = aligned_empty<std::uint64_t>(n);
        fill_released(static_cast<std::uint64_t*>(out.mutable_data()), static_cast<std::size_t>(n),
                      [this](std::uint64_t* p, std::size_t c) { m_core.fill_u64(p, c); });
        return out;
    }

    void fill(const py::object& out) {
        auto arr = validated_output<double>(out, "float64");
        fill_released(static_cast<double*>(arr.mutable_data()),
                      static_cast<std::size_t>(arr.size()),
                      [this](double* p, std::size_t c) { m_core.fill_f64(p, c); });
    }

    void fill_uint64(const py::object& out) {
        auto arr = validated_output<std::uint64_t>(out, "uint64");
        fill_released(static_cast<std::uint64_t*>(arr.mutable_data()),
                      static_cast<std::size_t>(arr.size()),
                      [this](std::uint64_t* p, std::size_t c) { m_core.fill_u64(p, c); });
    }

    [[nodiscard]] std::uint64_t seed() const noexcept { return m_core.seed(); }

    [[nodiscard]] std::string simd() const {
        return m_core.simd_active() ? "avx2" : "scalar";
    }

    [[nodiscard]] int threads() const noexcept { return m_core.threads_configured(); }

    [[nodiscard]] std::string repr() const {
        return "Rng(seed=" + std::to_string(seed()) + ", simd='" + simd() +
               "', threads=" + (threads() > 0 ? std::to_string(threads()) : std::string("auto")) + ")";
    }

private:
    static void ensure_size(py::ssize_t n) {
        if (n < 0) {
            throw py::value_error("n must be non-negative");
        }
    }

    [[nodiscard]] static int validate_threads(int threads) {
        if (threads < 0) {
            throw py::value_error("threads must be >= 0 (0 = auto)");
        }
        return threads;
    }

    // numpy's own allocator is frequently 16-mod-32 aligned, which would
    // disqualify the non-temporal store path; allocating our outputs
    // 64-byte aligned makes NT selection deterministic for this API.
    template <typename T>
    [[nodiscard]] static py::array_t<T> aligned_empty(py::ssize_t n) {
        const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(T);
        void* mem = ::operator new(bytes == 0 ? sizeof(T) : bytes, std::align_val_t{64});
        py::capsule owner(mem, [](void* p) { ::operator delete(p, std::align_val_t{64}); });
        return py::array_t<T>({n}, {static_cast<py::ssize_t>(sizeof(T))},
                              static_cast<T*>(mem), owner);
    }

    // In-place fills must never operate on a converted copy (the writes
    // would be silently lost), so instead of pybind's converting casters the
    // buffer is validated explicitly: ndarray, exact dtype, C-contiguous.
    template <typename T>
    [[nodiscard]] static py::array validated_output(const py::object& out, const char* dtype_name) {
        if (!py::isinstance<py::array>(out)) {
            throw py::type_error("out must be a numpy.ndarray");
        }
        auto arr = py::reinterpret_borrow<py::array>(out);
        // kind + itemsize rather than num(): dtype nums are storage-type
        // identities, so the equal-comparing 'Q' and 'L' spellings of uint64
        // carry different nums on LP64 platforms.
        if (arr.dtype().kind() != py::dtype::of<T>().kind() ||
            arr.itemsize() != static_cast<py::ssize_t>(sizeof(T))) {
            throw py::type_error(std::string("out must have dtype ") + dtype_name);
        }
        // kind/itemsize ignore endianness; a big-endian buffer would be
        // filled with native-order bytes and read back garbage.
        if (arr.dtype().byteorder() == '>') {
            throw py::type_error("out must be native byte order");
        }
        if ((arr.flags() & py::array::c_style) == 0) {
            throw py::type_error("out must be C-contiguous");
        }
        constexpr int kNpyArrayAligned = 0x0100;  // NPY_ARRAY_ALIGNED
        if ((arr.flags() & kNpyArrayAligned) == 0) {
            throw py::type_error("out must be element-aligned");
        }
        if (!arr.writeable()) {
            throw py::value_error("out must be writable");
        }
        return arr;
    }

    template <typename T, typename Fn>
    static void fill_released(T* data, std::size_t count, Fn&& fn) {
        if (count == 0) {
            return;
        }
        py::gil_scoped_release release;
        fn(data, count);
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
