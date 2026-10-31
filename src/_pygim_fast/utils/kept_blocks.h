#pragma once
// utils/kept_blocks.h — the large blocks a process frees, kept for its next allocation of their
// size, so a call that runs again finds its memory already faulted in.
//
// CORE layer: pybind-free. The C library hands a freed block of megabytes back to the kernel, and
// the next block of that size faults in again, page by page: a ranked query on CPython's Lib/test
// faulted 55 MB — 14,000 pages, a fifth of its CPU time in the kernel — every time it ran
// (2026-10-29, WSL2, Ryzen 7 5800X). With its blocks kept it faulted about 100 pages, and ran in
// 22-24 ms instead of 27-32.
//
// A block of kept_blocks::smallest bytes or more is allocated at its size class (block_size: its
// size rounded up to one of eight steps between two powers of two), and freed onto the process's
// shelf (block_shelf), which gives it to the next allocation of that class. The shelf holds at
// most kept_blocks::held_at_most bytes, dropping its oldest blocks to take a new one; a block
// larger than that is freed at once. Rounding up costs address space, not memory: the pages past
// what a caller writes are never touched.
//
//     kept_vector<match> pairs;            // grows through kept blocks once a megabyte or more
//     unfilled_vector<std::uint32_t> at;   // the same, and its resize writes nothing (unfilled.h)

#include <algorithm>
#include <bit>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace pygim {

/// The size a kept block of at least `bytes` bytes is allocated at: `bytes` rounded up to the next
/// of eight steps between two powers of two, so one size class serves every request within an
/// eighth of it, and wastes at most an eighth of its address space.
[[nodiscard]] constexpr std::size_t block_size(std::size_t bytes) noexcept {
    const std::size_t step = std::max<std::size_t>(std::bit_floor(bytes) / 8, 1);
    return (bytes + step - 1) / step * step;
}
static_assert(block_size(std::size_t{1} << 20) == std::size_t{1} << 20 && block_size((std::size_t{1} << 20) + 1) == std::size_t{9} << 17 &&
              block_size(3) == 3 && block_size(0) == 0);

/// One block on a shelf: where it is, and its size.
template <class Address>
class shelved_block {
public:
    constexpr shelved_block(Address address, std::size_t size) noexcept : m_address(address), m_size(size) {}

    [[nodiscard]] constexpr Address address() const noexcept { return m_address; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }

private:
    Address m_address;
    std::size_t m_size;
};
static_assert(shelved_block<int>(7, 3).address() == 7 && shelved_block<int>(7, 3).size() == 3);

/// Which freed blocks are kept, oldest first, at most `capacity` bytes of them: the decisions of
/// kept_blocks, apart from the memory itself, so constant evaluation proves them over any
/// `Address`. It neither allocates nor frees a block — it hands back the ones to free.
template <class Address>
class block_shelf {
public:
    using block = shelved_block<Address>;

    constexpr explicit block_shelf(std::size_t capacity) noexcept : m_capacity(capacity) {}

    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return m_capacity; }
    /// The bytes on the shelf.
    [[nodiscard]] constexpr std::size_t held() const noexcept { return m_held; }

    /// The newest block of exactly `size` bytes, taken off the shelf; none if it holds no such block.
    [[nodiscard]] constexpr std::optional<Address> take(std::size_t size) {
        const auto it = std::find_if(m_blocks.rbegin(), m_blocks.rend(), [size](const block& b) { return b.size() == size; });
        if (it == m_blocks.rend()) return std::nullopt;
        const Address found = it->address();
        m_held -= size;
        m_blocks.erase(std::next(it).base());
        return found;
    }

    /// Puts `b` on the shelf, dropping the oldest blocks that no longer fit beside it; returns what
    /// is to be freed — those, or `b` itself when it is larger than the shelf holds.
    [[nodiscard]] constexpr std::vector<block> keep(block b) {
        if (b.size() > m_capacity) return {b};
        std::size_t oldest = 0, held = m_held;
        while (held + b.size() > m_capacity) held -= m_blocks[oldest++].size();
        const auto first_kept = m_blocks.begin() + static_cast<std::ptrdiff_t>(oldest);
        std::vector<block> dropped(m_blocks.begin(), first_kept);   // what may throw comes first: nothing has changed yet
        m_blocks.push_back(b);
        m_blocks.erase(m_blocks.begin(), m_blocks.begin() + static_cast<std::ptrdiff_t>(oldest));
        m_held = held + b.size();
        return dropped;
    }

private:
    std::size_t m_capacity;
    std::size_t m_held = 0;
    std::vector<block> m_blocks;   // oldest first
};
static_assert([] {   // a block comes back at its size; the oldest makes room; one too large is never kept
    block_shelf<int> s(10);
    const bool nothing_yet = !s.take(4);
    const bool kept = s.keep({1, 4}).empty() && s.keep({2, 4}).empty();
    const auto dropped = s.keep({3, 4});        // 12 > 10: block 1 goes
    const bool too_large = s.keep({4, 11}).size() == 1;
    const auto newest = s.take(4);
    return nothing_yet && kept && dropped.size() == 1 && dropped[0].address() == 1 && too_large && newest == 3 && s.held() == 4;
}());

/// The process's shelf of freed blocks and the memory under it; see the file comment. Safe from
/// any thread, and across a fork: the child's copy of the lock is never left held.
class kept_blocks {
public:
    /// Below this, the C library's own bins serve faster than a lock.
    static constexpr std::size_t smallest = std::size_t{1} << 20;
    /// The shelf's capacity: a query's working set on a corpus of CPython's or LLVM's size.
    static constexpr std::size_t held_at_most = std::size_t{256} << 20;
    /// Every kept block starts on a cache line, so the first values of two never share one.
    static constexpr std::align_val_t alignment{64};

    /// The process's: created at first use and never destroyed, since a block freed by a static's
    /// destructor at exit may arrive after any destructor of its own.
    [[nodiscard]] static kept_blocks& process() {
        static kept_blocks* const blocks = new kept_blocks();
        return *blocks;
    }

    /// A block of block_size(bytes) bytes: the newest kept one of that size, or a new one.
    [[nodiscard]] void* take(std::size_t bytes) {
        const std::size_t size = block_size(bytes);
        {
            std::lock_guard lock(m_lock);
            if (const std::optional<void*> kept = m_shelf.take(size)) return *kept;
        }
        return ::operator new(size, alignment);
    }

    /// Takes back a block take(bytes) gave: kept, or freed — it, or the oldest it displaces,
    /// outside the lock. Out of memory to note it, it frees the block.
    void give(void* p, std::size_t bytes) noexcept {
        const std::size_t size = block_size(bytes);
        std::vector<block_shelf<void*>::block> freed;
        try {
            std::lock_guard lock(m_lock);
            freed = m_shelf.keep({p, size});   // leaves the shelf as it was if it throws
        } catch (...) {
            ::operator delete(p, size, alignment);
            return;
        }
        for (const auto& b : freed) ::operator delete(b.address(), b.size(), alignment);
    }

    /// The bytes on the shelf now.
    [[nodiscard]] std::size_t held() {
        std::lock_guard lock(m_lock);
        return m_shelf.held();
    }

private:
    kept_blocks() {
#if defined(__unix__) || defined(__APPLE__)
        // held across the fork, released in both processes: a fork while another thread holds it
        // would leave the child's copy held forever
        ::pthread_atfork([] { process().m_lock.lock(); }, [] { process().m_lock.unlock(); }, [] { process().m_lock.unlock(); });
#endif
    }

    std::mutex m_lock;
    block_shelf<void*> m_shelf{held_at_most};
};

/// std::allocator, except that blocks of kept_blocks::smallest bytes or more come from and return
/// to the process's kept blocks. In a constant evaluation, std::allocator itself.
template <class T>
class kept_allocator {
public:
    using value_type = T;

    constexpr kept_allocator() noexcept = default;
    template <class U>
    constexpr kept_allocator(const kept_allocator<U>&) noexcept {}

    [[nodiscard]] constexpr T* allocate(std::size_t n) {
        if !consteval {
            if (kept(n)) return static_cast<T*>(kept_blocks::process().take(n * sizeof(T)));
        }
        return std::allocator<T>().allocate(n);
    }
    constexpr void deallocate(T* p, std::size_t n) noexcept {
        if !consteval {
            if (kept(n)) {
                kept_blocks::process().give(p, n * sizeof(T));
                return;
            }
        }
        std::allocator<T>().deallocate(p, n);
    }

    template <class U>
    [[nodiscard]] friend constexpr bool operator==(const kept_allocator&, const kept_allocator<U>&) noexcept { return true; }

private:
    static_assert(alignof(T) <= static_cast<std::size_t>(kept_blocks::alignment), "a kept block is aligned to a cache line, no further");
    [[nodiscard]] static constexpr bool kept(std::size_t n) noexcept { return n * sizeof(T) >= kept_blocks::smallest; }
};

/// A vector whose blocks of a megabyte or more are kept for the next (see the file comment).
template <class T>
using kept_vector = std::vector<T, kept_allocator<T>>;

static_assert([] {
    kept_vector<int> v{1, 2, 3};
    v.push_back(4);
    return v.size() == 4 && v[3] == 4;
}());

}  // namespace pygim
