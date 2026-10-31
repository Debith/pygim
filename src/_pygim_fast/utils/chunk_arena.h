#pragma once
// utils/chunk_arena.h — memory handed out from chunks of megabytes and given back all at once, for
// many allocations that live and die together, as a corpus's trees do. Each costs a pointer bump,
// and the C library's per-thread heaps do not grow under them: a fresh process's first code.parse
// took 94 ms where a later one took 52, each heap growing 128 KB at a time with an mprotect that
// stalls every other thread's page faults (D61, 2026-10-30, WSL2, Ryzen 7 5800X).
//
//     chunk_arena arena;   // used by one thread at a time
//     arena_vector<node> nodes(arena_allocator<node>(&arena));
//     arena_string text(arena_allocator<char>(&arena));   // a file read into it (pathlike's reads)
//
// The chunks come from the process's kept blocks (kept_blocks.h) and go back to them when the arena
// dies, so the next arena finds them faulted in. An allocation of a quarter chunk or more gets a
// chunk of its own, so the chunk being filled is not left part empty.
//
// Under AddressSanitizer a chunk is poisoned when taken and each allocation unpoisoned as it is
// handed out, with a redzone after it, so a read past one allocation is reported as it would be on
// the heap; a chunk is unpoisoned before it goes back, as the next user of the block expects.
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "kept_blocks.h"

#if defined(__SANITIZE_ADDRESS__)
#define PYGIM_ARENA_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PYGIM_ARENA_ASAN 1
#endif
#endif
#ifndef PYGIM_ARENA_ASAN
#define PYGIM_ARENA_ASAN 0
#endif
#if PYGIM_ARENA_ASAN
#include <sanitizer/asan_interface.h>
#endif

namespace pygim {

/// Where `bytes` aligned to `align` (a power of two) start in a chunk whose free room is
/// [`at`, `end`), as offsets; nullopt when they do not fit.
[[nodiscard]] constexpr std::optional<std::size_t> fit_in_chunk(std::size_t at, std::size_t end, std::size_t bytes, std::size_t align) noexcept {
    const std::size_t start = (at + align - 1) & ~(align - 1);
    if (start > end || end - start < bytes) return std::nullopt;
    return start;
}
static_assert(fit_in_chunk(0, 64, 8, 8) == 0 && fit_in_chunk(3, 64, 8, 8) == 8 && fit_in_chunk(56, 64, 8, 8) == 56 && !fit_in_chunk(60, 64, 8, 8) &&
              !fit_in_chunk(62, 64, 1, 8) && fit_in_chunk(62, 64, 0, 8) == 64);

class chunk_arena {
public:
    static constexpr std::size_t chunk_bytes = std::size_t{4} << 20;
    /// The bytes left unhanded after each allocation: AddressSanitizer's redzone, none without it.
    static constexpr std::size_t redzone = PYGIM_ARENA_ASAN ? 32 : 0;

    chunk_arena() = default;
    chunk_arena(const chunk_arena&) = delete;
    chunk_arena& operator=(const chunk_arena&) = delete;
    ~chunk_arena() {
        for (const chunk& c : m_chunks) {
            unpoison(c.address(), c.size());
            kept_blocks::process().give(c.address(), c.size());
        }
    }

    /// Room for `bytes` aligned to `align` (at most a cache line), until the arena dies.
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) {
        if (bytes >= chunk_bytes / 4) return handed(take(bytes + redzone).address(), bytes);   // a chunk of its own; the one being filled stays so
        if (m_filling != none) {
            const chunk& c = m_chunks[m_filling];
            if (const auto start = fit_in_chunk(m_used, c.size(), bytes + redzone, align)) {
                m_used = *start + bytes + redzone;
                return handed(c.address() + *start, bytes);
            }
        }
        std::byte* fresh = take(chunk_bytes).address();
        m_filling = m_chunks.size() - 1;
        m_used = bytes + redzone;
        return handed(fresh, bytes);
    }

    /// The bytes its chunks span: address space, most of it never touched.
    [[nodiscard]] std::size_t reserved() const noexcept {
        std::size_t total = 0;
        for (const chunk& c : m_chunks) total += c.size();
        return total;
    }

private:
    using chunk = shelved_block<std::byte*>;
    static constexpr std::size_t none = static_cast<std::size_t>(-1);

    chunk& take(std::size_t bytes) {
        m_chunks.reserve(m_chunks.size() + 1);   // before the block: a vector that cannot grow loses no block
        auto* address = static_cast<std::byte*>(kept_blocks::process().take(bytes));
        poison(address, bytes);
        return m_chunks.emplace_back(address, bytes);
    }
    [[nodiscard]] static void* handed(std::byte* at, std::size_t bytes) noexcept {
        unpoison(at, bytes);
        return at;
    }
    static void poison([[maybe_unused]] std::byte* at, [[maybe_unused]] std::size_t bytes) noexcept {
#if PYGIM_ARENA_ASAN
        ASAN_POISON_MEMORY_REGION(at, bytes);
#endif
    }
    static void unpoison([[maybe_unused]] std::byte* at, [[maybe_unused]] std::size_t bytes) noexcept {
#if PYGIM_ARENA_ASAN
        ASAN_UNPOISON_MEMORY_REGION(at, bytes);
#endif
    }

    std::vector<chunk> m_chunks;
    std::size_t m_filling = none;   // the chunk being filled, by index: the vector's entries move as it grows
    std::size_t m_used = 0;         // of the chunk being filled
};

/// A container's allocator over a chunk_arena, or the heap's (std::allocator) without one — and in a
/// constant evaluation. It moves and swaps with its container, so a container moved from one place
/// to another keeps its memory, and a copy owns memory of its own on the heap. An arena frees
/// nothing until it dies.
template <class T>
class arena_allocator {
public:
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::false_type;

    constexpr arena_allocator() noexcept = default;
    constexpr explicit arena_allocator(chunk_arena* arena) noexcept : m_arena(arena) {}
    template <class U>
    constexpr arena_allocator(const arena_allocator<U>& other) noexcept : m_arena(other.arena()) {}

    [[nodiscard]] constexpr T* allocate(std::size_t n) {
        if (std::is_constant_evaluated() || m_arena == nullptr) return std::allocator<T>{}.allocate(n);
        return static_cast<T*>(m_arena->allocate(n * sizeof(T), alignof(T)));
    }
    constexpr void deallocate(T* p, std::size_t n) noexcept {
        if (std::is_constant_evaluated() || m_arena == nullptr) std::allocator<T>{}.deallocate(p, n);
    }
    [[nodiscard]] constexpr arena_allocator select_on_container_copy_construction() const noexcept { return {}; }

    [[nodiscard]] constexpr chunk_arena* arena() const noexcept { return m_arena; }
    [[nodiscard]] friend constexpr bool operator==(const arena_allocator& a, const arena_allocator& b) noexcept { return a.m_arena == b.m_arena; }

private:
    static_assert(alignof(T) <= static_cast<std::size_t>(kept_blocks::alignment), "a chunk is aligned to a cache line, no further");
    chunk_arena* m_arena = nullptr;
};

/// A vector in a chunk_arena, or on the heap without one.
template <class T>
using arena_vector = std::vector<T, arena_allocator<T>>;
static_assert([] {   // without an arena it is the heap's: a vector grows, copies and moves as std::vector does
    arena_vector<int> v{1, 2, 3};
    auto w = v;
    w.push_back(4);
    const auto moved = std::move(w);
    return v.size() == 3 && moved.size() == 4 && moved[3] == 4 && moved.get_allocator().arena() == nullptr;
}());

/// A string in a chunk_arena, or on the heap without one: the bytes a read puts where its caller
/// says (pathlike's reads), so a corpus's texts are in its arenas and anything else's on the heap,
/// one type that moves between them uncopied. A NUL follows its end, as std::string's does.
using arena_string = std::basic_string<char, std::char_traits<char>, arena_allocator<char>>;
static_assert([] {   // without an arena, a std::string: it grows past the string object, a NUL after its end
    arena_string s("abc");
    s.append(20, 'x');
    const arena_string moved = std::move(s);
    return moved.size() == 23 && moved.c_str()[23] == '\0' && moved.get_allocator().arena() == nullptr;
}());

}  // namespace pygim
