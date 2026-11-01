#pragma once
// code/python/grammar/future_imports.h — the `from __future__` imports a file may open with, after
// its docstring: each feature known, as CPython checks them even when it only parses.
//
//     future_imports opens;
//     while (...) { statement(); if (opens.open()) opens.check<Errors>(rows, names, line, rows.mark()); }
//
// CORE layer: pybind-free.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "vocabulary.h"

namespace pygim::code::python::grammar {

/// Where a file is in what may open it: a docstring first, then `from __future__` imports, then
/// anything — after which nothing is checked.
class future_imports {
public:
    [[nodiscard]] bool open() const noexcept { return m_open; }

    /// The statements of one line, rows [first, end): each one either keeps the opening open (the
    /// docstring, a `from __future__` import of known features) or closes it. Checked as each line
    /// closes, the window ends at the first other statement: a file's whole body was gathered into
    /// a vector first.
    template <class Errors, class Names>
    void check(const row_writer& rows, const Names& names, std::uint32_t first, std::uint32_t end) {
        static constexpr std::array<std::string_view, 10> known{"nested_scopes", "generators", "division", "absolute_import", "with_statement",
                                                                "print_function", "unicode_literals", "barry_as_FLUFL", "generator_stop", "annotations"};
        for_each_root_in_order(rows, first, end, [&](std::uint32_t at) {
            if (!m_open) return;
            const row& s = rows[at];
            if (m_docstring_allowed && s.kind() == kind::Expr) {
                const row& v = rows[at - 1];
                m_docstring_allowed = false;
                if (!(v.kind() == kind::Constant && constant_of(v.small()) == constant_kind::string)) m_open = false;
                return;
            }
            m_docstring_allowed = false;
            if (s.kind() != kind::ImportFrom || s.value() == no_value || names[s.value()] != "__future__" || s.small() != 0) {
                m_open = false;
                return;
            }
            for_each_root(rows, at + 1 - s.size(), at, [&](std::uint32_t a) {
                const std::string_view f = names[rows[a].value()];
                if (f == "braces") Errors::fail("not a chance", rows[a].start());
                bool found = false;
                for (std::string_view k : known) found = found || k == f;
                if (!found) Errors::fail("future feature " + std::string(f) + " is not defined", rows[a].start());
            });
        });
    }

private:
    bool m_open = true;
    bool m_docstring_allowed = true;
};

}  // namespace pygim::code::python::grammar
