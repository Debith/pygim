#pragma once
// core/primitives/parallel/crew.h — the threads a pool starts (worker_crew): each named and with
// every asynchronous signal blocked, started up to a width as far as the system lets them start,
// and joined when the crew stops.
//
//     worker_crew crew("pygim-pool");
//     crew.grow_to(4, [&](std::size_t index) { work(); });   // threads 0..3, as many as can start
//     crew.stop();                                             // none starts after; each is joined
//
// CORE layer: pybind-free.

#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "../../../platform/process.h"
#include "../scope_exit.h"

namespace pygim::primitives::parallel {

/// One owner's threads, under one name: pygim-pool for a pool's, pygim-parked for the threads
/// for_each borrows, as `top -H` and a debugger show them. Each blocks every asynchronous signal, so
/// Ctrl-C reaches the thread that can act on it (platform/process.h). The crew joins them when it
/// stops or is destroyed; by then they must have been told to end.
class worker_crew {
public:
    explicit worker_crew(const char* name) noexcept : m_name(name) {}
    ~worker_crew() { stop(); }
    worker_crew(const worker_crew&) = delete;
    worker_crew& operator=(const worker_crew&) = delete;

    /// Starts threads running body(index), their indexes 0, 1, ... in starting order, until
    /// `wanted` run: as many as the system lets start, none after stop(). How many run.
    template <class Body>
    std::size_t grow_to(std::size_t wanted, const Body& body) {
        if (wanted <= started()) return started();
        std::lock_guard guard(m_spawn);
        const on_scope_exit counted([this] { m_started.store(m_threads.size(), std::memory_order_relaxed); });
        const platform::native_process::signals_blocked quiet;   // inherited by the threads started here
        try {
            while (!m_stopped && m_threads.size() < wanted) start(m_threads.size(), body);
        } catch (const std::system_error&) {   // the work is the same, on fewer threads
            if (!m_refused) m_refused = std::current_exception();
        }
        return m_threads.size();
    }

    /// What the system said when a thread would not start, rethrown when not one runs: for work no
    /// other thread will do.
    void rethrow_if_none_run() const {
        std::lock_guard guard(m_spawn);
        if (m_threads.empty() && m_refused) std::rethrow_exception(m_refused);
    }

    /// How many have started: read without the lock, so exact once the crew stops growing.
    [[nodiscard]] std::size_t started() const noexcept { return m_started.load(std::memory_order_relaxed); }

    /// No thread starts after this; each one started is joined, outside the lock, which a thread
    /// growing the crew may be waiting for.
    void stop() noexcept {
        std::vector<std::jthread> threads;
        {
            std::lock_guard guard(m_spawn);
            m_stopped = true;
            threads = std::move(m_threads);
        }
    }   // joined here

private:
    template <class Body>
    void start(std::size_t index, const Body& body) {
        m_threads.emplace_back([name = m_name, body, index] {
            platform::native_process::name_this_thread(name);
            body(index);
        });
    }

    const char* m_name;
    mutable std::mutex m_spawn;               // the threads, the stop and the refusal
    std::vector<std::jthread> m_threads;
    bool m_stopped = false;
    std::exception_ptr m_refused;             // why the system would not start one
    std::atomic<std::size_t> m_started{0};
};

}  // namespace pygim::primitives::parallel
