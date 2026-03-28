#pragma once

#include "quick_timer.h"

#include <chrono>
#include <cstdio>
#include <string_view>

#ifndef PYGIM_SCOPE_LOGGING_ENABLED
#ifdef PYGIM_REPOSITORY_LOGGING_ENABLED
#define PYGIM_SCOPE_LOGGING_ENABLED PYGIM_REPOSITORY_LOGGING_ENABLED
#else
#define PYGIM_SCOPE_LOGGING_ENABLED 0
#endif
#endif

// ── Macro hygiene helpers ────────────────────────────────────────
#define PYGIM_CONCAT_IMPL(a, b) a##b
#define PYGIM_CONCAT(a, b) PYGIM_CONCAT_IMPL(a, b)

namespace pygim::logging {

// ── ScopeLog ─────────────────────────────────────────────────────
// Lightweight RAII enter/exit trace with file:line info.
// Zero heap allocations — raw steady_clock + fprintf to stderr.
class ScopeLog {
public:
    ScopeLog(const char *func,
             const char *file,
             int line,
             std::string_view tag = {},
             std::string_view note = {})
        : m_func(func),
          m_file(file),
          m_line(line),
          m_tag(tag),
          m_note(note),
          m_enabled(PYGIM_SCOPE_LOGGING_ENABLED != 0) {
        if (!m_enabled) return;
        m_start = std::chrono::steady_clock::now();
        write_enter();
    }

    ~ScopeLog() {
        if (!m_enabled) return;
        const auto micros = std::chrono::duration_cast<
            std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_start).count();
        write_exit(micros);
    }

private:
    void write_enter() const {
        if (m_tag.empty()) {
            if (m_note.empty()) {
                std::fprintf(stderr, "[pygim] enter %s (%s:%d)\n",
                             m_func, m_file, m_line);
            } else {
                std::fprintf(stderr, "[pygim] enter %s (%s:%d) : %.*s\n",
                             m_func, m_file, m_line,
                             static_cast<int>(m_note.size()), m_note.data());
            }
        } else {
            if (m_note.empty()) {
                std::fprintf(stderr, "[pygim:%.*s] enter %s (%s:%d)\n",
                             static_cast<int>(m_tag.size()), m_tag.data(),
                             m_func, m_file, m_line);
            } else {
                std::fprintf(stderr, "[pygim:%.*s] enter %s (%s:%d) : %.*s\n",
                             static_cast<int>(m_tag.size()), m_tag.data(),
                             m_func, m_file, m_line,
                             static_cast<int>(m_note.size()), m_note.data());
            }
        }
    }

    void write_exit(long long micros) const {
        if (m_tag.empty()) {
            if (m_note.empty()) {
                std::fprintf(stderr, "[pygim] exit  %s (%s:%d) after %lldus\n",
                             m_func, m_file, m_line, micros);
            } else {
                std::fprintf(stderr, "[pygim] exit  %s (%s:%d) after %lldus : %.*s\n",
                             m_func, m_file, m_line, micros,
                             static_cast<int>(m_note.size()), m_note.data());
            }
        } else {
            if (m_note.empty()) {
                std::fprintf(stderr, "[pygim:%.*s] exit  %s (%s:%d) after %lldus\n",
                             static_cast<int>(m_tag.size()), m_tag.data(),
                             m_func, m_file, m_line, micros);
            } else {
                std::fprintf(stderr, "[pygim:%.*s] exit  %s (%s:%d) after %lldus : %.*s\n",
                             static_cast<int>(m_tag.size()), m_tag.data(),
                             m_func, m_file, m_line, micros,
                             static_cast<int>(m_note.size()), m_note.data());
            }
        }
    }

    const char *m_func;
    const char *m_file;
    int m_line;
    std::string_view m_tag;
    std::string_view m_note;
    bool m_enabled;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace pygim::logging

// ── Macros ───────────────────────────────────────────────────────
#if PYGIM_SCOPE_LOGGING_ENABLED

// Scope enter/exit trace
#define PYGIM_SCOPE_LOG()                                                       \
    ::pygim::logging::ScopeLog PYGIM_CONCAT(pygim_scope_log_, __COUNTER__)(     \
        __func__, __FILE__, __LINE__, {}, {})
#define PYGIM_SCOPE_LOG_MSG(note)                                               \
    ::pygim::logging::ScopeLog PYGIM_CONCAT(pygim_scope_log_, __COUNTER__)(     \
        __func__, __FILE__, __LINE__, {}, (note))
#define PYGIM_SCOPE_LOG_TAG(tag)                                                \
    ::pygim::logging::ScopeLog PYGIM_CONCAT(pygim_scope_log_, __COUNTER__)(     \
        __func__, __FILE__, __LINE__, (tag), {})
#define PYGIM_SCOPE_LOG_TAG_MSG(tag, note)                                      \
    ::pygim::logging::ScopeLog PYGIM_CONCAT(pygim_scope_log_, __COUNTER__)(     \
        __func__, __FILE__, __LINE__, (tag), (note))

#define PYGIM_REPOSITORY_SCOPE_LOG()      PYGIM_SCOPE_LOG_TAG("repo")
#define PYGIM_REPOSITORY_SCOPE_LOG_MSG(n) PYGIM_SCOPE_LOG_TAG_MSG("repo", (n))

// Printf-style trace — all output to stderr (unified with QuickTimer).
#define PYGIM_LOG_FMT(fmt, ...) std::fprintf(stderr, fmt, ##__VA_ARGS__)

// RAII QuickTimer — prints summary with sub-timer breakdown at scope exit.
// Output goes to stderr (same stream as PYGIM_LOG_FMT and ScopeLog).
// Use PYGIM_TIMED_SUB("phase") within the same scope to add sub-timers.
// Variable name is always pygim_timer_ so PYGIM_TIMED_SUB can reference it.
#define PYGIM_TIMED_SCOPE(name)                                                 \
    ::pygim::QuickTimer pygim_timer_((name), stderr,                            \
                                     /*auto_print=*/true,                       \
                                     /*print_on_start=*/false)

// Start a sub-timer phase on the enclosing PYGIM_TIMED_SCOPE timer.
// Starting a new sub-timer automatically stops the previous one.
#define PYGIM_TIMED_SUB(phase) pygim_timer_.start_sub_timer((phase), false)

#else

#define PYGIM_SCOPE_LOG()                   ((void)0)
#define PYGIM_SCOPE_LOG_MSG(note)           ((void)0)
#define PYGIM_SCOPE_LOG_TAG(tag)            ((void)0)
#define PYGIM_SCOPE_LOG_TAG_MSG(tag, note)  ((void)0)
#define PYGIM_REPOSITORY_SCOPE_LOG()        ((void)0)
#define PYGIM_REPOSITORY_SCOPE_LOG_MSG(n)   ((void)0)

#define PYGIM_LOG_FMT(fmt, ...) ((void)0)

#define PYGIM_TIMED_SCOPE(name) ((void)0)
#define PYGIM_TIMED_SUB(phase)  ((void)0)

#endif
