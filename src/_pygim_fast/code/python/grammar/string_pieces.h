#pragma once
// code/python/grammar/string_pieces.h — the text pieces of the strings being read, before they are
// a literal: strings side by side and the text runs of an f-string between its fields, merged into
// one Constant as CPython merges them. Innermost string last: an f-string's field may hold another.
//
// CORE layer: pybind-free.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "../output.h"
#include "vocabulary.h"

namespace pygim::code::python::grammar {

class string_pieces {
public:
    /// Text pieces read but not yet a Constant: they become one when a field or the end comes.
    class pending_text {
    public:
        explicit pending_text(std::uint32_t base) noexcept : m_base(base) {}
        [[nodiscard]] std::uint32_t base() const noexcept { return m_base; }
        [[nodiscard]] std::uint32_t start() const noexcept { return m_start; }
        [[nodiscard]] std::uint32_t end() const noexcept { return m_end; }
        [[nodiscard]] bool unicode() const noexcept { return m_unicode; }
        [[nodiscard]] bool empty() const noexcept { return m_start == no_value; }
        void extend(std::uint32_t start, std::uint32_t end, bool unicode) noexcept {
            if (m_start == no_value) {
                m_start = start;
                m_unicode = unicode;
            }
            m_end = end;
        }
        void reset() noexcept { m_start = no_value; m_unicode = false; }

    private:
        std::uint32_t m_base;
        std::uint32_t m_start = no_value, m_end = 0;
        bool m_unicode = false;
    };

    /// A string's text, starting after the pieces pending now.
    [[nodiscard]] pending_text open() const noexcept { return pending_text(static_cast<std::uint32_t>(m_pending.size())); }

    void add(pending_text& p, std::uint32_t content_start, std::uint32_t content_end, std::uint8_t flags, std::uint32_t span_start, std::uint32_t span_end, bool unicode) {
        m_pending.emplace_back(content_start, content_end, flags);
        p.extend(span_start, span_end, unicode);
    }

    /// The pending pieces as one literal: its index.
    std::uint32_t commit(parse_output& out, pending_text& p) {
        const std::uint32_t lit = out.add_literal(std::span<const literal_piece>(m_pending).subspan(p.base()));
        m_pending.resize(p.base());
        return lit;
    }

    /// The pending text of an f-string as a Constant in `field`, put in at row `at` (or appended when
    /// `at` is the end); nothing when it reads as empty. Whether a row went in.
    bool flush(std::string_view text, parse_output& out, pending_text& p, std::uint32_t at, std::uint8_t field, bool bytes) {
        if (p.empty()) return false;
        if (blank(text, p.base())) {
            m_pending.resize(p.base());
            p.reset();
            return false;
        }
        const std::uint16_t small = static_cast<std::uint16_t>((bytes ? constant_kind::bytes : constant_kind::string)) | (p.unicode() ? unicode_prefix : 0);
        const row r(kind::Constant, field, small, 1, commit(out, p), p.start(), p.end());
        row_writer& rows = out.rows();
        if (at == rows.mark()) rows.leaf(kind::Constant, field, small, r.value(), r.start(), r.end());
        else rows.insert(at, r);
        p.reset();
        return true;
    }

    /// How many pieces are pending, for a checkpoint; and back to that many.
    [[nodiscard]] std::size_t size() const noexcept { return m_pending.size(); }
    void resize(std::size_t count) { m_pending.resize(count); }

private:
    // Whether pieces from `base` read as no characters at all (only line continuations, or none).
    [[nodiscard]] bool blank(std::string_view text, std::uint32_t base) const noexcept {
        for (std::size_t i = base; i < m_pending.size(); ++i) {
            const literal_piece& lp = m_pending[i];
            if (lp.has(piece_flag::verbatim) || lp.has(piece_flag::raw)) {
                if (lp.end() > lp.start()) return false;
                continue;
            }
            for (std::uint32_t k = lp.start(); k < lp.end();) {
                if (text[k] != '\\') return false;
                const char n = text[k + 1];
                if (n == '\n') k += 2;
                else if (n == '\r') k += (text[k + 2] == '\n') ? 3 : 2;
                else return false;
            }
        }
        return true;
    }

    std::vector<literal_piece> m_pending;
};

}  // namespace pygim::code::python::grammar
