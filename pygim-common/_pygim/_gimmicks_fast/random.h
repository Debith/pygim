#ifndef CHUNKED_NUMBER_GENERATOR_H
#define CHUNKED_NUMBER_GENERATOR_H

#include <vector>
#include <random>
#include <iostream>


class ChunkedNumberGenerator {
public:
    // Assuming an L1 cache size of 32KB and each int64_t is 8 bytes, calculate chunk size
    // This is a simplified calculation; consider your specific CPU cache size and structure
    static constexpr size_t cacheLineSize = 64; // Common cache line size in bytes
    static constexpr size_t int64Size = sizeof(int64_t);
    static constexpr size_t numbersPerCacheLine = cacheLineSize / int64Size;
    static constexpr size_t chunkSize = 4096; // Adjust based on cache size and experiment

    ChunkedNumberGenerator() : gen(rd()),
                               dis(std::numeric_limits<int64_t>::min(),
                               std::numeric_limits<int64_t>::max()) {
        chunks.emplace_back();
        chunks.back().reserve(chunkSize);
        currentChunkIndex = 0;
    }

    int64_t getNextNumber() {
        if (chunks[currentChunkIndex].size() == chunkSize) {
            currentChunkIndex++;
            if (currentChunkIndex >= chunks.size()) {
                chunks.emplace_back();
                chunks.back().reserve(chunkSize);
            }
        }

        int64_t number = dis(gen);
        chunks[currentChunkIndex].push_back(number);
        return number;
    }

private:
    std::vector<std::vector<int64_t>> chunks;
    size_t currentChunkIndex;
    std::random_device rd;
    std::mt19937_64 gen;
    std::uniform_int_distribution<int64_t> dis;
};


class Random {
public:
    static int64_t randomInteger64() {
        return generator.getNextNumber();
    }

    static int randomInteger(int min, int max) {
        static std::mt19937_64 engine(std::random_device{}());
        std::uniform_int_distribution<int> distribution(min, max);
        return distribution(engine);
    }

    static std::vector<uint64_t> randomIntegers(size_t count) {
        std::vector<uint64_t> numbers;
        numbers.reserve(count);
        for (size_t i = 0; i < count; i++) {
            numbers.push_back(generator.getNextNumber());
        }
        return numbers;
    }

private:
    static ChunkedNumberGenerator generator;
};

inline ChunkedNumberGenerator Random::generator;

#endif // CHUNKED_NUMBER_GENERATOR_H