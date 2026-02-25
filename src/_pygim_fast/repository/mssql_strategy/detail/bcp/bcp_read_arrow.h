#pragma once
// BCP Arrow reader: dispatch Arrow C stream (capsule / _export_to_c) and IPC bytes
// to a user-supplied callback. Uses C++20 concepts to constrain the callback type.

#include <concepts>
#include <memory>
#include <string>

#include <pybind11/pybind11.h>

#include <arrow/table.h>
#include <arrow/buffer.h>
#include <arrow/ipc/api.h>
#include <arrow/io/api.h>

#if __has_include(<arrow/c/bridge.h>)
#include <arrow/c/bridge.h>
#define PYGIM_HAVE_ARROW_C_BRIDGE 1
#else
#define PYGIM_HAVE_ARROW_C_BRIDGE 0
#endif

// Forward-declare to avoid pulling in the full header here.
namespace pygim { class QuickTimer; }

namespace pygim::bcp {
namespace py = pybind11;

/// Concept: callable that accepts a shared_ptr<RecordBatch>.
template <typename F>
concept RecordBatchProcessor =
    std::invocable<F, const std::shared_ptr<arrow::RecordBatch>&>;

// ── Input type detection ────────────────────────────────────────────────────

struct ArrowInputType {
    std::string mode;
    bool is_capsule{false};
    bool is_exporter{false};
};

inline ArrowInputType detect_input_type(const py::object& payload) {
    if (PyCapsule_CheckExact(payload.ptr()) &&
        PyCapsule_IsValid(payload.ptr(), "arrow_array_stream"))
        return {"arrow_c_stream_capsule", true, false};

    if (py::hasattr(payload, "_export_to_c"))
        return {"arrow_c_stream_exporter", false, true};

    return {"arrow_ipc_bytes", false, false};
}

// ── C stream reading ────────────────────────────────────────────────────────

#if PYGIM_HAVE_ARROW_C_BRIDGE

template <RecordBatchProcessor ProcessFn>
void read_c_stream(const py::object& payload, bool is_capsule,
                   QuickTimer& timer, ProcessFn&& process) {
    ArrowArrayStream exported{};
    ArrowArrayStream* stream = nullptr;

    if (is_capsule) {
        stream = static_cast<ArrowArrayStream*>(
            PyCapsule_GetPointer(payload.ptr(), "arrow_array_stream"));
        if (!stream) throw std::runtime_error("Invalid Arrow C stream capsule (null pointer)");
    } else {
        const auto ptr = reinterpret_cast<uintptr_t>(&exported);
        try { payload.attr("_export_to_c")(py::int_(ptr)); }
        catch (const py::error_already_set& e) {
            throw std::runtime_error(
                std::string("Failed exporting Arrow reader to C stream: ") + e.what());
        }
        stream = &exported;
    }

    timer.start_sub_timer("reader_open", false);
    auto reader_result = arrow::ImportRecordBatchReader(stream);
    timer.stop_sub_timer("reader_open", false);

    if (!reader_result.ok()) {
        if (!is_capsule && exported.release) exported.release(&exported);
        throw std::runtime_error(
            "Failed to import Arrow C stream: " + reader_result.status().ToString());
    }

    auto reader = reader_result.ValueOrDie();
    while (true) {
        std::shared_ptr<arrow::RecordBatch> batch;
        auto st = reader->ReadNext(&batch);
        if (!st.ok()) throw std::runtime_error("Failed reading C stream batch: " + st.ToString());
        if (!batch) break;
        process(batch);
    }
}

#endif // PYGIM_HAVE_ARROW_C_BRIDGE

// ── IPC bytes reading ───────────────────────────────────────────────────────

inline std::shared_ptr<arrow::Buffer> extract_ipc_buffer(const py::object& payload) {
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
    if (data.empty()) throw std::runtime_error("Arrow BCP received empty IPC payload");
    return arrow::Buffer::FromString(std::move(data));
}

template <RecordBatchProcessor ProcessFn>
void read_ipc_bytes(const py::object& payload, QuickTimer& timer, ProcessFn&& process) {
    auto buffer = extract_ipc_buffer(payload);

    // Try file format first
    auto reader = std::make_shared<arrow::io::BufferReader>(buffer);
    timer.start_sub_timer("reader_open", false);
    auto file_result = arrow::ipc::RecordBatchFileReader::Open(reader);
    timer.stop_sub_timer("reader_open", false);

    if (file_result.ok()) {
        auto file_reader = file_result.ValueOrDie();
        for (int i = 0; i < file_reader->num_record_batches(); ++i) {
            auto batch = file_reader->ReadRecordBatch(i);
            if (!batch.ok())
                throw std::runtime_error("Failed to read file batch " + std::to_string(i)
                                         + ": " + batch.status().ToString());
            process(batch.ValueOrDie());
        }
        return;
    }

    // Fall back to stream format
    reader = std::make_shared<arrow::io::BufferReader>(buffer);
    timer.start_sub_timer("reader_open", false);
    auto stream_result = arrow::ipc::RecordBatchStreamReader::Open(reader);
    timer.stop_sub_timer("reader_open", false);

    if (!stream_result.ok()) {
        throw std::runtime_error(
            "Failed to open Arrow IPC as file or stream: file="
            + file_result.status().ToString()
            + ", stream=" + stream_result.status().ToString());
    }

    auto stream_reader = stream_result.ValueOrDie();
    while (true) {
        std::shared_ptr<arrow::RecordBatch> batch;
        auto st = stream_reader->ReadNext(&batch);
        if (!st.ok()) throw std::runtime_error("Failed reading IPC stream batch: " + st.ToString());
        if (!batch) break;
        process(batch);
    }
}

// ── Unified reader dispatch ─────────────────────────────────────────────────

/// Detect input type and read all batches, calling `process` for each.
/// Returns the input mode string for metrics.
template <RecordBatchProcessor ProcessFn>
std::string read_arrow_data(const py::object& payload,
                            QuickTimer& timer,
                            ProcessFn&& process) {
    auto [mode, is_capsule, is_exporter] = detect_input_type(payload);

    if (is_capsule || is_exporter) {
#if PYGIM_HAVE_ARROW_C_BRIDGE
        read_c_stream(payload, is_capsule, timer, std::forward<ProcessFn>(process));
#else
        throw std::runtime_error(
            "Arrow C stream provided, but bridge headers unavailable at build time");
#endif
    } else {
        read_ipc_bytes(payload, timer, std::forward<ProcessFn>(process));
    }
    return mode;
}

} // namespace pygim::bcp
