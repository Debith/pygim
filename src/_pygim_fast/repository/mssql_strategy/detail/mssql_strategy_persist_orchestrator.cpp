#include "mssql_strategy_persist.h"

namespace pygim::persist_detail {

py::dict PersistDataFrameOrchestrator::execute(const PersistRequestView &view) const {
    std::optional<std::string> arrow_error = std::nullopt;

    if (view.prefer_arrow) {
        if (!env_true("PYGIM_ENABLE_ARROW_BCP")) {
            arrow_error =
                "Arrow BCP strategy disabled by default for stability; "
                "set PYGIM_ENABLE_ARROW_BCP=1 to enable";
        } else {
            const ArrowPersistView arrow_view = view.arrow_view();

            PersistAttempt c_stream_attempt = m_arrow.try_c_stream(arrow_view);
            if (c_stream_attempt.success) {
                py::dict out = to_py_dict(c_stream_attempt, std::nullopt);
                out["bcp_metrics"] = m_strategy.last_bcp_metrics();
                return out;
            }

            PersistAttempt ipc_attempt = m_arrow.try_ipc(arrow_view);
            if (ipc_attempt.success) {
                py::dict out = to_py_dict(
                    ipc_attempt,
                    c_stream_attempt.error.empty()
                        ? std::nullopt
                        : std::optional<std::string>(c_stream_attempt.error));
                out["bcp_metrics"] = m_strategy.last_bcp_metrics();
                return out;
            }

            std::string combined_error;
            if (!c_stream_attempt.error.empty()) {
                combined_error += c_stream_attempt.error;
            }
            if (!ipc_attempt.error.empty()) {
                if (!combined_error.empty()) {
                    combined_error += " | ";
                }
                combined_error += ipc_attempt.error;
            }
            arrow_error = combined_error.empty()
                              ? std::optional<std::string>("Arrow strategies failed")
                              : std::optional<std::string>(combined_error);
        }
    }

    PersistAttempt fallback = m_bulk.run(view);
    py::dict out = to_py_dict(fallback, arrow_error);
    out["bcp_metrics"] = py::none();
    return out;
}

} // namespace pygim::persist_detail
