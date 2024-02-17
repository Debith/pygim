#ifndef CHUNKED_NUMBER_GENERATOR_H
#define CHUNKED_NUMBER_GENERATOR_H

#include <algorithm>
#include <execution>
#include <vector>
#include <random>
#include <iostream>
#include <random>

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
        std::vector<uint64_t> numbers(count);
        generator.fillBuffer(numbers.data(), count);
        return numbers;
    }

private:
    static ChunkedNumberGenerator<uint64_t> generator;
};

inline ChunkedNumberGenerator<uint64_t> Random::generator;

#endif // CHUNKED_NUMBER_GENERATOR_H