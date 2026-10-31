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
// Windows: SetConsoleCtrlHandler, an event per scope in place of the pipe (not yet run on Windows).
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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace pygim {

namespace detail {

/// The process's one set of handlers and the scopes they wake: shared by every interrupt_scope.
class interrupt_hub {
public:
    static constexpr std::size_t max_scopes = 64;
    static constexpr std::array<int, 2> signals{SIGINT, SIGTERM};

    [[nodiscard]] static interrupt_hub& instance() {
        static interrupt_hub hub;   // never destroyed before a scope: scopes live on callers' stacks
        return hub;
    }

    /// Takes a slot for `wake`, installing the handlers when it is the first.
    [[nodiscard]] std::size_t enter(std::intptr_t wake) {
        std::lock_guard guard(m_lock);
        if (m_scopes++ == 0) install();
        for (std::size_t i = 0; i < max_scopes; ++i) {
            std::intptr_t free = -1;
            if (m_wake[i].compare_exchange_strong(free, wake)) return i;
        }
        if (--m_scopes == 0) uninstall();
        throw std::runtime_error("more than 64 native runs at once own Ctrl-C");
    }

    /// Gives the slot back; the last scope out restores the handlers and raises a signal that came.
    void leave(std::size_t slot) {
        m_wake[slot].store(-1, std::memory_order_seq_cst);
        while (m_in_handler.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();   // a handler mid-write may hold the old descriptor
        std::lock_guard guard(m_lock);
        if (--m_scopes != 0) return;
        uninstall();
        if (const int sig = m_pending.exchange(0)) std::raise(sig);   // to whoever had it, now that they have it again
    }

    /// How many signals have arrived: a scope compares it with what it saw when it began.
    [[nodiscard]] unsigned arrivals() const noexcept { return m_arrivals.load(std::memory_order_acquire); }
    [[nodiscard]] int last_signal() const noexcept { return m_last.load(std::memory_order_acquire); }

#ifdef _WIN32
    static BOOL WINAPI on_console(DWORD event) {
        if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT) return FALSE;
        instance().arrive(event == CTRL_CLOSE_EVENT ? SIGTERM : SIGINT);
        // Handled. Ctrl-C and Break stop the run at an item's edge and are raised again after it;
        // a closing console is Windows' to end: the process ends once this returns (or after the
        // system's few seconds), so its run is told to stop but may not reach its edge.
        return TRUE;
    }
#else
    static void on_signal(int sig) noexcept { instance().arrive(sig); }
#endif

private:
    interrupt_hub() { for (auto& w : m_wake) w.store(-1); }

    // In the handler: a flag and a byte to each running scope (Windows: each scope's event).
    void arrive(int sig) noexcept {
        m_in_handler.fetch_add(1, std::memory_order_seq_cst);
        m_last.store(sig, std::memory_order_release);
        m_pending.store(sig, std::memory_order_release);
        m_arrivals.fetch_add(1, std::memory_order_acq_rel);
        for (const auto& slot : m_wake) {
            const std::intptr_t wake = slot.load(std::memory_order_seq_cst);
            if (wake < 0) continue;
#ifdef _WIN32
            ::SetEvent(reinterpret_cast<HANDLE>(wake));
#else
            const char byte = 1;
            const int saved = errno;
            (void)!::write(static_cast<int>(wake), &byte, 1);
            errno = saved;
#endif
        }
        m_in_handler.fetch_sub(1, std::memory_order_seq_cst);
    }

#ifdef _WIN32
    void install() { ::SetConsoleCtrlHandler(&interrupt_hub::on_console, TRUE); }
    void uninstall() { ::SetConsoleCtrlHandler(&interrupt_hub::on_console, FALSE); }
#else
    void install() {
        for (std::size_t i = 0; i < signals.size(); ++i) {
            struct sigaction theirs {};
            ::sigaction(signals[i], nullptr, &theirs);
            m_taken[i] = theirs.sa_handler != SIG_IGN;   // what the program ignores stays ignored
            if (!m_taken[i]) continue;
            struct sigaction ours {};
            ours.sa_handler = &interrupt_hub::on_signal;
            sigemptyset(&ours.sa_mask);
            ours.sa_flags = SA_RESTART;
            ::sigaction(signals[i], &ours, &m_theirs[i]);
        }
    }
    void uninstall() {
        for (std::size_t i = 0; i < signals.size(); ++i) {
            if (m_taken[i]) ::sigaction(signals[i], &m_theirs[i], nullptr);
        }
    }
    std::array<struct sigaction, signals.size()> m_theirs{};
    std::array<bool, signals.size()> m_taken{};
#endif

    std::mutex m_lock;                         // enter/leave only; never taken in the handler
    std::size_t m_scopes = 0;
    std::array<std::atomic<std::intptr_t>, max_scopes> m_wake;   // a pipe's write end, or a Windows event
    std::atomic<int> m_in_handler{0};
    std::atomic<unsigned> m_arrivals{0};
    std::atomic<int> m_last{0};
    std::atomic<int> m_pending{0};
};

}  // namespace detail

/// One run's ownership of Ctrl-C and SIGTERM, from construction to destruction.
class interrupt_scope {
public:
    interrupt_scope() : m_wake(open_wake()), m_seen(detail::interrupt_hub::instance().arrivals()) {
        try {
            m_slot = detail::interrupt_hub::instance().enter(wake_handle());
        } catch (...) {
            close_wake();
            throw;
        }
    }
    interrupt_scope(const interrupt_scope&) = delete;
    interrupt_scope& operator=(const interrupt_scope&) = delete;
    ~interrupt_scope() {
        detail::interrupt_hub::instance().leave(m_slot);
        close_wake();
    }

    /// The work is done: wakes wait(). Safe from any thread; call it once.
    void finish() noexcept {
#ifdef _WIN32
        ::SetEvent(m_wake.event);
#else
        const char byte = 0;
        (void)!::write(m_wake.pipe[1], &byte, 1);
#endif
    }

    /// Sleeps until finish() or a signal, with no timeout; true when a signal came.
    [[nodiscard]] bool wait() {
#ifdef _WIN32
        ::WaitForSingleObject(m_wake.event, INFINITE);
#else
        pollfd p{m_wake.pipe[0], POLLIN, 0};
        while (::poll(&p, 1, -1) < 0 && errno == EINTR) {}
#endif
        return interrupted();
    }

    /// Whether a signal has come since this scope began.
    [[nodiscard]] bool interrupted() const noexcept { return detail::interrupt_hub::instance().arrivals() != m_seen; }
    /// The signal that came (SIGINT or SIGTERM), 0 when none.
    [[nodiscard]] int signal() const noexcept { return interrupted() ? detail::interrupt_hub::instance().last_signal() : 0; }

private:
#ifdef _WIN32
    struct wake_t { HANDLE event; };
    static wake_t open_wake() {
        const HANDLE e = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (e == nullptr) throw std::runtime_error("cannot create the event a run waits on");
        return {e};
    }
    [[nodiscard]] std::intptr_t wake_handle() const noexcept { return reinterpret_cast<std::intptr_t>(m_wake.event); }
    void close_wake() noexcept { ::CloseHandle(m_wake.event); }
#else
    struct wake_t { int pipe[2]; };
    static wake_t open_wake() {
        wake_t w{{-1, -1}};
        if (::pipe(w.pipe) != 0) throw std::runtime_error("cannot open the pipe a run waits on");
        for (const int fd : w.pipe) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ::fcntl(w.pipe[1], F_SETFL, O_NONBLOCK);   // a full pipe already wakes: a handler never blocks on it
        return w;
    }
    [[nodiscard]] std::intptr_t wake_handle() const noexcept { return m_wake.pipe[1]; }
    void close_wake() noexcept {
        ::close(m_wake.pipe[0]);
        ::close(m_wake.pipe[1]);
    }
#endif

    wake_t m_wake;
    unsigned m_seen;
    std::size_t m_slot = 0;
};

}  // namespace pygim
