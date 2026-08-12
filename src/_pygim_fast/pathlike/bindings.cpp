// pathlike/bindings.cpp — the `pygim.pathlike` pybind11 module.
//
// Exposes `path(...)` -> file(), a std::filesystem::path that reads and decodes
// itself with the optimal engine for its extension (see core.h / adapter.h).
//
// This is the single translation unit that instantiates rapidyaml's definitions,
// so RYML_SINGLE_HDR_DEFINE_NOW is defined *before* the amalgamated header is first
// included; every other include of it (from adapter.h) is a guarded no-op.

#define RYML_SINGLE_HDR_DEFINE_NOW
#include "third_party/rapidyaml/ryml_all.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#define PYBIND11_HAS_FILESYSTEM_IS_OPTIONAL
#include <pybind11/stl/filesystem.h>

#include <string>

#include "core.h"
#include "adapter.h"

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;
using namespace pygim::pathlike;

// Runtime proof, alongside core.h's static_asserts, that the compile-time engine
// choice reaches Python correctly.
static_assert(engine_for_ext(".yaml") == Engine::Yaml, "yaml dispatch");
static_assert(engine_for_ext(".json") == Engine::Json, "json dispatch");

PYBIND11_MODULE(pathlike, m) {
    m.doc() = "path(): an os.PathLike that reads & decodes itself with the optimal engine.";

    py::class_<file>(m, "file", R"doc(
A filesystem path that knows how to read and decode itself.

Never constructed directly — use ``pygim.path(...)``. The decoding engine is chosen
at compile time from the extension (``.yaml``/``.yml`` -> YAML); pass ``engine=`` to
override. Implements ``os.PathLike``, so it drops into ``open()``, ``Path()``, etc.
)doc")
        .def(py::init<fs::path>(), py::arg("path"))
        .def("__fspath__", &file::fspath, "os.PathLike protocol: the plain path string.")
        .def("__repr__", &file::repr)
        .def("__str__", &file::fspath)
        .def("__eq__",
             [](const file& a, const file& b) { return a.path() == b.path(); },
             py::is_operator())
        .def("__hash__", [](const file& f) { return py::hash(py::cast(f.fspath())); })
        .def_property_readonly("uri", &file::uri, "The 'file://<path>' URI form.")
        // -- name components (case preserved, like pathlib) --
        .def_property_readonly("name", &file::name, "The final path component.")
        .def_property_readonly("stem", &file::stem, "The final component without its suffix.")
        .def_property_readonly("suffix", &file::suffix, "The final extension incl. dot ('.yaml').")
        .def_property_readonly("suffixes", &file::suffixes,
                               "All extensions of the final component ('.tar.gz' -> ['.tar','.gz']).")
        .def_property_readonly("parts", &file::parts, "The path components as a list.")
        // -- composition: path / "sub" / "file.yaml" --
        .def("__truediv__", [](const file& f, const fs::path& other) { return f.joined(other); },
             py::is_operator())
        .def("__rtruediv__", [](const file& f, const fs::path& other) { return f.rjoined(other); },
             py::is_operator())
        .def("joinpath",
             [](const file& f, const py::args& parts) {
                 file out = f;
                 for (const py::handle& part : parts) out = out.joined(part.cast<fs::path>());
                 return out;
             },
             "Append one or more path components, like pathlib's joinpath().")
        .def_property_readonly("parent", &file::parent, "The parent directory as a file().")
        .def_property_readonly("parents", &file::parents, "Ancestor directories, closest first.")
        // -- derived paths (return new file()s) --
        .def("with_suffix", &file::with_suffix, py::arg("suffix"),
             "A copy with the final suffix replaced.")
        .def("with_name", &file::with_name, py::arg("name"),
             "A copy with the final component replaced.")
        .def("with_stem", &file::with_stem, py::arg("stem"),
             "A copy with the stem replaced (suffix kept).")
        .def("absolute", &file::absolute, "An absolute copy (does not resolve symlinks/..).")
        .def("resolve", &file::resolve, "A canonical, absolute copy (resolves symlinks/..).")
        // -- filesystem status --
        .def("is_absolute", &file::is_absolute, "Whether the path is absolute.")
        .def("exists", &file::exists, "Whether the path exists on disk.")
        .def("is_file", &file::is_file, "Whether it is a regular file.")
        .def("is_dir", &file::is_dir, "Whether it is a directory.")
        .def("is_symlink", &file::is_symlink, "Whether it is a symbolic link.")
        .def("size", &file::size, "File size in bytes (raises if it does not exist).")
        .def("read_bytes", [](const file& f) { return py::bytes(f.read_bytes()); },
             "The raw file bytes, undecoded.")
        .def("read",
             [](const file& f, const std::string& engine) {
                 return load(f, f.resolve_engine(engine));
             },
             py::arg("engine") = std::string(),
             "Decode the file to native Python objects using the optimal engine "
             "(or the named engine=).");

    m.def("path", [](fs::path p) { return file(std::move(p)); }, py::arg("path"),
          "Wrap a path in a self-reading, self-decoding file().");

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
