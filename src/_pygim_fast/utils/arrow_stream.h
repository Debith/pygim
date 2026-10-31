#pragma once
// utils/arrow_stream.h — handing an Arrow RecordBatch built in C++ to Python.
//
// ADAPTER layer (Arrow + pybind11). One exporter for every extension that
// produces Arrow data (datagen, similarity): an object implementing the Arrow
// PyCapsule protocol, `__arrow_c_stream__`, so pyarrow, polars and anything
// else speaking the protocol consume the batch without a copy and without this
// extension linking any Python-side Arrow API.
//
//     auto batch = ...;                                            // built with arrow builders
//     return to_table(exporter::of(batch));                        // a pyarrow.Table, zero-copy
//     return to_reader(exporter::streaming(reader));               // batches made as they are read
//
// Each extension registers the class itself with `bind()`; it is module-local,
// so two extensions loaded in one process never clash over the registration.
//
// Lifetime: the exporter shares ownership of its batches or reader; the
// stream it exports keeps them alive until its consumer releases it.

#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include <arrow/api.h>
#include <arrow/c/bridge.h>
#include <pybind11/gil_safe_call_once.h>
#include <pybind11/pybind11.h>

namespace pygim::arrow_stream {

namespace py = pybind11;

/// Arrow data behind the Arrow PyCapsule protocol: either finished batches
/// (exportable any number of times) or a live RecordBatchReader that
/// produces them on demand (exportable once — a stream is consumed). Made by
/// of() or streaming(), moved, never copied: a copy of a live one would be two
/// readers of one stream.
class exporter {
public:
    /// One batch.
    [[nodiscard]] static exporter of(std::shared_ptr<arrow::RecordBatch> batch) {
        std::shared_ptr<arrow::Schema> schema = batch->schema();
        std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
        batches.push_back(std::move(batch));
        return exporter(std::move(schema), std::move(batches), nullptr);
    }
    /// Batches of one schema (none: an empty table that keeps its schema).
    [[nodiscard]] static exporter of(std::shared_ptr<arrow::Schema> schema,
                                     std::vector<std::shared_ptr<arrow::RecordBatch>> batches) {
        return exporter(std::move(schema), std::move(batches), nullptr);
    }
    /// A reader, streamed as its consumer pulls.
    [[nodiscard]] static exporter streaming(std::shared_ptr<arrow::RecordBatchReader> reader) {
        std::shared_ptr<arrow::Schema> schema = reader->schema();
        return exporter(std::move(schema), {}, std::move(reader));
    }

    exporter(exporter&&) noexcept = default;
    exporter& operator=(exporter&&) noexcept = default;
    exporter(const exporter&) = delete;
    exporter& operator=(const exporter&) = delete;

    /// `__arrow_c_stream__`: an ArrowArrayStream capsule (`requested_schema`
    /// is not honoured: the data has one schema). A streaming exporter can
    /// be exported once; a second export is a RuntimeError.
    /// Cost: O(1); no data is copied.
    [[nodiscard]] py::capsule arrow_c_stream(const py::object& /*requested_schema*/ = py::none()) {
        std::shared_ptr<arrow::RecordBatchReader> r;
        if (m_live) {
            if (!m_reader) throw std::runtime_error("this Arrow stream was already consumed");
            r = std::move(m_reader);
        } else {
            auto made = arrow::RecordBatchReader::Make(m_batches, m_schema);
            if (!made.ok()) throw std::runtime_error(made.status().ToString());
            r = std::move(*made);
        }
        auto* stream = new ArrowArrayStream;
        std::memset(stream, 0, sizeof(*stream));
        const arrow::Status st = arrow::ExportRecordBatchReader(std::move(r), stream);
        if (!st.ok()) {
            delete stream;
            throw std::runtime_error(st.ToString());
        }
        return py::capsule(static_cast<void*>(stream), "arrow_array_stream", [](void* p) {
            auto* s = static_cast<ArrowArrayStream*>(p);
            if (s->release) s->release(s);
            delete s;
        });
    }

private:
    exporter(std::shared_ptr<arrow::Schema> schema, std::vector<std::shared_ptr<arrow::RecordBatch>> batches,
             std::shared_ptr<arrow::RecordBatchReader> reader)
        : m_schema(std::move(schema)), m_batches(std::move(batches)), m_reader(std::move(reader)), m_live(m_reader != nullptr) {}

    std::shared_ptr<arrow::Schema> m_schema;
    std::vector<std::shared_ptr<arrow::RecordBatch>> m_batches;
    std::shared_ptr<arrow::RecordBatchReader> m_reader;
    bool m_live;   // a reader, not batches: one export only
};

/// Registers `exporter` in module `m` as `name` (module-local).
inline void bind(py::module_& m, const char* name, const char* doc) {
    py::class_<exporter>(m, name, py::module_local(), doc)
        .def("__arrow_c_stream__", &exporter::arrow_c_stream, py::arg("requested_schema") = py::none(),
             "Export as an Arrow C stream (PyCapsule protocol).");
}

namespace detail {

// pyarrow's own constructors, looked up once — not per result, as each call imported pyarrow
// before (2026-10-29). There is no C-level way to a pyarrow.Table but libarrow_python's
// wrap_table, which this file stays clear of (see the file comment); pygim's wrappers have
// imported pyarrow by then, so the lookup finds it loaded.
[[nodiscard]] inline py::object& pyarrow_table() {
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<py::object> found;
    return found.call_once_and_store_result([] { return py::module_::import("pyarrow").attr("table"); }).get_stored();
}
[[nodiscard]] inline py::object& pyarrow_reader_from_stream() {
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<py::object> found;
    return found.call_once_and_store_result([] { return py::module_::import("pyarrow").attr("RecordBatchReader").attr("from_stream"); })
        .get_stored();
}

}  // namespace detail

/// The exporter's batches as a pyarrow.Table, through the PyCapsule
/// protocol (no copy). Needs the GIL; `exporter` must be bound in the
/// calling module.
[[nodiscard]] inline py::object to_table(exporter e) {
    return detail::pyarrow_table()(py::cast(std::move(e)));
}

/// The exporter as a pyarrow.RecordBatchReader (batches pulled on demand).
[[nodiscard]] inline py::object to_reader(exporter e) {
    return detail::pyarrow_reader_from_stream()(py::cast(std::move(e)));
}

}  // namespace pygim::arrow_stream
