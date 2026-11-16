#pragma once
// core/primitives/parallel/for_each.h — the threads the process lends (parked_workers) and for_each,
// which runs indexed tasks on them, on threads of its own when they are lent.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "crew.h"
#include "width.h"

namespace pygim::primitives::parallel {

/// The most threads one for_each starts.
inline constexpr std::size_t max_workers = 64;

/// The worker count for `items` independent pieces of work: every hardware
/// thread (at most max_workers), never more than there are pieces, at least one.
[[nodiscard]] inline std::size_t workers_for(std::size_t items) {
    return std::max<std::size_t>(1, std::min<std::size_t>({this_machines_cores(), items, max_workers}));
}

// ── the threads for_each borrows ─────────────────────────────────────────

/// One parked_workers job as a 64-bit word: how many helpers it wants (high half) and how many
/// tickets have been taken (low). A ticket below the helpers wanted is a real one — a helper's
/// share of the job — and any later one surplus, with which a thread touches nothing.
class job_word {
public:
    /// A new job for `helpers` helpers, no ticket taken.
    [[nodiscard]] static constexpr std::uint64_t opened(std::size_t helpers) noexcept { return std::uint64_t{helpers} << 32; }

    constexpr explicit job_word(std::uint64_t word) noexcept : m_word(word) {}

    [[nodiscard]] constexpr std::uint32_t helpers() const noexcept { return static_cast<std::uint32_t>(m_word >> 32); }
    [[nodiscard]] constexpr std::uint32_t tickets_taken() const noexcept { return static_cast<std::uint32_t>(m_word & 0xffff'ffffu); }
    /// Whether the ticket this word's taker drew is a real one.
    [[nodiscard]] constexpr bool real_ticket() const noexcept { return tickets_taken() < helpers(); }
    /// How many real tickets were taken when the job closed at this word.
    [[nodiscard]] constexpr std::uint32_t helpers_working() const noexcept { return std::min(tickets_taken(), helpers()); }

private:
    std::uint64_t m_word;
};
static_assert(job_word(job_word::opened(3) + 2).real_ticket() && !job_word(job_word::opened(3) + 3).real_ticket() &&
              job_word(job_word::opened(3) + 5).helpers_working() == 3 && job_word(job_word::opened(3) + 1).helpers_working() == 1);

/// The process's threads for for_each: started at the first for_each that wants more than one,
/// one fewer than the cores (the caller works too), parked between calls. A call that finds them
/// lent — a for_each inside a task, or one on another thread at the same time — or that wants
/// more threads than the pool holds starts its own, as every call did before. Starting and
/// joining 16 threads cost each for_each 500 µs, and a token index build makes eight
/// (2026-10-28, Linux).
///
/// A job is one 64-bit word (job_word). The caller publishes the job, wakes as many parked
/// threads, runs tasks itself, and when the tasks are gone adds `helpers` tickets so every later
/// one is surplus; it then waits only for the helpers that took a real ticket. A thread that wakes
/// late takes a ticket of whatever job is current — a real one, or a surplus one with which it
/// touches nothing — so a job's body is read only by helpers its caller waits for. After a fork
/// the child starts a pool of its own. Each extension module that includes this header has its
/// own pool (its own copy of these statics).
///
/// The threads park for the life of the process: once a pool has run, the process has threads,
/// and a later fork() is a fork of a process with threads (Python 3.12+ warns of it: what another
/// thread held in the parent is held for ever in the child, which only these statics' own
/// atfork handler takes care of). They block every asynchronous signal (signals_blocked).
class parked_workers {
public:
    /// The process's pool; created on first use, then reached without a lock.
    [[nodiscard]] static parked_workers& process() {
        if (parked_workers* pool = current().load(std::memory_order_acquire)) return *pool;
        std::lock_guard lock(creating());
        if (current().load(std::memory_order_relaxed) == nullptr) {
            current().store(new parked_workers(this_machines_cores() - 1), std::memory_order_release);   // never destroyed: its threads park until the process ends
        }
        return *current().load(std::memory_order_relaxed);
    }

    /// Runs body(0) on the calling thread and body(1..helpers) on parked threads as they wake, if
    /// the pool is free and holds `helpers` threads; returns false, running nothing, otherwise.
    /// `body` returns once no task is left, wherever it runs. Whatever body(0) throws is rethrown
    /// once every helper that took a share has left the body; what a helper's body throws, after.
    template <class Body>
    bool run(std::size_t helpers, Body& body) {
        if (helpers > m_threads.size() || m_busy.exchange(true, std::memory_order_acquire)) return false;
        class lent {
        public:
            explicit lent(std::atomic<bool>& busy) noexcept : m_busy(busy) {}
            ~lent() { m_busy.store(false, std::memory_order_release); }

        private:
            std::atomic<bool>& m_busy;
        } returned_after(m_busy);
        m_body = &body;
        m_call = [](void* b, std::size_t worker) { (*static_cast<Body*>(b))(worker); };
        m_done.store(0, std::memory_order_relaxed);
        m_job.store(job_word::opened(helpers), std::memory_order_release);   // no ticket taken yet
        m_wake.fetch_add(1, std::memory_order_release);
        if (2 * helpers >= m_threads.size()) m_wake.notify_all();   // one wake for many: each notify is a syscall on the caller's path
        else for (std::size_t k = 0; k < helpers; ++k) m_wake.notify_one();
        std::exception_ptr own;
        try {
            body(0);
        } catch (...) {
            own = std::current_exception();   // rethrown below: the helpers read `body` until they are waited for
        }
        const job_word closed(m_job.fetch_add(helpers, std::memory_order_acq_rel));   // the tickets left are no one's now
        for (std::uint32_t done; (done = m_done.load(std::memory_order_acquire)) != closed.helpers_working();) m_done.wait(done, std::memory_order_acquire);
        if (own) std::rethrow_exception(own);
        if (m_error) std::rethrow_exception(std::exchange(m_error, nullptr));   // what a helper's body let out (for_each's catches all)
        return true;
    }

private:
    // Starts up to `threads` parked threads: as many as the system lets start, none when it lets
    // none (a pool of none lends nothing, and every for_each runs on threads of its own or alone).
    explicit parked_workers(std::size_t threads) {
        m_threads.reserve(threads);
        const signals_blocked quiet;   // inherited: the parked threads never take a signal
        try {
            for (std::size_t t = 0; t < threads; ++t) m_threads.emplace_back([this] { name_this_thread("pygim-parked"); serve(); });
        } catch (const std::system_error&) {
        }
        // a fork while another thread creates the pool would leave the child's lock held: it is
        // held across the fork, and the child starts from no pool
        static const bool reset_in_child = platform::at_fork([] { creating().lock(); }, [] { creating().unlock(); },
                                                             [] {
                                                                 creating().unlock();
                                                                 current().store(nullptr, std::memory_order_relaxed);
                                                             });
        (void)reset_in_child;
    }

    [[nodiscard]] static std::atomic<parked_workers*>& current() noexcept {
        static std::atomic<parked_workers*> pool{nullptr};
        return pool;
    }
    [[nodiscard]] static std::mutex& creating() noexcept {
        static std::mutex lock;
        return lock;
    }

    void serve() {
        std::uint32_t woken = 0;   // the wake count this thread last saw
        for (;;) {
            m_wake.wait(woken, std::memory_order_acquire);
            woken = m_wake.load(std::memory_order_acquire);
            const job_word drawn(m_job.fetch_add(1, std::memory_order_acq_rel));
            if (drawn.real_ticket()) {
                try {   // a thread must never let an exception out: it would abort the interpreter
                    m_call(m_body, drawn.tickets_taken() + 1);
                } catch (...) {
                    std::lock_guard lock(m_error_mutex);
                    if (!m_error) m_error = std::current_exception();
                }
                m_done.fetch_add(1, std::memory_order_acq_rel);
                m_done.notify_one();
            }
        }
    }

    std::vector<std::thread> m_threads;
    std::atomic<bool> m_busy{false};         // a call holds the pool; an atomic, since a for_each inside body(0) asks again on the same thread
    void* m_body = nullptr;                  // the job: m_call(m_body, worker)
    void (*m_call)(void*, std::size_t) = nullptr;
    std::atomic<std::uint64_t> m_job{0};     // a job_word
    std::atomic<std::uint32_t> m_wake{0};    // parked threads wait on it (32 bits: a futex, see the file comment)
    std::atomic<std::uint32_t> m_done{0};    // helpers of the current job that finished
    std::mutex m_error_mutex;
    std::exception_ptr m_error;              // the first a helper let out, rethrown by the caller
};

/// Calls f(task, worker) for every task in [0, tasks) on `threads` threads (never more than there
/// are tasks), each claiming the next task when it finishes one (rows of a sweep differ a lot in
/// cost). Worker 0 is the calling thread; the others are the parked_workers' threads when they are
/// free, threads of their own otherwise — as many as the system lets start, the caller alone if
/// none. The first exception thrown by any task is rethrown here after every thread has stopped.
template <class F>
void for_each(std::size_t tasks, std::size_t threads, F&& f) {
    threads = std::min(threads, tasks);
    if (threads <= 1) {
        for (std::size_t t = 0; t < tasks; ++t) f(t, std::size_t{0});
        return;
    }
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex error_mutex;
    auto run = [&](std::size_t worker) {
        for (std::size_t t; !failed.load(std::memory_order_relaxed) && (t = next.fetch_add(1)) < tasks;) {
            try {
                f(t, worker);
            } catch (...) {
                std::lock_guard lock(error_mutex);
                if (!error) error = std::current_exception();
                failed = true;
            }
        }
    };
    if (!parked_workers::process().run(threads - 1, run)) {
        std::vector<std::jthread> own;   // joined as it goes, the caller's share done
        own.reserve(threads - 1);
        try {
            const signals_blocked quiet;   // inherited by the threads started here
            for (std::size_t w = 1; w < threads; ++w) own.emplace_back([&run, w] { name_this_thread("pygim-pool"); run(w); });
        } catch (const std::system_error&) {
        }   // as many as could start: the work is the same, on fewer threads
        run(0);
    }
    if (error) std::rethrow_exception(error);
}

}  // namespace pygim::primitives::parallel
