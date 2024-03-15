#ifndef ID_H
#define ID_H

#include <functional>
#include <cstdint> // For uint64_t

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

    ChunkedNumberGenerator() : gen(rd()), dis(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()) {
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


template<typename T>
class ID {
public:
    // Static assert to allow only uint8_t, uint16_t, uint32_t, uint64_t
    static_assert(
        std::is_same<T, uint8_t>::value ||
        std::is_same<T, uint16_t>::value ||
        std::is_same<T, uint32_t>::value ||
        std::is_same<T, uint64_t>::value,
        "ID can only be instantiated with unsigned integer types (uint8_t, uint16_t, uint32_t, uint64_t)"
    );

    constexpr explicit ID(T id) : mId(id) {}

    static ID random() {
        T randomId = generator.getNextNumber();
        return ID(randomId);
    }

    [[nodiscard]] constexpr auto hash() const noexcept {
        return std::hash<T>()(mId);
    }

    constexpr bool operator==(const ID& other)  const &  noexcept { return mId == other.mId; };
    constexpr bool operator==(const ID&& other) const &  noexcept { return mId == other.mId; };
    constexpr bool operator==(const ID& other)  const && noexcept { return mId == other.mId; };
    constexpr bool operator==(const ID&& other) const && noexcept { return mId == other.mId; };

    // FIXME: ensure immutability!
    // Delete copy assignment operator and copy constructor to ensure immutability
    //ID(ID&&) = delete;
    //ID(const ID&) = delete;
    //ID& operator=(const ID&) = delete;
    //ID& operator=(ID&&) = delete;

private:
    const T mId;
    static ChunkedNumberGenerator generator;
};

template<typename T>
inline ChunkedNumberGenerator ID<T>::generator;

#endif // ID_H