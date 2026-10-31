// similarity/adapter/bindings.cpp — registers pygim._similarity (re-exported
// by pygim/similarity.py, which loads pyarrow first). Logic lives in
// ../core.h (scores, the corpus, the sweep), ../python_source.h and
// ../cpp_source.h (the frontends), ../sweeps.h (a query's plan and its sweep on
// every core), stream.h (stream_pairs), tables.h (the Arrow results), index_state.h (what an
// Index and its streams share) and adapter.h (the Index); this file only binds.

#include <optional>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "adapter.h"
#include "similarity_languages.gen.h"   // GENERATED: using Languages = language_list<languages::...>

namespace py = pybind11;
using namespace pygim::similarity;

namespace {

using Index = basic_index<Languages>;

template <class L>
void record(py::dict& out) {
    py::tuple exts(L::info.exts().size());
    std::size_t at = 0;
    for (const std::string_view ext : L::info.exts()) exts[at++] = py::str(std::string(ext));
    py::dict d;
    d["doc"] = std::string(L::info.doc());
    d["exts"] = exts;
    out[py::str(std::string(L::info.name()))] = d;
}

template <class... Ls>
py::dict languages_record(language_list<Ls...>*) {
    py::dict out;
    (record<Ls>(out), ...);
    return out;
}

}  // namespace

PYBIND11_MODULE(_similarity, m) {
    m.doc() = "The compiled half of pygim.similarity: import that module instead (it loads Arrow first).";

    pygim::arrow_stream::bind(m, "_ArrowStream", "A result table or stream behind the Arrow PyCapsule protocol.");

    m.attr("LANGUAGES") = languages_record(static_cast<Languages*>(nullptr));

    m.def("language_of", [](const py::object& path) -> std::optional<std::string> {
        const std::string p = pygim::pathlike::utf8_fspath(path);
        const auto name = Languages::owner_of(suffix_of(p));
        return name ? std::optional<std::string>(std::string(*name)) : std::nullopt;
    }, py::arg("path"), R"doc(The language owning `path`'s extension ("python", "cpp"), or None.)doc");

    py::class_<Index>(m, "Index", R"doc(The units of one language and their all-pairs comparison.

Index(language) -- language is a key of LANGUAGES ("python", "cpp").)doc")
        .def(py::init<std::string_view>(), py::arg("language"),
             "An empty index of `language` (a key of LANGUAGES); ValueError naming the known languages otherwise.")
        .def_property_readonly("language", &Index::language, "The language this index holds.")
        .def("add", [](Index& ix, std::string_view source, std::string_view origin, const std::optional<std::string>& module) {
                 ix.add(source, origin, module ? std::string_view(*module) : std::string_view());
             }, py::arg("source"), py::arg("origin") = "<string>", py::arg("module") = py::none(),
             R"doc(Parse `source` (str) and add its units, recorded as coming from `origin`.

`module` is the source's dotted module name ("pkg.sub.m"): it qualifies the
module-level names and resolves relative imports, so the same function
reached through different imports compares equal; relative imports start from
its parent, or from the module itself when `origin` names an __init__ file.
Without it, names resolve as far as the source itself says. A source the
frontend cannot read raises ValueError "<origin>:<line>: ...".)doc")
        .def("add_file", &Index::add_file, py::arg("path"), py::arg("module") = py::none(),
             R"doc(Read the file at `path` and add its units.

The module name is `module`, or else derived the way the import system would:
the file's stem under every parent directory holding an __init__.py. ValueError
when the file's extension is not this index's language.)doc")
        .def("add_files", &Index::add_files, py::arg("files"), py::kw_only(), py::arg("threads") = py::none(),
             R"doc(Read, parse and fingerprint many files at once, then add their units in the order given.

The work runs in C++ on worker threads with the GIL released, and Ctrl-C stops it (KeyboardInterrupt);
the units' ids are what add_file() in a loop would give them. ValueError, before anything is read,
when a file's extension is not this index's language. Returns (path, message) for each file left
out: not a source the language's frontend can read, refused by the OS, or no regular file.
threads: how many work at once; None grows them while more are worth it.)doc")
        .def("units", &Index::units, py::arg("kind") = py::none(),
             R"doc(The units in insertion order, optionally only kind="function" or "class",
as a pyarrow.Table:

  id u32 | kind dictionary | qualname str | origin dictionary | line u32 |
  owner u32 (a method's class id; null otherwise) | nodes u32 | weight u64)doc")
        .def("pairs", &Index::pairs, py::arg("kind") = "function", py::arg("threshold") = 0.0,
             py::arg("min_size") = 0, py::arg("limit") = 0, py::arg("batch_size") = tables::default_batch_rows,
             py::arg("columns") = py::none(),
             R"doc(Every pair of units of `kind` scoring at least `threshold`, RANKED best first,
as a pyarrow.Table.

Units of fewer than `min_size` syntax-tree nodes (a class counts its methods')
are left out: trivial accessors are identical without being duplicates. With
`limit`, only the best `limit` pairs are kept, and the sweep skips every
comparison that could not make the cut. The table is made of batches of
`batch_size` rows.

Columns, one row per pair:

  a, b u32 (unit ids; join units().id) | a_qualname, b_qualname dictionary |
  a_origin, b_origin dictionary | a_line, b_line u32 |
  score f64 in [0, 1] | shared, total u64 (score == 2 * shared / total,
  exactly) | exact bool (identical up to local names)

`columns` selects and orders the columns (default: all). ["a", "b", "score"]
is 16 bytes a row instead of 56: names, files and lines join from units() by
id. Ranking holds every pair in memory; for results too large for that, use
stream_pairs(). Sweep, sort and table building run on every core with the GIL
released.)doc")
        .def("stream_pairs", &Index::stream_pairs, py::arg("kind") = "function", py::arg("threshold") = 0.0,
             py::arg("min_size") = 0, py::arg("batch_size") = tables::default_batch_rows, py::arg("columns") = py::none(),
             R"doc(The pairs of pairs() (without a limit), UNRANKED, as a pyarrow.RecordBatchReader.

Batches of exactly `batch_size` rows (the last shorter) are swept on every
core while the reader is consumed, in a fixed order: the same batches every
run, for any thread count. Memory stays bounded whatever the result size — by
`batch_size` and the cores: a few batches being written at threshold 0, two
blocks a worker at a threshold — so 100M+ pairs can be written to Parquet,
aggregated or filtered as they arrive.
Same columns, and `columns` selection, as pairs(). Adding to the index while a
stream is open fails the stream (ArrowInvalid).)doc")
        .def("stats", &Index::stats,
             "Counts and the corpus's exact bytes: units, functions, classes, subtrees, bytes (the units, their fingerprints and names;\n"
             "not the columns and orders a query keeps beside them).")
        .def("__len__", &Index::size, "How many units the index holds: functions, methods and classes.")
        .def("__repr__", &Index::repr, "similarity.Index('python', units=9).");
}
