// persistence/core/null_load_cache.h
// No-op LoadCache for backends that don't support parallel load.
// Empty class — zero-cost when held in Repository<Backend>.

#pragma once

namespace pygim::core {

/// NullLoadCache — empty placeholder for backends without parallel load support.
/// Occupies zero bytes (empty base / member optimization).
struct NullLoadCache {};

} // namespace pygim::core
