#pragma once
// AVX2 kernels for fixed-width transpose blocks.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "bcp_types.h"

namespace pygim::bcp::simd {

struct Avx2BlockPlan {
    std::size_t start_index{0};
    std::size_t dst_col_offset{0};
};

struct Avx2BlockPlan64 {
    std::size_t start_index{0};
    std::size_t dst_col_offset{0};
};

// We can only use AVX2 intrinsics portably when either:
// - compiling with AVX2 enabled globally (__AVX2__), or
// - using GCC/Clang function-level target("avx2") attributes.
#if defined(__AVX2__) || defined(__GNUC__) || defined(__clang__)
inline constexpr bool kCanBuildAvx2Kernel = true;
#include <immintrin.h>
#else
inline constexpr bool kCanBuildAvx2Kernel = false;
#endif

[[nodiscard]] inline bool is_4byte_contiguous_block(std::span<ColumnBinding*> fixed,
                                                    std::size_t start) noexcept {
    if (start + 8 > fixed.size()) return false;
    const auto base = fixed[start]->staging_offset;
    for (std::size_t j = 0; j < 8; ++j) {
        const auto* bp = fixed[start + j];
        if (!bp || bp->value_stride != 4 || bp->data_ptr == nullptr) return false;
        if (bp->staging_offset != base + j * 4) return false;
    }
    return true;
}

[[nodiscard]] inline std::vector<Avx2BlockPlan>
plan_avx2_blocks(std::span<ColumnBinding*> fixed) {
    std::vector<Avx2BlockPlan> plan;
    plan.reserve(fixed.size() / 8);

    for (std::size_t c = 0; c + 8 <= fixed.size();) {
        if (!is_4byte_contiguous_block(fixed, c)) {
            ++c;
            continue;
        }
        plan.push_back(Avx2BlockPlan{
            .start_index = c,
            .dst_col_offset = fixed[c]->staging_offset,
        });
        c += 8;
    }
    return plan;
}

[[nodiscard]] inline bool is_8byte_contiguous_block(std::span<ColumnBinding*> fixed,
                                                    std::size_t start) noexcept {
    if (start + 4 > fixed.size()) return false;
    const auto base = fixed[start]->staging_offset;
    for (std::size_t j = 0; j < 4; ++j) {
        const auto* bp = fixed[start + j];
        if (!bp || bp->value_stride != 8 || bp->data_ptr == nullptr) return false;
        // Restrict 8-byte AVX2 path to raw Arrow primitive 8-byte columns.
        // Converted SQL structs (time/date/timestamp) can have 8-byte stride
        // but are not guaranteed to be semantically safe for lane-wise transpose.
        if (bp->arrow_type != arrow::Type::INT64 &&
            bp->arrow_type != arrow::Type::UINT64 &&
            bp->arrow_type != arrow::Type::DOUBLE &&
            bp->arrow_type != arrow::Type::DURATION) {
            return false;
        }
        if (bp->staging_offset != base + j * 8) return false;
    }
    return true;
}

[[nodiscard]] inline std::vector<Avx2BlockPlan64>
plan_avx2_blocks64(std::span<ColumnBinding*> fixed) {
    std::vector<Avx2BlockPlan64> plan;
    plan.reserve(fixed.size() / 4);

    for (std::size_t c = 0; c + 4 <= fixed.size();) {
        if (!is_8byte_contiguous_block(fixed, c)) {
            ++c;
            continue;
        }
        plan.push_back(Avx2BlockPlan64{
            .start_index = c,
            .dst_col_offset = fixed[c]->staging_offset,
        });
        c += 4;
    }
    return plan;
}

#if defined(__AVX2__) || defined(__GNUC__) || defined(__clang__)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
inline void transpose8x8_i32(
    const std::array<const int32_t*, 8>& src_cols,
    int64_t row_base,
    uint8_t* dst,
    std::size_t row_stride,
    std::size_t dst_col_offset)
{
    __m256i c0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[0] + row_base));
    __m256i c1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[1] + row_base));
    __m256i c2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[2] + row_base));
    __m256i c3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[3] + row_base));
    __m256i c4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[4] + row_base));
    __m256i c5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[5] + row_base));
    __m256i c6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[6] + row_base));
    __m256i c7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[7] + row_base));

    __m256i t0 = _mm256_unpacklo_epi32(c0, c1);
    __m256i t1 = _mm256_unpackhi_epi32(c0, c1);
    __m256i t2 = _mm256_unpacklo_epi32(c2, c3);
    __m256i t3 = _mm256_unpackhi_epi32(c2, c3);
    __m256i t4 = _mm256_unpacklo_epi32(c4, c5);
    __m256i t5 = _mm256_unpackhi_epi32(c4, c5);
    __m256i t6 = _mm256_unpacklo_epi32(c6, c7);
    __m256i t7 = _mm256_unpackhi_epi32(c6, c7);

    __m256i s0 = _mm256_unpacklo_epi64(t0, t2);
    __m256i s1 = _mm256_unpackhi_epi64(t0, t2);
    __m256i s2 = _mm256_unpacklo_epi64(t1, t3);
    __m256i s3 = _mm256_unpackhi_epi64(t1, t3);
    __m256i s4 = _mm256_unpacklo_epi64(t4, t6);
    __m256i s5 = _mm256_unpackhi_epi64(t4, t6);
    __m256i s6 = _mm256_unpacklo_epi64(t5, t7);
    __m256i s7 = _mm256_unpackhi_epi64(t5, t7);

    __m256i r0 = _mm256_permute2x128_si256(s0, s4, 0x20);
    __m256i r1 = _mm256_permute2x128_si256(s1, s5, 0x20);
    __m256i r2 = _mm256_permute2x128_si256(s2, s6, 0x20);
    __m256i r3 = _mm256_permute2x128_si256(s3, s7, 0x20);
    __m256i r4 = _mm256_permute2x128_si256(s0, s4, 0x31);
    __m256i r5 = _mm256_permute2x128_si256(s1, s5, 0x31);
    __m256i r6 = _mm256_permute2x128_si256(s2, s6, 0x31);
    __m256i r7 = _mm256_permute2x128_si256(s3, s7, 0x31);

    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 0 * row_stride), r0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 1 * row_stride), r1);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 2 * row_stride), r2);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 3 * row_stride), r3);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 4 * row_stride), r4);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 5 * row_stride), r5);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 6 * row_stride), r6);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 7 * row_stride), r7);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
inline void transpose4x4_i64(
    const std::array<const int64_t*, 4>& src_cols,
    int64_t row_base,
    uint8_t* dst,
    std::size_t row_stride,
    std::size_t dst_col_offset)
{
    __m256i c0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[0] + row_base));
    __m256i c1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[1] + row_base));
    __m256i c2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[2] + row_base));
    __m256i c3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_cols[3] + row_base));

    __m256i t0 = _mm256_unpacklo_epi64(c0, c1);
    __m256i t1 = _mm256_unpackhi_epi64(c0, c1);
    __m256i t2 = _mm256_unpacklo_epi64(c2, c3);
    __m256i t3 = _mm256_unpackhi_epi64(c2, c3);

    __m256i r0 = _mm256_permute2x128_si256(t0, t2, 0x20);
    __m256i r1 = _mm256_permute2x128_si256(t1, t3, 0x20);
    __m256i r2 = _mm256_permute2x128_si256(t0, t2, 0x31);
    __m256i r3 = _mm256_permute2x128_si256(t1, t3, 0x31);

    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 0 * row_stride), r0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 1 * row_stride), r1);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 2 * row_stride), r2);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + dst_col_offset + 3 * row_stride), r3);
}
#endif

} // namespace pygim::bcp::simd
