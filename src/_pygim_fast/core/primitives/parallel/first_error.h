#pragma once
// core/primitives/parallel/first_error.h — the first exception any of several threads lets out,
// kept to be rethrown on the thread that waits for them all.
//
//     first_error errors;
//     try { work(); } catch (...) { errors.capture(); }   // on each thread
//     errors.rethrow();                                   // after they all stopped: the first, if any
//
// CORE layer: pybind-free.

#include <atomic>
#include <exception>
#include <mutex>

namespace pygim::primitives::parallel {

/// The first exception captured among threads; later ones are dropped.
class first_error {
public:
    /// Keeps the exception being handled unless one was kept before: call it inside a catch.
    void capture() noexcept {
        std::lock_guard lock(m_mutex);
        if (!m_error) m_error = std::current_exception();
        m_failed.store(true, std::memory_order_relaxed);
    }
    /// Whether one was captured: what lets the other threads stop taking work.
    [[nodiscard]] bool failed() const noexcept { return m_failed.load(std::memory_order_relaxed); }
    /// The one captured, rethrown, if any; it stays captured.
    void rethrow() const {
        std::lock_guard lock(m_mutex);
        if (m_error) std::rethrow_exception(m_error);
    }

private:
    mutable std::mutex m_mutex;
    std::exception_ptr m_error;
    std::atomic<bool> m_failed{false};
};

}  // namespace pygim::primitives::parallel
