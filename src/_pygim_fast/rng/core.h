#pragma once

// pygim.rng core — pybind-free high-throughput pseudo-random generation.
//
// Engine: 16 interleaved xoshiro256++ streams (Blackman & Vigna). Output
// element i is drawn from stream (i mod 16); the SIMD path evaluates the 16
// streams as 4 AVX2 groups of 4 lanes, and the scalar path replays the exact
// same layout, so results are bit-identical across hardware paths.
//
// Streams are derived on demand from a seed via the SplitMix64 finalizer
// (counter-based, O(1) random access). Output is partitioned into fixed-size
// blocks; block b uses streams [16b, 16b+16), which makes the sequence a pure
// function of the seed — independent of call sizes and thread counts.
//
// Two element types: double (uniform in [0, 1)) and std::uint64_t (the raw
// draw). Every kernel is one template over the element type; value<T> is the
// only place they differ.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>
#define PYGIM_RNG_X86 1
#endif

namespace pygim::rng {

inline constexpr std::size_t kLanes = 16;              // interleaved streams
inline constexpr std::size_t kBlockElems = 1u << 18;   // 262144; multiple of kLanes
static_assert(kBlockElems % kLanes == 0);

inline constexpr std::uint64_t kGolden = 0x9e3779b97f4a7c15ULL;

// Fills at least this large (bytes) use non-temporal stores when the
// destination is 16-byte aligned: beyond LLC capacity the write-allocate
// traffic of regular stores halves effective bandwidth.
inline constexpr std::size_t kNtThresholdBytes = 32u << 20;

// ---------------------------------------------------------------------------
// SplitMix64 (Vigna). The state update is x += kGolden, so the n-th output of
// the stream seeded with x0 is a pure function of (x0, n): counter-based
// random access used to derive per-stream xoshiro states in O(1).
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr std::uint64_t splitmix64_mix(std::uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

[[nodiscard]] constexpr std::uint64_t splitmix64_at(std::uint64_t x0, std::uint64_t n) noexcept {
    return splitmix64_mix(x0 + (n + 1) * kGolden);
}

// ---------------------------------------------------------------------------
// xoshiro256++ (Blackman & Vigna, 2019). Public-domain reference algorithm;
// constants verified against the rand_xoshiro known-answer vectors below.
// ---------------------------------------------------------------------------

struct Xoshiro256pp {
    std::array<std::uint64_t, 4> s{};

    [[nodiscard]] static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

    [[nodiscard]] constexpr std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(s[0] + s[3], 23) + s[0];
        const std::uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
};

// Stream j's initial state: SplitMix64-mixed words, with the stream counter
// injected through its own mix round BEFORE combining with the seed. A plain
// additive window (mix(seed + counter*golden)) has a lattice defect: seeds
// differing by multiples of 4*golden would share whole streams, merely
// re-laned. The nonlinear inner mix breaks that seed/counter symmetry, so
// stream coincidence across seeds requires four simultaneous mixer
// collisions. Streams are statistically independent for all practical
// purposes (sub-2^-200 overlap probability on the 2^256-1 xoshiro cycle).
[[nodiscard]] constexpr Xoshiro256pp derive_stream(std::uint64_t seed_base, std::uint64_t stream) noexcept {
    Xoshiro256pp g{};
    for (std::uint64_t k = 0; k < 4; ++k) {
        g.s[k] = splitmix64_mix(seed_base + splitmix64_mix((stream * 4 + k + 1) * kGolden));
    }
    // Defense-in-depth: the all-zero state is the one forbidden xoshiro
    // state. Provably unreachable here (the mixer is a bijection and the
    // four inner inputs are distinct, so at most one word can be zero);
    // kept in case the derivation ever changes.
    if ((g.s[0] | g.s[1] | g.s[2] | g.s[3]) == 0) {
        g.s[0] = kGolden;
    }
    return g;
}

using StreamArray = std::array<Xoshiro256pp, kLanes>;

[[nodiscard]] inline StreamArray derive_block_streams(std::uint64_t seed_base, std::uint64_t block) noexcept {
    StreamArray streams;
    for (std::size_t lane = 0; lane < kLanes; ++lane) {
        streams[lane] = derive_stream(seed_base, block * kLanes + lane);
    }
    return streams;
}

// ---------------------------------------------------------------------------
// Compile-time known-answer tests.
// Vectors from rust-random/rngs rand_xoshiro, produced with the reference
// implementations at prng.di.unimi.it.
// ---------------------------------------------------------------------------

namespace kat {

constexpr bool xoshiro256pp_reference() {
    Xoshiro256pp g{{1, 2, 3, 4}};
    constexpr std::uint64_t expected[10] = {
        41943041ULL,
        58720359ULL,
        3588806011781223ULL,
        3591011842654386ULL,
        9228616714210784205ULL,
        9973669472204895162ULL,
        14011001112246962877ULL,
        12406186145184390807ULL,
        15849039046786891736ULL,
        10450023813501588000ULL,
    };
    for (std::uint64_t e : expected) {
        if (g.next() != e) {
            return false;
        }
    }
    return true;
}

constexpr bool splitmix64_reference() {
    constexpr std::uint64_t seed = 1477776061723855037ULL;
    constexpr std::uint64_t expected[5] = {
        1985237415132408290ULL,
        2979275885539914483ULL,
        13511426838097143398ULL,
        8488337342461049707ULL,
        15141737807933549159ULL,
    };
    for (std::uint64_t n = 0; n < 5; ++n) {
        if (splitmix64_at(seed, n) != expected[n]) {
            return false;
        }
    }
    return true;
}

static_assert(xoshiro256pp_reference());
static_assert(splitmix64_reference());

}  // namespace kat

// ---------------------------------------------------------------------------
// The element of type T for one raw draw. double takes the top 53 bits scaled
// by 2^-53, numpy's own mapping ((next_uint64 >> 11) * (1.0 / 2^53)), so the
// distribution granularity matches numpy exactly.
// ---------------------------------------------------------------------------

template <typename T>
[[nodiscard]] constexpr T value(std::uint64_t x) noexcept {
    static_assert(std::is_same_v<T, double> || std::is_same_v<T, std::uint64_t>,
                  "elements are double or std::uint64_t");
    if constexpr (std::is_same_v<T, double>) {
        return static_cast<double>(x >> 11) * 0x1.0p-53;
    } else {
        return x;
    }
}

// Scalar kernel. A "row" is one output from each of the 16 streams.
template <typename T>
inline void rows_scalar(StreamArray& st, T* out, std::size_t rows) noexcept {
    for (std::size_t r = 0; r < rows; ++r, out += kLanes) {
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            out[lane] = value<T>(st[lane].next());
        }
    }
}

// ---------------------------------------------------------------------------
// AVX2 kernel: 4 groups x 4 lanes of vertical xoshiro256++, compiled with a
// per-function target attribute so no global -mavx2 flag is required, and
// dispatched at runtime. The uint64->double sequence is the exact-integer
// conversion (Mysticial): exact for v < 2^53, hence bit-identical to the
// scalar path.
// ---------------------------------------------------------------------------

#if defined(PYGIM_RNG_X86)

namespace detail {

// State word k of four streams, one stream per 64-bit lane.
struct alignas(32) LaneGroup {
    __m256i s[4];
};

__attribute__((target("avx2"))) inline __m256i rotl64(__m256i x, int k) noexcept {
    return _mm256_or_si256(_mm256_slli_epi64(x, k), _mm256_srli_epi64(x, 64 - k));
}

__attribute__((target("avx2"))) inline __m256i xoshiro_next(LaneGroup& g) noexcept {
    __m256i* s = g.s;
    const __m256i result = _mm256_add_epi64(rotl64(_mm256_add_epi64(s[0], s[3]), 23), s[0]);
    const __m256i t = _mm256_slli_epi64(s[1], 17);
    s[2] = _mm256_xor_si256(s[2], s[0]);
    s[3] = _mm256_xor_si256(s[3], s[1]);
    s[1] = _mm256_xor_si256(s[1], s[2]);
    s[0] = _mm256_xor_si256(s[0], s[3]);
    s[2] = _mm256_xor_si256(s[2], t);
    s[3] = rotl64(s[3], 45);
    return result;
}

// Exact conversion of v = x >> 11 (< 2^53) to double, then scale by 2^-53.
__attribute__((target("avx2"))) inline __m256d unit_double(__m256i x) noexcept {
    const __m256i v = _mm256_srli_epi64(x, 11);
    const __m256i lo_magic = _mm256_castpd_si256(_mm256_set1_pd(0x1.0p52));
    const __m256i hi_magic = _mm256_castpd_si256(_mm256_set1_pd(0x1.0p84));
    const __m256d sub = _mm256_set1_pd(0x1.0p84 + 0x1.0p52);
    const __m256d scale = _mm256_set1_pd(0x1.0p-53);
    // lo: keep low 32 bits, splice in 2^52 exponent -> double(2^52 + lo)
    const __m256i lo = _mm256_blend_epi32(v, lo_magic, 0b10101010);
    // hi: high 21 bits at a 2^84 anchor -> double(2^84 + hi * 2^32)
    const __m256i hi = _mm256_or_si256(_mm256_srli_epi64(v, 32), hi_magic);
    // (2^84 + hi*2^32) - (2^84 + 2^52) + (2^52 + lo) == hi*2^32 + lo, exactly
    const __m256d d = _mm256_add_pd(
        _mm256_sub_pd(_mm256_castsi256_pd(hi), sub), _mm256_castsi256_pd(lo));
    return _mm256_mul_pd(d, scale);
}

// The bits of value<T> for each of the four draws in x.
template <typename T>
__attribute__((target("avx2"))) inline __m256i value_bits(__m256i x) noexcept {
    if constexpr (std::is_same_v<T, double>) {
        return _mm256_castpd_si256(unit_double(x));
    } else {
        return x;
    }
}

// The 16 streams' states <-> 4 groups: lane j of group g is stream 4g + j.
__attribute__((target("avx2"))) inline void load_groups(const StreamArray& st, LaneGroup* g) noexcept {
    alignas(32) std::uint64_t words[4];
    for (std::size_t grp = 0; grp < 4; ++grp) {
        for (std::size_t k = 0; k < 4; ++k) {
            for (std::size_t lane = 0; lane < 4; ++lane) {
                words[lane] = st[grp * 4 + lane].s[k];
            }
            g[grp].s[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(words));
        }
    }
}

__attribute__((target("avx2"))) inline void store_groups(const LaneGroup* g, StreamArray& st) noexcept {
    alignas(32) std::uint64_t words[4];
    for (std::size_t grp = 0; grp < 4; ++grp) {
        for (std::size_t k = 0; k < 4; ++k) {
            _mm256_store_si256(reinterpret_cast<__m256i*>(words), g[grp].s[k]);
            for (std::size_t lane = 0; lane < 4; ++lane) {
                st[grp * 4 + lane].s[k] = words[lane];
            }
        }
    }
}

}  // namespace detail

// Streaming = non-temporal stores, for fills whose working set exceeds the
// LLC: they skip the read-for-ownership traffic of regular stores, roughly
// halving bus usage. They are 16 bytes wide, so the destination needs only
// 16-byte alignment, which malloc, numpy and array.array buffers all have.
template <typename T, bool Streaming = false>
__attribute__((target("avx2"))) inline void rows_avx2(StreamArray& st, T* out, std::size_t rows) noexcept {
    detail::LaneGroup g[4];
    detail::load_groups(st, g);
    for (std::size_t r = 0; r < rows; ++r, out += kLanes) {
        for (std::size_t grp = 0; grp < 4; ++grp) {
            const __m256i v = detail::value_bits<T>(detail::xoshiro_next(g[grp]));
            if constexpr (Streaming) {
                auto* p = reinterpret_cast<__m128i*>(out + grp * 4);
                _mm_stream_si128(p, _mm256_castsi256_si128(v));
                _mm_stream_si128(p + 1, _mm256_extracti128_si256(v, 1));
            } else {
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + grp * 4), v);
            }
        }
    }
    detail::store_groups(g, st);
    if constexpr (Streaming) {
        _mm_sfence();
    }
}

[[nodiscard]] inline bool cpu_has_avx2() noexcept {
    return __builtin_cpu_supports("avx2") != 0;
}

#else

[[nodiscard]] inline bool cpu_has_avx2() noexcept { return false; }

#endif  // PYGIM_RNG_X86

// ---------------------------------------------------------------------------
// Elements [pos, pos + count) of one block, continuing from live stream
// states: lane by lane up to a row boundary, whole rows through the kernel,
// then the remainder lane by lane.
// ---------------------------------------------------------------------------

template <typename T, typename Rows>
inline void fill_in_block(StreamArray& st, T* out, std::size_t pos, std::size_t count, Rows rows_kernel) noexcept {
    std::size_t i = 0;
    const auto one_by_one = [&](std::size_t end) {
        for (; i < end; ++i, ++pos) {
            out[i] = value<T>(st[pos % kLanes].next());
        }
    };
    one_by_one(std::min(count, (kLanes - pos % kLanes) % kLanes));
    if (const std::size_t rows = (count - i) / kLanes; rows != 0) {
        rows_kernel(st, out + i, rows);
        i += rows * kLanes;
        pos += rows * kLanes;
    }
    one_by_one(count);
}

// ---------------------------------------------------------------------------
// RngCore: seed + position + the live streams of an unfinished block. fill()
// is a pure function of (seed, position, n): call sizes and thread counts do
// not change the emitted sequence.
// ---------------------------------------------------------------------------

class RngCore {
public:
    explicit RngCore(std::uint64_t seed, int threads = 0, bool simd = true) noexcept
        : m_seed_base(seed),
          m_threads(threads),
          m_simd(simd && cpu_has_avx2()) {}

    [[nodiscard]] std::uint64_t seed() const noexcept { return m_seed_base; }
    [[nodiscard]] bool simd_active() const noexcept { return m_simd; }
    [[nodiscard]] int threads_configured() const noexcept { return m_threads; }

    // The next n elements of the sequence: the rest of a block an earlier
    // call started, then whole blocks, then the start of the next block.
    template <typename T>
    void fill(T* out, std::size_t n) {
        // Serialize concurrent fills on the same object: the GIL is released
        // during generation, so two Python threads sharing one generator
        // would otherwise race on m_pos / m_cache (C++ UB, not merely
        // nondeterminism). Uncontended cost is negligible against a fill.
        std::lock_guard<std::mutex> guard(m_state_mutex);
        const std::size_t head = std::min<std::size_t>(n, (kBlockElems - m_pos % kBlockElems) % kBlockElems);
        const std::size_t blocks = (n - head) / kBlockElems;
        fill_partial(out, head);
        fill_blocks(out + head, blocks);
        fill_partial(out + head + blocks * kBlockElems, n - head - blocks * kBlockElems);
    }

private:
    template <typename T>
    void fill_range(StreamArray& st, T* out, std::size_t pos, std::size_t count) const noexcept {
#if defined(PYGIM_RNG_X86)
        if (m_simd) {
            return fill_in_block(st, out, pos, count, rows_avx2<T>);
        }
#endif
        fill_in_block(st, out, pos, count, rows_scalar<T>);
    }

    // count elements within the current block. A block's streams are derived
    // when its first element is drawn and stay in m_cache while it is
    // unfinished, so the next call continues exactly where this one stopped.
    template <typename T>
    void fill_partial(T* out, std::size_t count) noexcept {
        if (count == 0) {
            return;
        }
        const std::size_t pos = static_cast<std::size_t>(m_pos % kBlockElems);
        if (pos == 0) {
            m_cache = derive_block_streams(m_seed_base, m_pos / kBlockElems);
        }
        fill_range(m_cache, out, pos, count);
        m_pos += count;
    }

    // Whole blocks from a block boundary, in parallel when there are several.
    // Blocks are independent by construction, so any partitioning yields the
    // same output.
    template <typename T>
    void fill_blocks(T* out, std::size_t blocks) {
        if (blocks == 0) {
            return;
        }
        const std::uint64_t first = m_pos / kBlockElems;
#if defined(PYGIM_RNG_X86)
        const bool streaming = m_simd && blocks * kBlockElems * sizeof(T) >= kNtThresholdBytes &&
                               reinterpret_cast<std::uintptr_t>(out) % 16 == 0;
#endif
        const auto run = [&](std::size_t b0, std::size_t b1) {  // blocks [b0, b1) of this fill
            for (std::size_t b = b0; b < b1; ++b) {
                StreamArray st = derive_block_streams(m_seed_base, first + b);
#if defined(PYGIM_RNG_X86)
                if (streaming) {
                    rows_avx2<T, true>(st, out + b * kBlockElems, kBlockElems / kLanes);
                    continue;
                }
#endif
                fill_range(st, out + b * kBlockElems, 0, kBlockElems);
            }
        };

        const std::size_t want = m_threads > 0 ? static_cast<std::size_t>(m_threads)
                                               : std::max<std::size_t>(1, std::thread::hardware_concurrency());
        const std::size_t nthreads = std::min({want, blocks, std::size_t{256}});
        if (nthreads == 1) {
            run(0, blocks);
        } else {
            // RAII joiner: if a spawn throws mid-loop (std::system_error
            // under thread exhaustion), unwinding joins the started workers
            // instead of calling std::terminate as ~thread would.
            // (std::jthread would do this, but Apple Clang's libc++ does not
            // ship it.)
            std::vector<std::thread> workers;
            struct Joiner {
                std::vector<std::thread>& threads;
                ~Joiner() {
                    for (auto& t : threads) {
                        if (t.joinable()) t.join();
                    }
                }
            } joiner{workers};
            workers.reserve(nthreads);
            for (std::size_t t = 0; t < nthreads; ++t) {
                workers.emplace_back(run, t * blocks / nthreads, (t + 1) * blocks / nthreads);
            }
        }
        m_pos += static_cast<std::uint64_t>(blocks) * kBlockElems;
    }

    std::mutex m_state_mutex;     //!< serializes fills; the GIL is released during generation
    std::uint64_t m_seed_base;    //!< SplitMix64 counter base derived from the user seed
    std::uint64_t m_pos = 0;      //!< elements emitted so far
    StreamArray m_cache{};        //!< live streams of the current block while it is unfinished
    int m_threads;                //!< configured threads (0 = auto)
    bool m_simd;                  //!< AVX2 path active
};

}  // namespace pygim::rng
