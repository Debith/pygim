#pragma once

#include <chrono>
#include <iostream>
#include <string_view>

#ifndef PYGIM_SCOPE_LOGGING_ENABLED
#ifdef PYGIM_REPOSITORY_LOGGING_ENABLED
#define PYGIM_SCOPE_LOGGING_ENABLED PYGIM_REPOSITORY_LOGGING_ENABLED
#else
#define PYGIM_SCOPE_LOGGING_ENABLED 0
#endif
#endif

namespace pygim::logging {

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
        if (!m_enabled) {
            return;
        }
        m_start = std::chrono::steady_clock::now();
        write_prefix("enter");
        if (!m_note.empty()) {
            std::clog << " : " << m_note;
        }
        std::clog << std::endl;
    }

    ~ScopeLog() {
        if (!m_enabled) {
            return;
        }
        const auto end = std::chrono::steady_clock::now();
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();
        write_prefix("exit ");
        std::clog << " after " << micros << "us";
        if (!m_note.empty()) {
            std::clog << " : " << m_note;
        }
        std::clog << std::endl;
    }

private:
    void write_prefix(const char *verb) const {
        std::clog << "[pygim";
        if (!m_tag.empty()) {
            std::clog << ':' << m_tag;
        }
        std::clog << "] " << verb << ' ' << m_func
                  << " (" << m_file << ':' << m_line << ')';
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

#if PYGIM_SCOPE_LOGGING_ENABLED
#define PYGIM_SCOPE_LOG()                                                                                               \
    ::pygim::logging::ScopeLog pygim_scope_log_##__COUNTER__(__func__, __FILE__, __LINE__, {}, {})

#define PYGIM_SCOPE_LOG_MSG(note)                                                                                       \
    ::pygim::logging::ScopeLog pygim_scope_log_##__COUNTER__(__func__, __FILE__, __LINE__, {}, (note))

#define PYGIM_SCOPE_LOG_TAG(tag)                                                                                        \
    ::pygim::logging::ScopeLog pygim_scope_log_##__COUNTER__(__func__, __FILE__, __LINE__, (tag), {})

#define PYGIM_SCOPE_LOG_TAG_MSG(tag, note)                                                                              \
    ::pygim::logging::ScopeLog pygim_scope_log_##__COUNTER__(__func__, __FILE__, __LINE__, (tag), (note))

#define PYGIM_REPOSITORY_SCOPE_LOG() PYGIM_SCOPE_LOG_TAG("repo")
#define PYGIM_REPOSITORY_SCOPE_LOG_MSG(note) PYGIM_SCOPE_LOG_TAG_MSG("repo", (note))
#else
#define PYGIM_SCOPE_LOG() (void)0
#define PYGIM_SCOPE_LOG_MSG(note) (void)0
#define PYGIM_SCOPE_LOG_TAG(tag) (void)0
#define PYGIM_SCOPE_LOG_TAG_MSG(tag, note) (void)0
#define PYGIM_REPOSITORY_SCOPE_LOG() (void)0
#define PYGIM_REPOSITORY_SCOPE_LOG_MSG(note) (void)0
#endif
