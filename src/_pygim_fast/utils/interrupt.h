#pragma once
// utils/interrupt.h — Ctrl-C and SIGTERM owned by a native run for its length.
//
//     interrupt_scope scope;                       // SIGINT and SIGTERM are this run's now
//     start the work, telling it scope.finish() when it is done
//     if (scope.wait()) stop the work: interrupted // sleeps, no timeout: wakes on a signal or on finish()
//     // ~interrupt_scope: the previous handlers come back, and a signal that interrupted is raised
//     // again, so whoever had it (Python's handler: KeyboardInterrupt) receives it as if just sent
//
// The handler only sets a flag and writes a byte to each running scope's pipe — both safe in a
// signal handler — so it can arrive on any thread, mid-system-call (SA_RESTART restarts those).
// A signal the program ignores (SIG_IGN) is left alone: a run never stops on what the program
// asked not to hear. Runs at the same time share the handler, installed by the first and given
// back by the last, which raises the signal again then; Ctrl-C stops them all.
//
// Decided 2026-10-25 (#272): the operation runs with no Python and no polling; loops stop at an
// item's edge; an interrupted call raises and its partial result is dropped. Measured with a
// prototype: SIGINT at 200 ms into a 5 s run of 4 workers — the run returned at 200 ms with every
// item finished whole, and Python's handler raised KeyboardInterrupt after.
//
// The handlers and the pipe are the system's (platform/signals.h: SetConsoleCtrlHandler and an
// event per scope on Windows); which runs own the signals, and what came, are this header's.
//
// CORE layer: pybind-free.

#include <array>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "../platform/signals.h"

namespace pygim {

namespace detail {

/// The process's one set of handlers and the scopes they wake: shared by every interrupt_scope.
class interrupt_hub {
public:
    static constexpr std::size_t max_scopes = 64;

    [[nodiscard]] static interrupt_hub& instance() {
        static interrupt_hub hub;   // never destroyed before a scope: scopes live on callers' stacks
        return hub;
    }

    /// Takes a slot for `wake` (a platform::wakeup's handle), installing the handlers when it is the first.
    [[nodiscard]] std::size_t enter(std::intptr_t wake) {
        std::lock_guard guard(m_lock);
        if (m_scopes++ == 0) m_handlers.install(&on_signal);
        for (std::size_t i = 0; i < max_scopes; ++i) {
            std::intptr_t free = -1;
            if (m_wake[i].compare_exchange_strong(free, wake)) return i;
        }
        if (--m_scopes == 0) m_handlers.uninstall();
        throw std::runtime_error("more than 64 native runs at once own Ctrl-C");
    }

    /// Gives the slot back; the last scope out restores the handlers and raises a signal that came.
    void leave(std::size_t slot) {
        m_wake[slot].store(-1, std::memory_order_seq_cst);
        while (m_in_handler.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();   // a handler mid-write may hold the old handle
        std::lock_guard guard(m_lock);
        if (--m_scopes != 0) return;
        m_handlers.uninstall();
        if (const int sig = m_pending.exchange(0)) std::raise(sig);   // to whoever had it, now that they have it again
    }

    /// How many signals have arrived: a scope compares it with what it saw when it began.
    [[nodiscard]] unsigned arrivals() const noexcept { return m_arrivals.load(std::memory_order_acquire); }
    [[nodiscard]] int last_signal() const noexcept { return m_last.load(std::memory_order_acquire); }

private:
    interrupt_hub() { for (auto& w : m_wake) w.store(-1); }

    static void on_signal(int sig) noexcept { instance().arrive(sig); }

    // In the handler: a flag, and a wake for each running scope.
    void arrive(int sig) noexcept {
        m_in_handler.fetch_add(1, std::memory_order_seq_cst);
        m_last.store(sig, std::memory_order_release);
        m_pending.store(sig, std::memory_order_release);
        m_arrivals.fetch_add(1, std::memory_order_acq_rel);
        for (const auto& slot : m_wake) {
            const std::intptr_t wake = slot.load(std::memory_order_seq_cst);
            if (wake >= 0) platform::wakeup::wake_from_handler(wake);
        }
        m_in_handler.fetch_sub(1, std::memory_order_seq_cst);
    }

    platform::interrupt_handlers m_handlers;   // installed by enter's first, given back by leave's last
    std::mutex m_lock;                         // enter/leave only; never taken in the handler
    std::size_t m_scopes = 0;
    std::array<std::atomic<std::intptr_t>, max_scopes> m_wake;   // each running scope's wakeup handle
    std::atomic<int> m_in_handler{0};
    std::atomic<unsigned> m_arrivals{0};
    std::atomic<int> m_last{0};
    std::atomic<int> m_pending{0};
};

}  // namespace detail

/// One run's ownership of Ctrl-C and SIGTERM, from construction to destruction.
class interrupt_scope {
public:
    interrupt_scope()
        : m_seen(detail::interrupt_hub::instance().arrivals()),
          m_slot(detail::interrupt_hub::instance().enter(m_wake.handle())) {}
    interrupt_scope(const interrupt_scope&) = delete;
    interrupt_scope& operator=(const interrupt_scope&) = delete;
    ~interrupt_scope() { detail::interrupt_hub::instance().leave(m_slot); }   // before m_wake closes

    /// The work is done: wakes wait(). Safe from any thread; call it once.
    void finish() noexcept { m_wake.wake(); }

    /// Sleeps until finish() or a signal, with no timeout; true when a signal came.
    [[nodiscard]] bool wait() {
        m_wake.sleep();
        return interrupted();
    }

    /// Whether a signal has come since this scope began.
    [[nodiscard]] bool interrupted() const noexcept { return detail::interrupt_hub::instance().arrivals() != m_seen; }
    /// The signal that came (SIGINT or SIGTERM), 0 when none.
    [[nodiscard]] int signal() const noexcept { return interrupted() ? detail::interrupt_hub::instance().last_signal() : 0; }

private:
    platform::wakeup m_wake;   // first: opened before the slot names it, closed after leave() gives it back
    unsigned m_seen;
    std::size_t m_slot;
};

}  // namespace pygim
