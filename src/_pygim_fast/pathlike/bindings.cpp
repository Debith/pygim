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

#include <limits>
#include <optional>
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

namespace {

// None or "" -> auto (Engine::Unknown); a known name -> that engine; else throws.
Engine engine_from_arg(const std::optional<std::string>& name) {
    if (!name || name->empty()) return Engine::Unknown;
    const Engine e = engine_from_name(*name);
    if (e == Engine::Unknown) {
        throw std::invalid_argument("unknown engine: '" + *name +
                                    "' (known: yaml, json, toml)");
    }
    return e;
}

// key_cache: -1 -> unbounded, 0 -> off, N -> at most N distinct interned keys.
std::size_t cache_capacity_from_arg(py::ssize_t key_cache) {
    if (key_cache < 0) return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(key_cache);
}

// The path as a Python str, decoded from the NATIVE representation.
py::str fspath_str(const file& f) {
#ifdef _WIN32
    return py::cast(f.path().native());   // std::wstring -> str, lossless
#else
    const std::string& n = f.path().native();
    return py::reinterpret_steal<py::str>(
        PyUnicode_DecodeFSDefaultAndSize(n.data(), static_cast<py::ssize_t>(n.size())));
#endif
}

}  // namespace

PYBIND11_MODULE(pathlike, m) {
    m.doc() = "path(): an os.PathLike that reads & decodes itself with the optimal engine.";

    py::class_<file>(m, "file", R"doc(
A filesystem path that knows how to read and decode itself.

Never constructed directly — use ``pygim.path(...)``. The decoding engine is chosen
at compile time from the extension (``.yaml``/``.yml`` -> YAML via rapidyaml,
``.json`` -> JSON via simdjson, ``.toml`` -> TOML via toml++); pin one with
``engine=`` at construction, or
override per call with ``read(engine=...)``. Implements ``os.PathLike``, so it
drops into ``open()``, ``Path()``, etc.
)doc")
        .def(py::init([](fs::path p, const std::optional<std::string>& engine) {
                 return file(std::move(p), engine_from_arg(engine));
             }),
             py::arg("path"), py::arg("engine") = py::none())
        .def_property_readonly(
            "engine",
            [](const file& f) -> py::object {
                const Engine e = f.pinned_engine();
                if (e == Engine::Unknown) return py::none();
                return py::str(std::string(engine_label(e)));
            },
            "The engine pinned at construction ('yaml'/'json'/'toml'), or None for auto.")
        // Decode the NATIVE path representation (wstring on Windows — lossless;
        // bytes via the filesystem encoding on POSIX). fs::path::string() would
        // narrow through the ACP on Windows and can mangle non-ASCII paths.
        .def("__fspath__", [](const file& f) { return fspath_str(f); },
             "os.PathLike protocol: the plain path string.")
        .def("__repr__", &file::repr)
        .def("__str__", [](const file& f) { return fspath_str(f); })
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
             [](const file& f, const std::optional<std::string>& engine, py::ssize_t key_cache) {
                 const Engine named = engine_from_arg(engine);
                 return load(f, f.resolve_engine(named == Engine::Unknown
                                                     ? std::string_view{}
                                                     : engine_label(named)),
                             cache_capacity_from_arg(key_cache));
             },
             py::arg("engine") = py::none(), py::arg("key_cache") = 256,
             "Decode the file to native Python objects (I/O and parsing release "
             "the GIL). engine= overrides for this call; key_cache bounds the "
             "key-interning cache (0 off, -1 unbounded).")
        .def("write",
             [](const file& f, py::handle obj, const std::optional<std::string>& engine) {
                 const Engine named = engine_from_arg(engine);
                 write(f, obj, f.resolve_engine(named == Engine::Unknown
                                                    ? std::string_view{}
                                                    : engine_label(named)));
             },
             py::arg("obj"), py::arg("engine") = py::none(),
             "Serialise obj to this path with the resolved engine (yaml/json/"
             "toml). TOML requires a mapping root and supports datetimes; "
             "strings that would read back typed are quoted automatically, so "
             "write/read round-trips.")
        // -- directory traversal (results inherit the engine pin) --
        .def("iterdir", &file::iterdir, "The directory's children, sorted.")
        .def("glob", &file::glob, py::arg("pattern"),
             "Relative glob: * and ? within a component, ** across directories. "
             "Sorted and deduplicated; results inherit the engine pin.")
        .def("rglob", &file::rglob, py::arg("pattern"),
             "glob('**/' + pattern): the pattern anywhere under this directory.")
        .def("pathset",
             [](const file& f, const std::string& pattern) {
                 std::vector<std::string> paths;
                 for (const file& m : f.glob(pattern)) paths.push_back(m.fspath());
                 return py::module_::import("pygim.pathset").attr("PathSet")(paths);
             },
             py::arg("pattern") = std::string("*"),
             "The glob results as a pygim.pathset.PathSet, for set algebra and "
             "Filter queries.");

    m.def("path",
          [](fs::path p, const std::optional<std::string>& engine) {
              return file(std::move(p), engine_from_arg(engine));
          },
          py::arg("path"), py::arg("engine") = py::none(),
          "Wrap a path in a self-reading, self-decoding file(). Pass engine= to pin "
          "the decoder ('yaml'/'json'/'toml'); default resolves from the extension.");

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
