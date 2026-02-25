#pragma once
// BCP Arrow Import: pybind11-dependent conversion of Python Arrow objects to
// C++ RecordBatchReader.  This header is ONLY included from pybind11 binding
// code (mssql_strategy.cpp, persist_arrow.cpp) — never from pure C++ strategy
// internals.

#include <memory>
#include <string>
#include <stdexcept>

#include <pybind11/pybind11.h>

#include <arrow/record_batch.h>
#include <arrow/buffer.h>
#include <arrow/ipc/api.h>
#include <arrow/io/api.h>

#if __has_include(<arrow/c/bridge.h>)
#include <arrow/c/bridge.h>
#define PYGIM_HAVE_ARROW_C_BRIDGE 1
#else
#define PYGIM_HAVE_ARROW_C_BRIDGE 0
#endif

namespace pygim::bcp {
namespace py = pybind11;

// ── Result of importing a Python Arrow payload ──────────────────────────────

struct ArrowImportResult {
    std::shared_ptr<arrow::RecordBatchReader> reader;
    std::string mode;   // "arrow_c_stream_capsule" | "arrow_c_stream_exporter" | "arrow_ipc_file" | "arrow_ipc_stream"
};

// ── Adapter: wrap RecordBatchFileReader as a streaming RecordBatchReader ─────

class FileBatchReaderAdapter : public arrow::RecordBatchReader {
    std::shared_ptr<arrow::ipc::RecordBatchFileReader> m_file;
    int m_index{0};
public:
    explicit FileBatchReaderAdapter(std::shared_ptr<arrow::ipc::RecordBatchFileReader> file)
        : m_file(std::move(file)) {}

    std::shared_ptr<arrow::Schema> schema() const override {
        return m_file->schema();
    }
    arrow::Status ReadNext(std::shared_ptr<arrow::RecordBatch>* out) override {
        if (m_index >= m_file->num_record_batches()) {
            *out = nullptr;
            return arrow::Status::OK();
        }
        auto result = m_file->ReadRecordBatch(m_index++);
        if (!result.ok()) return result.status();
        *out = result.ValueOrDie();
        return arrow::Status::OK();
    }
};

// ── C-stream import (capsule or _export_to_c) ──────────────────────────────

#if PYGIM_HAVE_ARROW_C_BRIDGE

inline std::shared_ptr<arrow::RecordBatchReader>
import_c_stream(const py::object& payload, bool is_capsule) {
    ArrowArrayStream exported{};
    ArrowArrayStream* stream = nullptr;

    if (is_capsule) {
        stream = static_cast<ArrowArrayStream*>(
            PyCapsule_GetPointer(payload.ptr(), "arrow_array_stream"));
        if (!stream)
            throw std::runtime_error("Invalid Arrow C stream capsule (null pointer)");
    } else {
        const auto ptr = reinterpret_cast<uintptr_t>(&exported);
        try { payload.attr("_export_to_c")(py::int_(ptr)); }
        catch (const py::error_already_set& e) {
            throw std::runtime_error(
                std::string("Failed exporting Arrow reader to C stream: ") + e.what());
        }
        stream = &exported;
    }

    auto result = arrow::ImportRecordBatchReader(stream);
    if (!result.ok()) {
        if (!is_capsule && exported.release) exported.release(&exported);
        throw std::runtime_error(
            "Failed to import Arrow C stream: " + result.status().ToString());
    }
    return result.ValueOrDie();
}

#endif // PYGIM_HAVE_ARROW_C_BRIDGE

// ── IPC bytes → RecordBatchReader ───────────────────────────────────────────

inline std::shared_ptr<arrow::Buffer>
extract_ipc_buffer(const py::object& payload) {
    py::bytes payload_bytes;
    if (py::isinstance<py::bytes>(payload)) {
        payload_bytes = py::reinterpret_borrow<py::bytes>(payload);
    } else {
        try { payload_bytes = py::bytes(payload); }
        catch (const py::error_already_set&) {
            throw std::runtime_error(
                "arrow_ipc_payload must be bytes or bytes-convertible for IPC parsing");
        }
    }
    auto data = payload_bytes.cast<std::string>();
    if (data.empty())
        throw std::runtime_error("Arrow BCP received empty IPC payload");
    return arrow::Buffer::FromString(std::move(data));
}

/// Try IPC file format, then fall back to stream format.  Returns a
/// RecordBatchReader in both cases (file reader is wrapped via adapter).
inline std::pair<std::shared_ptr<arrow::RecordBatchReader>, std::string>
import_ipc_reader(const py::object& payload) {
    auto buffer = extract_ipc_buffer(payload);
    auto io_reader = std::make_shared<arrow::io::BufferReader>(buffer);

    // Try IPC file format first
    auto file_result = arrow::ipc::RecordBatchFileReader::Open(io_reader);
    if (file_result.ok()) {
        auto adapter = std::make_shared<FileBatchReaderAdapter>(file_result.ValueOrDie());
        return {adapter, "arrow_ipc_file"};
    }

    // Fall back to IPC stream format
    io_reader = std::make_shared<arrow::io::BufferReader>(buffer);
    auto stream_result = arrow::ipc::RecordBatchStreamReader::Open(io_reader);
    if (!stream_result.ok()) {
        throw std::runtime_error(
            "Failed to open Arrow IPC as file or stream: file="
            + file_result.status().ToString()
            + ", stream=" + stream_result.status().ToString());
    }
    return {stream_result.ValueOrDie(), "arrow_ipc_stream"};
}

// ── Unified: detect input type and return a RecordBatchReader ───────────────

/// Convert a Python Arrow payload (C-stream capsule, _export_to_c object, or
/// IPC bytes) into a pure-C++ RecordBatchReader.  This is the ONLY function
/// the binding layer needs to call.
inline ArrowImportResult import_arrow_reader(const py::object& payload) {
    // 1. Capsule?
    if (PyCapsule_CheckExact(payload.ptr()) &&
        PyCapsule_IsValid(payload.ptr(), "arrow_array_stream")) {
#if PYGIM_HAVE_ARROW_C_BRIDGE
        return {import_c_stream(payload, /*is_capsule=*/true),
                "arrow_c_stream_capsule"};
#else
        throw std::runtime_error(
            "Arrow C stream capsule provided but bridge headers unavailable at build time");
#endif
    }

    // 2. Exporter with _export_to_c?
    if (py::hasattr(payload, "_export_to_c")) {
#if PYGIM_HAVE_ARROW_C_BRIDGE
        return {import_c_stream(payload, /*is_capsule=*/false),
                "arrow_c_stream_exporter"};
#else
        throw std::runtime_error(
            "Arrow C stream exporter provided but bridge headers unavailable at build time");
#endif
    }

    // 3. IPC bytes
    auto [reader, mode] = import_ipc_reader(payload);
    return {std::move(reader), std::move(mode)};
}

} // namespace pygim::bcp
