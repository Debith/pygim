#pragma once
// code/python/grammar/targets.h — an expression made a target in place: its context set to Store
// or Del down through its tuples, lists and starred parts, and refused where it cannot be one
// (#280: parse a superset, then check). An assignment's targets are parsed as expressions first.
//
// CORE layer: pybind-free.

#include <cstdint>
#include <string>

#include "vocabulary.h"

namespace pygim::code::python::grammar {

/// Row `r`, the root of an expression, made a target of context `ctx` (store or del) in place.
template <class Errors>
void make_target(row_writer& rows, std::uint32_t r, std::uint16_t ctx) {
    const row& x = rows[r];
    switch (x.kind()) {
        case kind::Name:
        case kind::Attribute:
        case kind::Subscript: rows.set_context(r, ctx); return;
        case kind::Starred:
            if (ctx == del) Errors::fail("cannot delete starred", x.start());
            rows.set_context(r, ctx);
            make_target<Errors>(rows, r - 1, ctx);
            return;
        case kind::Tuple:
        case kind::List:
            rows.set_context(r, ctx);
            for_each_root(rows, r + 1 - x.size(), r, [&](std::uint32_t c) { make_target<Errors>(rows, c, ctx); });
            return;
        default:
            Errors::fail(std::string(ctx == del ? "cannot delete " : "cannot assign to ") + std::string(describe_kind(x)), x.start());
    }
}

}  // namespace pygim::code::python::grammar
