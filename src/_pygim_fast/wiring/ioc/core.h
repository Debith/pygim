#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pygim::core {

enum class Lifecycle { Transient = 0, Singleton = 1 };

template<class Interface, class Provider, class Decorator>
struct ServiceDescriptor {
    Interface interface;
    Provider provider;
    Lifecycle lifecycle{Lifecycle::Transient};
    std::optional<std::string> name;
    std::vector<Decorator> decorators;
    bool autowire{false};

    ServiceDescriptor() = default;

    ServiceDescriptor(
        Interface interface_,
        Provider provider_,
        Lifecycle lifecycle_ = Lifecycle::Transient,
        std::optional<std::string> name_ = std::nullopt,
                std::vector<Decorator> decorators_ = {},
                bool autowire_ = false)
        : interface(std::move(interface_)),
          provider(std::move(provider_)),
          lifecycle(lifecycle_),
          name(std::move(name_)),
                    decorators(std::move(decorators_)),
                    autowire(autowire_) {}
};

template<class Key, class Descriptor, class Instance, class Hash, class Eq>
class ContainerCore {
public:
    using key_type = Key;
    using descriptor_type = Descriptor;
    using instance_type = Instance;

    explicit ContainerCore(std::size_t capacity = 0) {
        reserve(capacity);
    }

    void reserve(std::size_t capacity) {
        m_registry.reserve(capacity);
        m_index_map.reserve(capacity);
    }

    void register_or_override(const key_type& key, descriptor_type descriptor, bool override_existing = false) {
        auto it = m_index_map.find(key);
        bool exists = it != m_index_map.end();
        if (exists) {
            if (!override_existing) {
                throw std::runtime_error("Duplicate service registration (use override=True)");
            }
            m_registry[it->second] = std::move(descriptor);
            m_singletons.erase(it->second);
            return;
        }

        if (override_existing) {
            throw std::runtime_error("override=True requires existing service");
        }

        std::size_t index = m_registry.size();
        m_registry.emplace_back(std::move(descriptor));
        m_index_map.emplace(key, index);
    }

    [[nodiscard]] bool contains(const key_type& key) const {
        return m_index_map.find(key) != m_index_map.end();
    }

    [[nodiscard]] const descriptor_type* try_descriptor(const key_type& key) const {
        auto it = m_index_map.find(key);
        if (it == m_index_map.end()) {
            return nullptr;
        }
        return &m_registry[it->second];
    }

    template<class ProviderInvoker, class DecoratorApplier>
    [[nodiscard]] instance_type resolve(const key_type& key, ProviderInvoker&& invoke_provider, DecoratorApplier&& apply_decorators) {
        auto it = m_index_map.find(key);
        if (it == m_index_map.end()) {
            throw std::runtime_error("No provider for key");
        }

        std::size_t index = it->second;
        descriptor_type& descriptor = m_registry[index];

        if (descriptor.lifecycle == Lifecycle::Singleton) {
            auto singleton = m_singletons.find(index);
            if (singleton != m_singletons.end()) {
                return singleton->second;
            }
        }

        instance_type instance = std::forward<ProviderInvoker>(invoke_provider)(descriptor);
        instance = std::forward<DecoratorApplier>(apply_decorators)(descriptor.decorators, std::move(instance), descriptor.interface);

        if (descriptor.lifecycle == Lifecycle::Singleton) {
            m_singletons.insert_or_assign(index, instance);
        }

        return instance;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_registry.size();
    }

    [[nodiscard]] std::vector<key_type> keys() const {
        std::vector<key_type> result;
        result.reserve(m_index_map.size());
        for (const auto& [key, _] : m_index_map) {
            result.push_back(key);
        }
        return result;
    }

private:
    std::vector<descriptor_type> m_registry;
    std::unordered_map<key_type, std::size_t, Hash, Eq> m_index_map;
    std::unordered_map<std::size_t, instance_type> m_singletons;
};

} // namespace pygim::core