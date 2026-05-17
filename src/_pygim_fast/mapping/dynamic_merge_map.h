#pragma once

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace pygim::mapping {

enum class MergeStrategy {
    Sum,
    Max,
    Min,
    Replace,
};

template <typename T, typename = void>
struct MergeDefaultStrategy {
    static constexpr MergeStrategy value = MergeStrategy::Replace;
};

template <typename T>
struct MergeDefaultStrategy<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
    static constexpr MergeStrategy value = MergeStrategy::Sum;
};

template <typename Key,
          typename T,
          typename Map = std::unordered_map<Key, T>,
          typename StrategyMap = std::unordered_map<Key, MergeStrategy>>
class DynamicMergeMap {
public:
    using key_type = Key;
    using mapped_type = T;
    using map_type = Map;

    explicit DynamicMergeMap(
        MergeStrategy default_strategy = MergeDefaultStrategy<T>::value)
        : m_default_strategy(default_strategy) {}

    explicit DynamicMergeMap(const Map& values,
                             MergeStrategy default_strategy = MergeDefaultStrategy<T>::value)
        : m_values(values),
          m_default_strategy(default_strategy) {}

    void set_default_strategy(MergeStrategy strategy) noexcept {
        m_default_strategy = strategy;
    }

    MergeStrategy default_strategy() const noexcept {
        return m_default_strategy;
    }

    void set_merge_strategy(const Key& key, MergeStrategy strategy) {
        m_strategies[key] = strategy;
    }

    void set(const Key& key, const T& value) {
        m_values[key] = value;
    }

    bool contains(const Key& key) const {
        return m_values.find(key) != m_values.end();
    }

    const T& at(const Key& key) const {
        return m_values.at(key);
    }

    T value_or(const Key& key, const T& fallback) const {
        const auto it = m_values.find(key);
        if (it == m_values.end()) return fallback;
        return it->second;
    }

    const Map& data() const noexcept {
        return m_values;
    }

    const StrategyMap& strategies() const noexcept {
        return m_strategies;
    }

    void clear() {
        m_values.clear();
        m_strategies.clear();
    }

    void merge_in(const Key& key, const T& rhs) {
        const auto strategy = strategy_for(key);
        auto it = m_values.find(key);
        if (it == m_values.end()) {
            m_values[key] = rhs;
            return;
        }
        it->second = apply(strategy, it->second, rhs);
    }

    void merge_with(const DynamicMergeMap& other) {
        for (const auto& [key, value] : other.m_values) {
            merge_in(key, value);
        }
    }

    DynamicMergeMap merged(const DynamicMergeMap& other) const {
        DynamicMergeMap out = *this;
        out.merge_with(other);
        return out;
    }

    friend DynamicMergeMap operator|(const DynamicMergeMap& lhs,
                                     const DynamicMergeMap& rhs) {
        return lhs.merged(rhs);
    }

private:
    MergeStrategy strategy_for(const Key& key) const {
        const auto it = m_strategies.find(key);
        return (it == m_strategies.end()) ? m_default_strategy : it->second;
    }

    static T apply(MergeStrategy strategy, const T& lhs, const T& rhs) {
        switch (strategy) {
            case MergeStrategy::Replace:
                return rhs;
            case MergeStrategy::Sum:
                if constexpr (std::is_arithmetic_v<T>) return lhs + rhs;
                return rhs;
            case MergeStrategy::Max:
                if constexpr (std::is_arithmetic_v<T>) return std::max(lhs, rhs);
                return rhs;
            case MergeStrategy::Min:
                if constexpr (std::is_arithmetic_v<T>) return std::min(lhs, rhs);
                return rhs;
        }
        throw std::runtime_error("unsupported merge strategy");
    }

private:
    Map m_values{};
    StrategyMap m_strategies{};
    MergeStrategy m_default_strategy;
};

} // namespace pygim::mapping
