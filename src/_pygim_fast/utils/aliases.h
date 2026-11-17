// utils/aliases.h — the repo's names for standard types that recur across headers, so a signature says
// what a value is rather than how std:: spells it (global #102). A base header: the standard library only.
//
// The rule is a hybrid (Debith, PR #38 review, 2026-10-19). A type that recurs across the repo is named
// here, once. A type one header uses in a role of its own is aliased in that header, by its role —
// `using MemberNames = pygim::Strings;` — so this file stays the shared vocabulary, not a dump of every
// alias anywhere.

#pragma once

#include <string>
#include <vector>

namespace pygim {

using Strings = std::vector<std::string>;  // a list of texts: joined, sorted, returned as names

}  // namespace pygim
