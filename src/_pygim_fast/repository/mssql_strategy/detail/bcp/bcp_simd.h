#pragma once
// SIMD level selection helpers for BCP transpose pipeline.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <algorithm>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace pygim::bcp {

enum class Simd {
    Scalar,
    Avx2,
};

[[nodiscard]] inline bool cpu_supports_avx2() noexcept {
    if (const char* forced = std::getenv("PYGIM_FORCE_SIMD")) {
        std::string mode(forced);
        std::transform(mode.begin(), mode.end(), mode.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (mode == "scalar" || mode == "off" || mode == "none" || mode == "0")
            return false;
        if (mode == "avx2" || mode == "1")
            return true;
    }

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#if defined(_MSC_VER)
    int regs[4]{};
    __cpuidex(regs, 0, 0);
    if (regs[0] < 7) return false;
    __cpuidex(regs, 1, 0);
    const bool osxsave = (regs[2] & (1 << 27)) != 0;
    if (!osxsave) return false;
    unsigned long long xcr0 = _xgetbv(0);
    const bool ymm_state = (xcr0 & 0x6) == 0x6;
    if (!ymm_state) return false;
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0;  // AVX2
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__has_builtin)
#if __has_builtin(__builtin_cpu_supports)
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
#else
    return __builtin_cpu_supports("avx2");
#endif
#else
    return false;
#endif
#else
    return false;
#endif
}

} // namespace pygim::bcp
