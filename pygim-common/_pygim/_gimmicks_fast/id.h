#ifndef ID_H
#define ID_H

#include <functional>
#include <cstdint> // For uint64_t
#include "random.h"

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