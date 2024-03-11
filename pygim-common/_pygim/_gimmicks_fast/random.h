#ifndef CHUNKED_NUMBER_GENERATOR_H
#define CHUNKED_NUMBER_GENERATOR_H

#include <functional>
#include <cstdint> // For uint64_t

#include <vector>
#include <random>
#include <iostream>
#include <execution>
#include <algorithm>

#include <immintrin.h> // Include for AVX2 intrinsics


/*
__m256i xorshift_avx2(__m256i state) {
    // Example operation: XOR and shift left
    __m256i xor_result = _mm256_xor_si256(state, _mm256_slli_epi64(state, 13));
    // Combine with another operation, e.g., shift right
    __m256i result = _mm256_xor_si256(xor_result, _mm256_srli_epi64(xor_result, 7));
    return result;
}
*/


class RandomNumberGenerator {
public:
    // Assuming an L1 cache size of 32KB and each int64_t is 8 bytes, calculate chunk size
    // This is a simplified calculation; consider your specific CPU cache size and structure
    static constexpr size_t cacheLineSize = 64; // Common cache line size in bytes
    static constexpr size_t int64Size = sizeof(int64_t);
    static constexpr size_t numbersPerCacheLine = cacheLineSize / int64Size;
    static constexpr size_t chunkSize = 4096; // Adjust based on cache size and experiment

    RandomNumberGenerator() : gen(rd()), dis(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()) {
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

    // const uint64_t getNextNumber() {
    //     if (curIdx == chunks.back().size()) {
    //         chunks.emplace_back();
    //         chunks.back().reserve(chunkSize);
    //         curIdx = 0;
    //     }
    //     if (chunks.back().empty()) {
    //         chunks.back().resize(chunkSize);
    //         getNextNumber(chunks.back().data(), chunkSize);
    //     }
    //     const uint64_t result = chunks.back()[curIdx];
    //     curIdx++;
    //     return result;
    // }

    void fillBuffer(uint64_t* dest, size_t count) {
        while (count > 0) {
            size_t chunk = std::min(count, chunkSize - curIdx);
            std::copy(chunks.back().begin() + curIdx, chunks.back().begin() + curIdx + chunk, dest);
            //std::memcpy(dest, chunks.back().data() + curIdx, chunk * sizeof(uint64_t));
            dest += chunk;
            count -= chunk;
            curIdx += chunk;

            if (curIdx >= chunkSize) {
                chunks.emplace_back();
                chunks.back().reserve(chunkSize);
                curIdx = 0;
            }
        }
    }

private:
    std::vector<std::vector<uint64_t>> chunks;
    size_t curIdx;
    std::random_device rd;
    std::mt19937_64 gen;
    std::uniform_int_distribution<uint64_t> dis;
};
/*

template <typename T>
class ChunkedNumberGenerator {
public:
    static constexpr size_t cacheLineSize = 64;
    static constexpr size_t int64Size = sizeof(uint64_t);
    static constexpr size_t numbersPerCacheLine = cacheLineSize / int64Size;
    static constexpr size_t bufferSize = 4096 / int64Size;

    ChunkedNumberGenerator() : rd(), gen(rd()), dis(std::numeric_limits<T>::min(), std::numeric_limits<T>::max()) {
        buffer.resize(bufferSize);
        reset();
    }

    void reset() {
        std::for_each(std::execution::par, buffer.begin(), buffer.end(), [](T& n) {
            ++n;
        });
        index = 0;
    }

    T getNextNumber() {
        if (index >= bufferSize) {
            reset();
        }
        return buffer[index++];
    }

    // New method to fill an external buffer directly
    void fillBuffer(T* dest, size_t count) {
        while (count > 0) {
            size_t chunk = std::min(count, bufferSize - index);
            std::memcpy(dest, buffer.data() + index, chunk * sizeof(T));
            dest += chunk;
            count -= chunk;
            index += chunk;

            if (index >= bufferSize) {
                reset(); // Ensure the buffer is refilled and index is reset
            }
        }
    }

private:
    std::random_device rd;
    std::mt19937_64 gen;
    std::uniform_int_distribution<T> dis;
    std::vector<T> buffer;
    size_t index = 0;
};

*/

class Random {
public:
    static int64_t randomInteger64() {
        return 0;
        //return generator.getNextNumber();
    }

    static int randomInteger(int min, int max) {
        static std::mt19937_64 engine(std::random_device{}());
        std::uniform_int_distribution<int> distribution(min, max);
        return distribution(engine);
    }

    static std::vector<uint64_t> randomIntegers(size_t count) {
        std::vector<uint64_t> numbers(count);
        generator.getNextNumber(numbers.data(), count);
        return numbers;
    }

private:
    static RandomNumberGenerator generator;
};

inline RandomNumberGenerator Random::generator;



#endif // CHUNKED_NUMBER_GENERATOR_H