#pragma once

#include <immintrin.h>
#include <cstdint>
#include <tuple>
#include <span>

#include "encoded_consts.h"


constexpr std::tuple<int, int, int> decode_date(uint32_t encoded) {
    int year = (encoded >> 9) & ((1 << 14) - 1); // Extract 14 bits for year
    int month = (encoded >> 5) & ((1 << 4) - 1); // Extract 4 bits for month
    int day = encoded & ((1 << 5) - 1);          // Extract 5 bits for day
    return {year, month, day};
}

constexpr std::tuple<int, int, int> decode_time(uint32_t encoded) {
    int hour = (encoded >> 12) & ((1 << 5) - 1);  // Extract 5 bits for hour
    int minute = (encoded >> 6) & ((1 << 6) - 1); // Extract 6 bits for minute
    int second = encoded & ((1 << 6) - 1);        // Extract 6 bits for second
    return {hour, minute, second};
}


// Fixed function
#include <immintrin.h>
#include <span>
#include <vector>
#include <array>

struct Time {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    Time(uint8_t h, uint8_t m, uint8_t s) : hour(h), minute(m), second(s) {}
};

struct Date {
    uint16_t year;
    uint8_t month;
    uint8_t day;

    Date(uint16_t y, uint8_t m, uint8_t d) : year(y), month(m), day(d) {}
};


std::vector<Date> extractDates(const std::span<const uint32_t>& encodedDateTime) {
    std::vector<Date> extractedDates;

    for (size_t i = 0; i < encodedDateTime.size(); i += 8) {
        if (i + 8 > encodedDateTime.size()) {
            break; // Prevent exceeding bounds
        }

        // Load 8 encoded date-time values into an AVX2 register
        __m256i data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(encodedDateTime.data() + i));
        
        // Assuming year, month, and day are encoded similarly
        // Adjust these masks and shifts according to your specific encoding
        __m256i years = _mm256_srli_epi32(_mm256_and_si256(data, _mm256_set1_epi32(0xFFF << 20)), 20);
        __m256i months = _mm256_srli_epi32(_mm256_and_si256(data, _mm256_set1_epi32(0xF << 16)), 16);
        __m256i days = _mm256_srli_epi32(_mm256_and_si256(data, _mm256_set1_epi32(0x1F << 11)), 11);

        // Temporary storage
        alignas(32) uint32_t tempYears[8], tempMonths[8], tempDays[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tempYears), years);
        _mm256_store_si256(reinterpret_cast<__m256i*>(tempMonths), months);
        _mm256_store_si256(reinterpret_cast<__m256i*>(tempDays), days);

        // Construct Date objects and add to the vector
        for (int j = 0; j < 8; ++j) {
            extractedDates.emplace_back(static_cast<uint16_t>(tempYears[j]), 
                                        static_cast<uint8_t>(tempMonths[j]), 
                                        static_cast<uint8_t>(tempDays[j]));
        }
    }

    return extractedDates;
}


std::vector<Time> extractTimes(const std::span<const uint32_t>& encodedDateTime) {
    std::vector<Time> extractedTimes;

    for (size_t i = 0; i < encodedDateTime.size(); i += 8) {
        if (i + 8 > encodedDateTime.size()) {
            break; // Ensure we don't exceed the bounds of encodedDateTime
        }

        // Load 8 encoded date-time values into an AVX2 register
        __m256i data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(encodedDateTime.data() + i));
        
        // Extract hours, minutes, and seconds by masking and shifting
        __m256i hours = _mm256_srli_epi32(_mm256_and_si256(data, _mm256_set1_epi32(0x1F << 12)), 12);
        __m256i mins = _mm256_srli_epi32(_mm256_and_si256(data, _mm256_set1_epi32(0x3F << 6)), 6);
        __m256i secs = _mm256_and_si256(data, _mm256_set1_epi32(0x3F));

        // Temporary storage to store the extracted values
        alignas(32) uint32_t tempHours[8], tempMinutes[8], tempSeconds[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tempHours), hours);
        _mm256_store_si256(reinterpret_cast<__m256i*>(tempMinutes), mins);
        _mm256_store_si256(reinterpret_cast<__m256i*>(tempSeconds), secs);

        // Create Time objects and add them to the vector
        for (int j = 0; j < 8; ++j) {
            extractedTimes.emplace_back(static_cast<uint8_t>(tempHours[j]), 
                                        static_cast<uint8_t>(tempMinutes[j]), 
                                        static_cast<uint8_t>(tempSeconds[j]));
        }
    }

    return extractedTimes;
}

