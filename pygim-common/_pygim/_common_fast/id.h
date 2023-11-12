#ifndef ID_H
#define ID_H

#include <functional>
#include <cstdint> // For uint64_t

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

    constexpr explicit ID(T id) : m_id(id) {};

    // ... other methods ...

    [[nodiscard]] constexpr auto hash() const noexcept {
        return std::hash<T>()(m_id);
    };

    constexpr bool operator==(const ID& other)  const &  noexcept { return m_id == other.m_id; }
    constexpr bool operator==(const ID&& other) const &  noexcept { return m_id == other.m_id; }
    constexpr bool operator==(const ID& other)  const && noexcept { return m_id == other.m_id; }
    constexpr bool operator==(const ID&& other) const && noexcept { return m_id == other.m_id; }

    // Delete copy assignment operator and copy constructor to ensure immutability
    ID(ID&&) = delete;
    ID(const ID&) = delete;
    ID& operator=(const ID&) = delete;
    ID& operator=(ID&&) = delete;

private:
    const T m_id;
};

// Specialize the std::hash template for ID
namespace std {
    template<typename T>
    struct hash<ID<T>> {
        size_t operator()(const ID<T>& idObj) const noexcept {
            return idObj.__hash__();
        }
    };
}

#endif // ID_H