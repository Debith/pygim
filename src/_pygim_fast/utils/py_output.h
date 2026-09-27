#pragma once
// utils/py_output.h — hand n generated values to Python in the shape the
// caller asked for: a numpy array, a list, a tuple, or a polars Series or
// DataFrame.
//
// ADAPTER layer (pybind11). The producer is any callable `fill(T* out,
// std::size_t n)` that writes n values; it runs with the GIL released, so it
// must not touch Python. numpy and polars are optional at run time: they are
// imported when a format needs them, `format=None` picks numpy when it imports
// and a list otherwise, and `available()` lists what this interpreter can do.
// Only an ImportError counts as "not installed"; a broken install raises.

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace pygim::output {

namespace py = pybind11;

enum class Format { Auto, Numpy, List, Tuple, PolarsSeries, PolarsFrame };

[[nodiscard]] inline bool importable(const char* module) {
    try {
        py::module_::import(module);
        return true;
    } catch (py::error_already_set& e) {
        if (e.matches(PyExc_ImportError)) return false;
        throw;
    }
}

inline py::module_ require(const char* module, const std::string& format) {
    if (!importable(module)) {
        throw py::import_error("format='" + format + "' needs " + module + ", which is not installed");
    }
    return py::module_::import(module);
}

// The format names usable right now, in the order `parse` documents them.
[[nodiscard]] inline py::tuple available() {
    py::list names;
    if (importable("numpy")) names.append("numpy");
    names.append("list");
    names.append("tuple");
    if (importable("polars")) {
        names.append("polars.Series");
        names.append("polars.DataFrame");
    }
    return py::tuple(names);
}

[[nodiscard]] inline Format parse(const py::object& format) {
    if (format.is_none()) return Format::Auto;
    if (!py::isinstance<py::str>(format)) throw py::type_error("format must be None or a str");
    const auto name = format.cast<std::string>();
    if (name == "numpy") return Format::Numpy;
    if (name == "list") return Format::List;
    if (name == "tuple") return Format::Tuple;
    if (name == "polars" || name == "polars.Series") return Format::PolarsSeries;
    if (name == "polars.DataFrame") return Format::PolarsFrame;
    throw py::value_error("format must be None, 'numpy', 'list', 'tuple', 'polars.Series' or "
                          "'polars.DataFrame' (got '" + name + "')");
}

template <typename T, typename Fill>
void fill_released(T* out, std::size_t n, Fill& fill) {
    if (n == 0) return;
    py::gil_scoped_release release;
    fill(out, n);
}

// numpy's own allocator is frequently 16-mod-32 aligned; allocating 64-byte
// aligned lets a producer's aligned (e.g. non-temporal) store path engage
// deterministically.
template <typename T, typename Fill>
[[nodiscard]] py::array_t<T> to_numpy(py::ssize_t n, Fill& fill) {
    require("numpy", "numpy");
    const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(T);
    void* mem = ::operator new(bytes == 0 ? sizeof(T) : bytes, std::align_val_t{64});
    py::capsule owner(mem, [](void* p) { ::operator delete(p, std::align_val_t{64}); });
    py::array_t<T> out({n}, {static_cast<py::ssize_t>(sizeof(T))}, static_cast<T*>(mem), owner);
    fill_released(static_cast<T*>(mem), static_cast<std::size_t>(n), fill);
    return out;
}

template <typename Seq, typename T, typename Fill>
[[nodiscard]] Seq to_sequence(py::ssize_t n, Fill& fill) {
    std::vector<T> buf(static_cast<std::size_t>(n));
    fill_released(buf.data(), buf.size(), fill);
    Seq seq(n);
    for (std::size_t i = 0; i < buf.size(); ++i) seq[i] = py::cast(buf[i]);
    return seq;
}

// polars infers Int64 from Python ints, which overflows past 2**63, so the
// dtype is always given; numpy, when present, hands over one buffer instead
// of n objects.
template <typename T, typename Fill>
[[nodiscard]] py::object to_polars(py::ssize_t n, Fill& fill, bool frame, const char* name) {
    static_assert(std::is_same_v<T, double> || std::is_same_v<T, std::uint64_t>,
                  "add the polars dtype for this element type");
    py::module_ pl = require("polars", frame ? "polars.DataFrame" : "polars.Series");
    py::object values = importable("numpy") ? py::object(to_numpy<T>(n, fill))
                                            : py::object(to_sequence<py::list, T>(n, fill));
    py::object dtype = pl.attr(std::is_same_v<T, double> ? "Float64" : "UInt64");
    py::object series = pl.attr("Series")(name, values, py::arg("dtype") = dtype);
    return frame ? series.attr("to_frame")() : series;
}

// n values from `fill` as `format` (see parse); `name` labels a polars column.
template <typename T, typename Fill>
[[nodiscard]] py::object emit(py::ssize_t n, const py::object& format, Fill&& fill,
                              const char* name = "value") {
    if (n < 0) throw py::value_error("n must be non-negative");
    Format f = parse(format);
    if (f == Format::Auto) f = importable("numpy") ? Format::Numpy : Format::List;
    switch (f) {
        case Format::Numpy: return to_numpy<T>(n, fill);
        case Format::List: return to_sequence<py::list, T>(n, fill);
        case Format::Tuple: return to_sequence<py::tuple, T>(n, fill);
        case Format::PolarsSeries: return to_polars<T>(n, fill, false, name);
        case Format::PolarsFrame: return to_polars<T>(n, fill, true, name);
        case Format::Auto: break;
    }
    throw py::value_error("unreachable output format");
}

// An existing numpy buffer to write into. Never a converted copy (the writes
// would be silently lost), so instead of pybind's converting casters the
// buffer is validated explicitly: ndarray, exact dtype, native byte order,
// C-contiguous, aligned, writable.
template <typename T>
[[nodiscard]] py::array writable_buffer(const py::object& out, const char* dtype_name) {
    if (!py::isinstance<py::array>(out)) throw py::type_error("out must be a numpy.ndarray");
    auto arr = py::reinterpret_borrow<py::array>(out);
    // kind + itemsize rather than num(): the equal-comparing 'Q' and 'L'
    // spellings of uint64 carry different nums on LP64 platforms.
    if (arr.dtype().kind() != py::dtype::of<T>().kind() ||
        arr.itemsize() != static_cast<py::ssize_t>(sizeof(T))) {
        throw py::type_error(std::string("out must have dtype ") + dtype_name);
    }
    if (arr.dtype().byteorder() == '>') throw py::type_error("out must be native byte order");
    if ((arr.flags() & py::array::c_style) == 0) throw py::type_error("out must be C-contiguous");
    constexpr int kNpyArrayAligned = 0x0100;  // NPY_ARRAY_ALIGNED
    if ((arr.flags() & kNpyArrayAligned) == 0) throw py::type_error("out must be element-aligned");
    if (!arr.writeable()) throw py::value_error("out must be writable");
    return arr;
}

template <typename T, typename Fill>
void fill_buffer(const py::object& out, const char* dtype_name, Fill&& fill) {
    auto arr = writable_buffer<T>(out, dtype_name);
    fill_released(static_cast<T*>(arr.mutable_data()), static_cast<std::size_t>(arr.size()), fill);
}

}  // namespace pygim::output
