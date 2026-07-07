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
// destination is 32-byte aligned: beyond LLC capacity the write-allocate
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
// Conversions. random(): top 53 bits scaled by 2^-53 — the same mapping numpy
// uses ((next_uint64 >> 11) * (1.0 / 9007199254740992.0)), so distribution
// granularity matches numpy exactly.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr double u64_to_unit_double(std::uint64_t x) noexcept {
    return static_cast<double>(x >> 11) * 0x1.0p-53;
}

// ---------------------------------------------------------------------------
// Scalar kernels. A "row" is one output from each of the 16 streams.
// ---------------------------------------------------------------------------

inline void rows_scalar_f64(StreamArray& st, double* out, std::size_t rows) noexcept {
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            out[r * kLanes + lane] = u64_to_unit_double(st[lane].next());
        }
    }
}

inline void rows_scalar_u64(StreamArray& st, std::uint64_t* out, std::size_t rows) noexcept {
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            out[r * kLanes + lane] = st[lane].next();
        }
    }
}

// ---------------------------------------------------------------------------
// AVX2 kernels: 4 groups x 4 lanes of vertical xoshiro256++, compiled with a
// per-function target attribute so no global -mavx2 flag is required, and
// dispatched at runtime. The uint64->double sequence is the exact-integer
// conversion (Mysticial): exact for v < 2^53, hence bit-identical to the
// scalar path.
// ---------------------------------------------------------------------------

#if defined(PYGIM_RNG_X86)

namespace detail {

struct alignas(32) LaneGroup {
    __m256i s0, s1, s2, s3;
};

__attribute__((target("avx2"))) inline __m256i rotl64(__m256i x, int k) noexcept {
    return _mm256_or_si256(_mm256_slli_epi64(x, k), _mm256_srli_epi64(x, 64 - k));
}

__attribute__((target("avx2"))) inline __m256i xoshiro_next(LaneGroup& g) noexcept {
    const __m256i result =
        _mm256_add_epi64(rotl64(_mm256_add_epi64(g.s0, g.s3), 23), g.s0);
    const __m256i t = _mm256_slli_epi64(g.s1, 17);
    g.s2 = _mm256_xor_si256(g.s2, g.s0);
    g.s3 = _mm256_xor_si256(g.s3, g.s1);
    g.s1 = _mm256_xor_si256(g.s1, g.s2);
    g.s0 = _mm256_xor_si256(g.s0, g.s3);
    g.s2 = _mm256_xor_si256(g.s2, t);
    g.s3 = rotl64(g.s3, 45);
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

__attribute__((target("avx2"))) inline void load_groups(const StreamArray& st, LaneGroup* g) noexcept {
    for (std::size_t grp = 0; grp < 4; ++grp) {
        const auto& a = st[grp * 4 + 0].s;
        const auto& b = st[grp * 4 + 1].s;
        const auto& c = st[grp * 4 + 2].s;
        const auto& d = st[grp * 4 + 3].s;
        g[grp].s0 = _mm256_set_epi64x(static_cast<long long>(d[0]), static_cast<long long>(c[0]),
                                      static_cast<long long>(b[0]), static_cast<long long>(a[0]));
        g[grp].s1 = _mm256_set_epi64x(static_cast<long long>(d[1]), static_cast<long long>(c[1]),
                                      static_cast<long long>(b[1]), static_cast<long long>(a[1]));
        g[grp].s2 = _mm256_set_epi64x(static_cast<long long>(d[2]), static_cast<long long>(c[2]),
                                      static_cast<long long>(b[2]), static_cast<long long>(a[2]));
        g[grp].s3 = _mm256_set_epi64x(static_cast<long long>(d[3]), static_cast<long long>(c[3]),
                                      static_cast<long long>(b[3]), static_cast<long long>(a[3]));
    }
}

__attribute__((target("avx2"))) inline void store_groups(const LaneGroup* g, StreamArray& st) noexcept {
    alignas(32) std::uint64_t tmp[4];
    for (std::size_t grp = 0; grp < 4; ++grp) {
        const __m256i regs[4] = {g[grp].s0, g[grp].s1, g[grp].s2, g[grp].s3};
        for (std::size_t k = 0; k < 4; ++k) {
            _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), regs[k]);
            for (std::size_t lane = 0; lane < 4; ++lane) {
                st[grp * 4 + lane].s[k] = tmp[lane];
            }
        }
    }
}

}  // namespace detail

__attribute__((target("avx2"))) inline void rows_avx2_f64(StreamArray& st, double* out, std::size_t rows) noexcept {
    detail::LaneGroup g[4];
    detail::load_groups(st, g);
    for (std::size_t r = 0; r < rows; ++r) {
        double* row = out + r * kLanes;
        _mm256_storeu_pd(row + 0, detail::unit_double(detail::xoshiro_next(g[0])));
        _mm256_storeu_pd(row + 4, detail::unit_double(detail::xoshiro_next(g[1])));
        _mm256_storeu_pd(row + 8, detail::unit_double(detail::xoshiro_next(g[2])));
        _mm256_storeu_pd(row + 12, detail::unit_double(detail::xoshiro_next(g[3])));
    }
    detail::store_groups(g, st);
}

__attribute__((target("avx2"))) inline void rows_avx2_u64(StreamArray& st, std::uint64_t* out, std::size_t rows) noexcept {
    detail::LaneGroup g[4];
    detail::load_groups(st, g);
    for (std::size_t r = 0; r < rows; ++r) {
        std::uint64_t* row = out + r * kLanes;
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(row + 0), detail::xoshiro_next(g[0]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(row + 4), detail::xoshiro_next(g[1]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(row + 8), detail::xoshiro_next(g[2]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(row + 12), detail::xoshiro_next(g[3]));
    }
    detail::store_groups(g, st);
}

// Non-temporal variants for fills whose working set exceeds the LLC: stream
// stores bypass the write-allocate read-for-ownership traffic, roughly
// halving bus usage. Caller must guarantee 32-byte alignment of `out`.

__attribute__((target("avx2"))) inline void rows_avx2_f64_nt(StreamArray& st, double* out, std::size_t rows) noexcept {
    detail::LaneGroup g[4];
    detail::load_groups(st, g);
    for (std::size_t r = 0; r < rows; ++r) {
        double* row = out + r * kLanes;
        _mm256_stream_pd(row + 0, detail::unit_double(detail::xoshiro_next(g[0])));
        _mm256_stream_pd(row + 4, detail::unit_double(detail::xoshiro_next(g[1])));
        _mm256_stream_pd(row + 8, detail::unit_double(detail::xoshiro_next(g[2])));
        _mm256_stream_pd(row + 12, detail::unit_double(detail::xoshiro_next(g[3])));
    }
    detail::store_groups(g, st);
    _mm_sfence();
}

__attribute__((target("avx2"))) inline void rows_avx2_u64_nt(StreamArray& st, std::uint64_t* out, std::size_t rows) noexcept {
    detail::LaneGroup g[4];
    detail::load_groups(st, g);
    for (std::size_t r = 0; r < rows; ++r) {
        std::uint64_t* row = out + r * kLanes;
        _mm256_stream_si256(reinterpret_cast<__m256i*>(row + 0), detail::xoshiro_next(g[0]));
        _mm256_stream_si256(reinterpret_cast<__m256i*>(row + 4), detail::xoshiro_next(g[1]));
        _mm256_stream_si256(reinterpret_cast<__m256i*>(row + 8), detail::xoshiro_next(g[2]));
        _mm256_stream_si256(reinterpret_cast<__m256i*>(row + 12), detail::xoshiro_next(g[3]));
    }
    detail::store_groups(g, st);
    _mm_sfence();
}

[[nodiscard]] inline bool cpu_has_avx2() noexcept {
    return __builtin_cpu_supports("avx2") != 0;
}

#else

[[nodiscard]] inline bool cpu_has_avx2() noexcept { return false; }

#endif  // PYGIM_RNG_X86

// ---------------------------------------------------------------------------
// Block-range fill: any [start_in_block, start_in_block + count) range within
// one block, continuing from live stream states. Misaligned head and tail run
// scalar; aligned full rows use the selected kernel.
// ---------------------------------------------------------------------------

namespace detail {

template <typename T, typename RowKernel, typename OneFn>
inline void fill_range_in_block(StreamArray& st, T* out, std::size_t start_in_block,
                                std::size_t count, RowKernel&& rows_kernel, OneFn&& one) {
    std::size_t pos = start_in_block;
    std::size_t produced = 0;
    // Head: advance lane-by-lane until row-aligned.
    while (produced < count && (pos % kLanes) != 0) {
        out[produced++] = one(st[pos % kLanes]);
        ++pos;
    }
    // Body: whole rows through the kernel.
    const std::size_t rows = (count - produced) / kLanes;
    if (rows != 0) {
        rows_kernel(st, out + produced, rows);
        produced += rows * kLanes;
        pos += rows * kLanes;
    }
    // Tail.
    while (produced < count) {
        out[produced++] = one(st[pos % kLanes]);
        ++pos;
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// RngCore: seed + absolute position + resume cache. generate() is a pure
// function of (seed, abs_pos, n) — call sizes and thread counts do not change
// the emitted sequence.
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

    void fill_f64(double* out, std::size_t n) { generate<double>(out, n); }
    void fill_u64(std::uint64_t* out, std::size_t n) { generate<std::uint64_t>(out, n); }

private:
    template <typename T>
    void fill_block_range(StreamArray& st, T* out, std::size_t start_in_block, std::size_t count) {
        auto one = [](Xoshiro256pp& s) {
            if constexpr (std::is_same_v<T, double>) {
                return u64_to_unit_double(s.next());
            } else {
                return s.next();
            }
        };
#if defined(PYGIM_RNG_X86)
        if (m_simd) {
            if constexpr (std::is_same_v<T, double>) {
                detail::fill_range_in_block(st, out, start_in_block, count, rows_avx2_f64, one);
            } else {
                detail::fill_range_in_block(st, out, start_in_block, count, rows_avx2_u64, one);
            }
            return;
        }
#endif
        if constexpr (std::is_same_v<T, double>) {
            detail::fill_range_in_block(st, out, start_in_block, count, rows_scalar_f64, one);
        } else {
            detail::fill_range_in_block(st, out, start_in_block, count, rows_scalar_u64, one);
        }
    }

    // Defensive resume path: unreachable through the public API today (the
    // trailing-partial stage always leaves a valid cache), kept so a future
    // refactor that drops the cache degrades to slow-but-correct.
    static void fast_forward(StreamArray& st, std::size_t in_block) noexcept {
        const std::size_t rows = in_block / kLanes;
        const std::size_t extra = in_block % kLanes;
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            const std::size_t steps = rows + (lane < extra ? 1 : 0);
            for (std::size_t k = 0; k < steps; ++k) {
                (void)st[lane].next();
            }
        }
    }

    template <typename T>
    void generate(T* out, std::size_t n) {
        // Serialize concurrent fills on the same object: the GIL is released
        // during generation, so two Python threads sharing one generator
        // would otherwise race on m_abs_pos / m_cache (C++ UB, not merely
        // nondeterminism). Uncontended cost is negligible against a fill.
        std::lock_guard<std::mutex> guard(m_state_mutex);
        std::size_t produced = 0;

        // 1) Leading partial block: continue from the resume cache.
        std::size_t in_block = static_cast<std::size_t>(m_abs_pos % kBlockElems);
        if (in_block != 0 && n > 0) {
            const std::uint64_t block = m_abs_pos / kBlockElems;
            if (!m_cache_valid) {
                m_cache = derive_block_streams(m_seed_base, block);
                fast_forward(m_cache, in_block);
                m_cache_valid = true;
            }
            const std::size_t take = std::min(n, kBlockElems - in_block);
            fill_block_range(m_cache, out, in_block, take);
            produced += take;
            m_abs_pos += take;
            if (m_abs_pos % kBlockElems == 0) {
                m_cache_valid = false;
            }
        }

        // 2) Full blocks, optionally in parallel. Blocks are independent by
        //    construction, so any partitioning yields the same output.
        const std::size_t remaining = n - produced;
        const std::uint64_t first_block = m_abs_pos / kBlockElems;
        const std::size_t full_blocks = remaining / kBlockElems;
        if (full_blocks != 0) {
            const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
            const std::size_t want =
                m_threads > 0 ? static_cast<std::size_t>(m_threads) : static_cast<std::size_t>(hw);
            const std::size_t nthreads = std::min({want, full_blocks, std::size_t{256}});

#if defined(PYGIM_RNG_X86)
            const bool use_nt = m_simd &&
                (full_blocks * kBlockElems * sizeof(T)) >= kNtThresholdBytes &&
                (reinterpret_cast<std::uintptr_t>(out + produced) % 32) == 0;
#else
            constexpr bool use_nt = false;
#endif

            auto run_blocks = [&](std::uint64_t b0, std::uint64_t b1, T* dst) {
                for (std::uint64_t b = b0; b < b1; ++b) {
                    StreamArray st = derive_block_streams(m_seed_base, b);
                    T* block_out = dst + (b - b0) * kBlockElems;
#if defined(PYGIM_RNG_X86)
                    if (use_nt) {
                        if constexpr (std::is_same_v<T, double>) {
                            rows_avx2_f64_nt(st, block_out, kBlockElems / kLanes);
                        } else {
                            rows_avx2_u64_nt(st, block_out, kBlockElems / kLanes);
                        }
                        continue;
                    }
#endif
                    fill_block_range(st, block_out, 0, kBlockElems);
                }
            };

            T* base = out + produced;
            if (nthreads <= 1 || full_blocks < 2) {
                run_blocks(first_block, first_block + full_blocks, base);
            } else {
                // RAII joiner: if a spawn throws mid-loop (std::system_error
                // under thread exhaustion), unwinding joins the started
                // workers instead of calling std::terminate as ~thread
                // would. (std::jthread would do this, but Apple Clang's
                // libc++ does not ship it.)
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
                const std::size_t per = full_blocks / nthreads;
                const std::size_t rem = full_blocks % nthreads;
                std::size_t offset_blocks = 0;
                for (std::size_t t = 0; t < nthreads; ++t) {
                    const std::size_t nb = per + (t < rem ? 1 : 0);
                    const std::uint64_t b0 = first_block + offset_blocks;
                    T* dst = base + offset_blocks * kBlockElems;
                    workers.emplace_back(
                        [&, b0, nb, dst] { run_blocks(b0, b0 + nb, dst); });
                    offset_blocks += nb;
                }
                for (auto& w : workers) {
                    w.join();
                }
            }
            produced += full_blocks * kBlockElems;
            m_abs_pos += static_cast<std::uint64_t>(full_blocks) * kBlockElems;
        }

        // 3) Trailing partial block: fill and retain the cache for resume.
        const std::size_t tail = n - produced;
        if (tail != 0) {
            m_cache = derive_block_streams(m_seed_base, m_abs_pos / kBlockElems);
            fill_block_range(m_cache, out + produced, 0, tail);
            m_cache_valid = true;
            m_abs_pos += tail;
        }
    }

    std::mutex m_state_mutex;       //!< serializes fills; the GIL is released during generation
    std::uint64_t m_seed_base;      //!< SplitMix64 counter base derived from the user seed
    std::uint64_t m_abs_pos = 0;    //!< absolute elements emitted so far
    StreamArray m_cache{};          //!< live stream states of the current partial block
    bool m_cache_valid = false;     //!< whether m_cache continues the current block
    int m_threads;                  //!< configured threads (0 = auto)
    bool m_simd;                    //!< AVX2 path active
};

}  // namespace pygim::rng
