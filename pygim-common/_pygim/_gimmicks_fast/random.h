#ifndef CHUNKED_NUMBER_GENERATOR_H
#define CHUNKED_NUMBER_GENERATOR_H

#include <functional>
#include <cstdint> // For uint64_t

#include <vector>
#include <random>
#include <iostream>

#include <immintrin.h> // Include for AVX2 intrinsics

__m256i xorshift_avx2(__m256i state) {
    // Example operation: XOR and shift left
    __m256i xor_result = _mm256_xor_si256(state, _mm256_slli_epi64(state, 13));
    // Combine with another operation, e.g., shift right
    __m256i result = _mm256_xor_si256(xor_result, _mm256_srli_epi64(xor_result, 7));
    return result;
}

/*
class ChunkedNumberGenerator {
public:
    static int getNextNumber() {
        // Thread-safe initialization of a static local variable
         static std::mt19937_64 engine(std::random_device{}());
        static std::uniform_int_distribution<int> distribution(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());

        return distribution(engine);
    }
};

*/

class ChunkedNumberGenerator {
public:
    // Assuming an L1 cache size of 32KB and each int64_t is 8 bytes, calculate chunk size
    // This is a simplified calculation; consider your specific CPU cache size and structure
    static constexpr size_t cacheLineSize = 64; // Common cache line size in bytes
    static constexpr size_t int64Size = sizeof(int64_t);
    static constexpr size_t numbersPerCacheLine = cacheLineSize / int64Size;
    static constexpr size_t chunkSize = 4096; // Adjust based on cache size and experiment

    ChunkedNumberGenerator() : gen(rd()), dis(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()) {
        chunks.emplace_back();
        chunks.back().reserve(chunkSize);
        curIdx = 0;
    }

    template<typename T>
    void getNextNumber(T* array, size_t size) {    
        // Note: Example assumes size is a multiple of 4 for simplicity
        __m256i value = _mm256_set_epi64x(5, 9, 2, 7); // Initialize with 64-bit values
        for (size_t i = 0; i < size; i += 4) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(array + i), value); // Store 64-bit integer values
            // Increment each element in 'value' by 4 to prepare for the next store
            value = _mm256_add_epi64(value, _mm256_set1_epi64x(4)); 
        }
    }   

    const uint64_t getNextNumber() {
        if (curIdx == chunks.back().size()) {
            chunks.emplace_back();
            chunks.back().reserve(chunkSize);
            curIdx = 0;
        }
        if (chunks.back().empty()) {
            chunks.back().resize(chunkSize);
            getNextNumber(chunks.back().data(), chunkSize);
        }
        const uint64_t result = chunks.back()[curIdx];
        curIdx++;
        return result;
    }  

private:
    std::vector<std::vector<uint64_t>> chunks;
    size_t curIdx;
    std::random_device rd;
    std::mt19937_64 gen;
    std::uniform_int_distribution<uint64_t> dis;
};

#endif // CHUNKED_NUMBER_GENERATOR_H