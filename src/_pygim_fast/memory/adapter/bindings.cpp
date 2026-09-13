// memory/adapter/bindings.cpp — registers pygim.memory.
//
// rapidyaml is header-only and must be defined in exactly one translation
// unit per extension, before anything includes it.
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "../../pathlike/adapter/third_party/rapidyaml/ryml_all.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "memory_adapter.h"

namespace py = pybind11;
using pygim::memory::adapter::Memory;

PYBIND11_MODULE(memory, m) {
    m.doc() = R"doc(Problem-space memory: retrieval driven by the kind of problem being solved.

A repository is a folder of plain files — a vocabulary, content objects and a
hash-chained audit log — shared through git. `Memory(root)` opens one; every
operation returns a plain dict, and a refused write is a result with
"refused" and "facts", not an exception.

    Memory.init(".memory")
    m = Memory(".memory")
    m.read(hard=["domain=dnd", "artifact=spell", "task=design"])
)doc";

    py::register_exception<pygim::memory::load_error>(m, "VocabularyError", PyExc_ValueError);

    m.def("digest", [](const py::bytes& data) { return pygim::memory::digest::of(std::string(data)).hex(); }, py::arg("data"),
          "The 128-bit digest pygim.memory names content by, as 32 hex characters — what a source's version\n"
          "and a locator's passage are recorded as.");

    py::class_<Memory>(m, "Memory")
        .def(py::init<const std::string&>(), py::arg("root"),
             "Opens the repository at `root`: loads the vocabulary (raising VocabularyError with every\n"
             "failure by file and line), replays the audit log, and joins divergent histories.")
        .def_static("init", &Memory::init, py::arg("root"),
                    "Creates a repository at `root` with the base vocabulary (taxonomy v0).")
        .def_property_readonly("root", &Memory::root)
        .def_property_readonly("version", &Memory::version, "How many rows the head's history holds.")
        .def_property_readonly("head", &Memory::head, "The id of the head row — what a receipt pins.")
        .def("refresh", &Memory::refresh, "Folds in rows other processes committed.")
        .def("session", &Memory::session, "Opens a session: its number, where the store stands, reviews and proposals.")
        .def("vocabulary", &Memory::vocabulary, "Every live dimension and value with its codebook entry.")
        .def("read", &Memory::read, py::arg("hard"), py::arg("soft") = std::vector<std::string>{}, py::kw_only(),
             py::arg("max") = 8u, py::arg("budget") = 0u, py::arg("session") = 0ull,
             "Retrieves the context for a problem space: the procedure first, then ranked memories.")
        .def("remember", &Memory::remember, py::kw_only(), py::arg("title"), py::arg("text"), py::arg("tags"),
             py::arg("reason") = "", py::arg("supersedes") = std::vector<std::string>{},
             py::arg("generalises") = std::vector<std::string>{},
             py::arg("seen") = std::vector<std::string>{}, py::arg("cites") = std::vector<std::string>{},
             py::arg("proposals") = py::list(), py::arg("session") = 0ull, py::arg("turn") = 0u,
             py::arg("author") = "agent",
             "Writes a memory, after the four checks: closed vocabulary, no unread write, head only,\n"
             "identical content. With `generalises`, it states the pattern two or more heads share: they\n"
             "stay heads, and it must cover them on every hard dimension.")
        .def("review", &Memory::review, py::arg("session"),
             "What one session wrote, in order, with what already generalises each memory — where a\n"
             "consolidation starts.")
        .def("merge", &Memory::merge, py::arg("memories"), py::kw_only(), py::arg("title"), py::arg("text"),
             py::arg("reason"), py::arg("tags") = std::vector<std::string>{}, py::arg("session") = 0ull,
             py::arg("author") = "agent", "Joins heads that say one thing into one memory that supersedes them.")
        .def("learn", &Memory::learn, py::arg("memory"), py::kw_only(), py::arg("tag") = "", py::arg("reason") = "",
             py::arg("session") = 0ull, "Reports that a memory helped; with a tag, counts toward promoting it.")
        .def("link", &Memory::link, py::arg("memory"), py::arg("tag"), py::kw_only(), py::arg("reason"),
             py::arg("author") = "human")
        .def("unlink", &Memory::unlink, py::arg("memory"), py::arg("tag"), py::kw_only(), py::arg("reason"),
             py::arg("author") = "human")
        .def("retire", &Memory::retire, py::arg("memory"), py::kw_only(), py::arg("reason"), py::arg("author") = "human")
        .def("show", &Memory::show, py::arg("memory"))
        .def("proposals", &Memory::proposals, "Concepts the vocabulary lacks, folded, waiting for a human.")
        .def("ingest", &Memory::ingest, py::arg("path"), "Ingests a hand-written corpus file, reconciled by slug and digest.")
        .def("receipts", &Memory::receipts)
        .def("rerun", &Memory::rerun, py::arg("receipt"),
             "Reruns a receipt against the snapshot and vocabulary it pinned; `same` says whether it matched.");
}
