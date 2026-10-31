#pragma once
// platform/signals.h — Ctrl-C and SIGTERM as each system delivers them, and a wake-up a run sleeps
// on until its work is done or one comes. Infrastructure behind the platform boundary
// (docs/design/platform_boundary.md, D77): utils/interrupt.h keeps which runs own the signals and
// what came, and never asks which system it runs on.
//
//     handlers.install(&arrive);   // arrive(SIGINT or SIGTERM) runs in the handler
//     wakeup w;  w.sleep();        // until w.wake(), or wakeup::wake_from_handler(w.handle())
//
// POSIX: sigaction for SIGINT and SIGTERM (a signal the program ignores is left alone), and a pipe
// whose read end is polled. Windows: SetConsoleCtrlHandler for Ctrl-C and Break (SIGINT) and a
// closing console (SIGTERM), and an event (not yet run on Windows). Each system's engine is whole
// in its own branch.
//
// CORE layer: pybind-free.

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "windows_headers.h"

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace pygim::platform {

// interrupt_handlers — the process's handlers for Ctrl-C and SIGTERM: installed with the function
// each arrival calls, in the handler (so it may only do what a signal handler may), and given back
// as they were. One per process: its owner (utils/interrupt.h's hub) installs it once however many
// runs share it.
//
// wakeup — what a run sleeps on until its work is done or a signal comes. wake() is safe from any
// thread; wake_from_handler(handle()) in a signal handler.

#ifdef _WIN32

class interrupt_handlers {
public:
    using arrival = void (*)(int sig);

    void install(arrival arrive) {
        s_arrive = arrive;
        ::SetConsoleCtrlHandler(&on_console, TRUE);
    }
    void uninstall() { ::SetConsoleCtrlHandler(&on_console, FALSE); }

private:
    static BOOL WINAPI on_console(DWORD event) {
        if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT) return FALSE;
        s_arrive(event == CTRL_CLOSE_EVENT ? SIGTERM : SIGINT);
        // Handled. A closing console is Windows' to end: the process ends once this returns (or
        // after the system's few seconds), so what arrival stops may not reach its edge.
        return TRUE;
    }
    static inline arrival s_arrive = nullptr;   // the console's handler runs on a thread of its own
};

class wakeup {
public:
    wakeup() : m_event(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (m_event == nullptr) throw std::runtime_error("cannot create the event a run waits on");
    }
    wakeup(const wakeup&) = delete;
    wakeup& operator=(const wakeup&) = delete;
    ~wakeup() { ::CloseHandle(m_event); }

    [[nodiscard]] std::intptr_t handle() const noexcept { return reinterpret_cast<std::intptr_t>(m_event); }
    static void wake_from_handler(std::intptr_t handle) noexcept { ::SetEvent(reinterpret_cast<HANDLE>(handle)); }
    void wake() noexcept { ::SetEvent(m_event); }
    void sleep() noexcept { ::WaitForSingleObject(m_event, INFINITE); }

private:
    HANDLE m_event;
};

#else

class interrupt_handlers {
public:
    using arrival = void (*)(int sig);

    void install(arrival arrive) {
        for (std::size_t i = 0; i < signals.size(); ++i) {
            struct sigaction theirs {};
            ::sigaction(signals[i], nullptr, &theirs);
            m_taken[i] = theirs.sa_handler != SIG_IGN;   // what the program ignores stays ignored
            if (!m_taken[i]) continue;
            struct sigaction ours {};
            ours.sa_handler = arrive;
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

private:
    static constexpr std::array<int, 2> signals{SIGINT, SIGTERM};
    std::array<struct sigaction, signals.size()> m_theirs{};
    std::array<bool, signals.size()> m_taken{};
};

class wakeup {
public:
    wakeup() {
        if (::pipe(m_pipe) != 0) throw std::runtime_error("cannot open the pipe a run waits on");
        for (const int fd : m_pipe) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ::fcntl(m_pipe[1], F_SETFL, O_NONBLOCK);   // a full pipe already wakes: a handler never blocks on it
    }
    wakeup(const wakeup&) = delete;
    wakeup& operator=(const wakeup&) = delete;
    ~wakeup() {
        ::close(m_pipe[0]);
        ::close(m_pipe[1]);
    }

    [[nodiscard]] std::intptr_t handle() const noexcept { return m_pipe[1]; }
    static void wake_from_handler(std::intptr_t handle) noexcept {
        const char byte = 1;
        const int saved = errno;   // a handler leaves errno as the code it interrupted had it
        (void)!::write(static_cast<int>(handle), &byte, 1);
        errno = saved;
    }
    void wake() noexcept {
        const char byte = 0;
        (void)!::write(m_pipe[1], &byte, 1);
    }
    void sleep() noexcept {
        pollfd p{m_pipe[0], POLLIN, 0};
        while (::poll(&p, 1, -1) < 0 && errno == EINTR) {}
    }

private:
    int m_pipe[2] = {-1, -1};
};

#endif

}  // namespace pygim::platform
