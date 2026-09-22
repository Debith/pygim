// enact/adapter/bindings.cpp — registers pygim.enact.
//
// rapidyaml is header-only and must be defined in exactly one translation
// unit per extension, before anything includes it.
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "../../pathlike/adapter/third_party/rapidyaml/ryml_all.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "enact_adapter.h"

namespace py = pybind11;
using pygim::enact::adapter::Enact;

PYBIND11_MODULE(enact, m) {
    m.doc() = R"doc(ENACT: knowledge retrieved by the kind of problem being solved.

A repository is a folder of plain files — a vocabulary, content objects and a
hash-chained audit log — shared through git. `Enact(root)` opens one; every
operation returns a plain dict, and a refused write is a result with
"refused" and "facts", not an exception.

    Enact.init(".enact")
    m = Enact(".enact")
    m.read(hard=["domain=dnd", "artifact=spell", "task=design"])
)doc";

    py::register_exception<pygim::enact::load_error>(m, "VocabularyError", PyExc_ValueError);

    m.def("digest", [](const py::bytes& data) { return pygim::enact::digest::of(std::string(data)).hex(); }, py::arg("data"),
          "The 128-bit digest pygim.enact names content by, as 32 hex characters — what a source's version\n"
          "and a locator's passage are recorded as.");

    py::class_<Enact>(m, "Enact")
        .def(py::init<const std::string&>(), py::arg("root"),
             "Opens the repository at `root`: loads the vocabulary (raising VocabularyError with every\n"
             "failure by file and line), replays the audit log, and joins divergent histories.")
        .def_static("init", &Enact::init, py::arg("root"),
                    "Creates a repository at `root` with the base vocabulary (taxonomy v0).")
        .def_property_readonly("root", &Enact::root)
        .def_property_readonly("version", &Enact::version, "How many rows the head's history holds.")
        .def_property_readonly("head", &Enact::head, "The id of the head row — what a receipt pins.")
        .def_property_readonly("taxonomy", &Enact::taxonomy_version, "The live vocabulary's version, as 32 hex characters.")
        .def("refresh", &Enact::refresh, "Folds in rows other processes committed.")
        .def("session", &Enact::session, "Opens a session: its number, where the store stands, reviews and proposals.")
        .def("vocabulary", &Enact::vocabulary, "Every live dimension and value with its codebook entry.")
        .def("read", &Enact::read, py::arg("hard"), py::arg("soft") = std::vector<std::string>{}, py::kw_only(),
             py::arg("max") = 8u, py::arg("budget") = 0u, py::arg("term") = "", py::arg("session") = 0ull,
             "Retrieves the context for a problem space: the procedure first, then ranked memories, with\n"
             "what the candidates carry (`facets`) and cite (`coverage`). `term` keeps only candidates whose\n"
             "title or text contains it.")
        .def("remember", &Enact::remember, py::kw_only(), py::arg("title"), py::arg("text"), py::arg("tags"),
             py::arg("reason") = "", py::arg("supersedes") = std::vector<std::string>{},
             py::arg("generalises") = std::vector<std::string>{},
             py::arg("seen") = std::vector<std::string>{}, py::arg("cites") = std::vector<std::string>{},
             py::arg("proposals") = py::list(), py::arg("session") = 0ull, py::arg("turn") = 0u,
             py::arg("author") = "agent", py::arg("origin") = "written",
             "Writes a memory, after the four checks: closed vocabulary, no unread write, head only,\n"
             "identical content. With `generalises`, it states the pattern two or more heads share: they\n"
             "stay heads, and it must cover them on every hard dimension. `origin=\"seed\"` marks a store\n"
             "being seeded from existing documents, which has nothing to have read: no unread check.")
        .def("waiting_acceptance", &Enact::waiting_acceptance,
             "Every generalisation waiting for a person, with its text and the memories it would fold.")
        .def("accept", &Enact::accept, py::arg("memory"), py::kw_only(), py::arg("reason") = "", py::arg("author") = "human",
             "A human accepts a generalisation: from the next read its instances fold under it. Returns the\n"
             "refreshed report's path as `report`.")
        .def("lessons", &Enact::lessons, py::arg("session"), py::arg("text"), py::kw_only(), py::arg("author") = "agent",
             "Records what a consolidation learnt and publishes it in reviews/session-<n>.md, beside the\n"
             "generalisations waiting for acceptance, the cases left, and the proposals raised.")
        .def("heads", &Enact::heads, py::arg("tags"),
             "Every head carrying any of `tags`, without recording a read — standing knowledge such as\n"
             "kind=preference, handed to every session without touching usage counters.")
        .def("review", &Enact::review, py::arg("session"),
             "What one session wrote, in order, with what already generalises each memory — where a\n"
             "consolidation starts.")
        .def("merge", &Enact::merge, py::arg("memories"), py::kw_only(), py::arg("title"), py::arg("text"),
             py::arg("reason"), py::arg("tags") = std::vector<std::string>{}, py::arg("session") = 0ull,
             py::arg("author") = "agent", "Joins heads that say one thing into one memory that supersedes them.")
        .def("learn", &Enact::learn, py::arg("memory"), py::kw_only(), py::arg("tag") = "", py::arg("reason") = "",
             py::arg("session") = 0ull, py::arg("verdict") = "useful",
             "Reports what a retrieved memory turned out to be worth: `useful` (with a tag, counts toward\n"
             "promoting it), `not_needed`, or `misleading`. Only usefulness promotes.")
        .def("link", &Enact::link, py::arg("memory"), py::arg("tag"), py::kw_only(), py::arg("reason"),
             py::arg("author") = "human")
        .def("unlink", &Enact::unlink, py::arg("memory"), py::arg("tag"), py::kw_only(), py::arg("reason"),
             py::arg("author") = "human")
        .def("cite", &Enact::cite, py::arg("memory"), py::arg("locator"), py::kw_only(), py::arg("reason"),
             py::arg("author") = "human",
             "Adds a locator to a memory's citations, without superseding it — a precision fix costs a\n"
             "row, not a new memory and a new number.")
        .def("uncite", &Enact::uncite, py::arg("memory"), py::arg("locator"), py::kw_only(), py::arg("reason"),
             py::arg("author") = "human", "Takes a locator off a memory's citations.")
        .def("retire", &Enact::retire, py::arg("memory"), py::kw_only(), py::arg("reason"), py::arg("author") = "human")
        .def("post", &Enact::post, py::arg("text"), py::kw_only(), py::arg("kind") = "comment", py::arg("to") = "",
             py::arg("about") = "", py::arg("reply_to") = "", py::arg("resolves") = "", py::arg("session") = 0ull,
             py::arg("author") = "agent",
             "Leaves a message for whoever works in this store next: feedback, a request or a comment.\n"
             "`resolves` closes an earlier message, so a thread is never closed silently.")
        .def("mailbox", &Enact::mailbox, py::kw_only(), py::arg("all") = false, py::arg("mine") = "",
             "The messages left here: what is still open, oldest first, or everything with all=True.")
        .def("show", &Enact::show, py::arg("memory"))
        .def("proposals", &Enact::proposals, "Concepts the vocabulary lacks, folded, waiting for a human.")
        .def("ingest", &Enact::ingest, py::arg("path"), "Ingests a hand-written corpus file, reconciled by slug and digest.")
        .def("sources", &Enact::sources, "Every vocabulary value that cites a source, with its full locator.")
        .def("receipts", &Enact::receipts)
        .def("rerun", &Enact::rerun, py::arg("receipt"),
             "Reruns a receipt against the snapshot and vocabulary it pinned; `same` says whether it matched.");
}
