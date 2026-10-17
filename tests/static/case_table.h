// tests/static/case_table.h — compile-time proofs written as case tables.
//
// A table is an array of rows; a row is one call — its arguments, then its expected result, then
// a case id (global #118: argument columns, expected outcomes last). first_failing_row(rows, holds)
// answers the index of the first row whose call does not give its expected result, so one
//
//     static_assert(first_failing_row(mangled_rows, ...) == every_row_holds);
//
// proves the whole table and, when a row breaks, names it: the compiler prints what the comparison
// reduced to, e.g. "the comparison reduces to '(3 == 18446744073709551615)'" — row 3.
//
// Lifetime and threading: consteval only; nothing exists at run time.

#pragma once

#include <cstddef>

namespace pygim::proofs {

/// What first_failing_row() answers when every row gives its expected result.
inline constexpr std::size_t every_row_holds = static_cast<std::size_t>(-1);

/// The index of the first row for which `holds(row)` is false, or every_row_holds.
/// Example: first_failing_row(rows, [](const auto& r) { return f(r.arg) == r.expected; }).
template <class Rows, class Holds>
consteval std::size_t first_failing_row(const Rows& rows, Holds holds) {
    std::size_t index = 0;
    for (const auto& row : rows) {
        if (!holds(row)) return index;
        ++index;
    }
    return every_row_holds;
}

}  // namespace pygim::proofs
