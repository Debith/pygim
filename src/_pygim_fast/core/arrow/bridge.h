#pragma once
// core/arrow/bridge.h — what datagen and persistence both do at Arrow's edge: an Arrow status or
// result made pygim's error, and a RecordBatchReader handed over as Arrow's C stream
// (ArrowArrayStream), which a Python consumer takes by capsule or by address.
//
//     const auto table = pygim::arrow_bridge::value_of(reader->ToTable(), "reading the Arrow stream");
//     pygim::arrow_bridge::exported_stream stream(reader);   // BackendError if Arrow refuses
//
// CORE layer: Arrow, no Python.

#include <arrow/c/abi.h>
#include <arrow/c/bridge.h>
#include <arrow/record_batch.h>
#include <arrow/result.h>
#include <arrow/status.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "../../errors/core.h"

namespace pygim::arrow_bridge {

/// Returns when `status` is OK; otherwise pygim::errors::backend_error, "`context`: Arrow's reason".
inline void check(const ::arrow::Status& status, std::string_view context) {
    if (!status.ok()) [[unlikely]]
        throw pygim::errors::backend_error(std::string(context) + ": " + status.ToString());
}

/// The value `result` holds, or backend_error as check() says it.
template <class T>
[[nodiscard]] T value_of(::arrow::Result<T> result, std::string_view context) {
    check(result.status(), context);
    return std::move(result).ValueUnsafe();
}

/// A RecordBatchReader exported as Arrow's C stream. It owns the stream until a consumer takes it,
/// and releases it if none does.
class exported_stream {
public:
    explicit exported_stream(std::shared_ptr<::arrow::RecordBatchReader> reader) : m_stream(new ArrowArrayStream{}) {
        check(::arrow::ExportRecordBatchReader(std::move(reader), m_stream.get()), "exporting an Arrow stream");
    }

    /// The stream, still owned here: for a consumer that takes it by address and marks it released.
    [[nodiscard]] ArrowArrayStream* get() const noexcept { return m_stream.get(); }

    /// The stream, the caller's from now on, to be freed with free(): for a capsule.
    [[nodiscard]] ArrowArrayStream* release() noexcept { return m_stream.release(); }

    /// Releases `stream` unless its consumer did, and frees it.
    static void free(ArrowArrayStream* stream) noexcept {
        if (stream->release != nullptr) stream->release(stream);
        delete stream;
    }

private:
    struct freeing {
        void operator()(ArrowArrayStream* stream) const noexcept { free(stream); }
    };
    std::unique_ptr<ArrowArrayStream, freeing> m_stream;
};

}  // namespace pygim::arrow_bridge
