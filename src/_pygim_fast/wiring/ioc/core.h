#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pygim::core {

enum class Lifecycle { Transient = 0, Singleton = 1 };

[[nodiscard]] constexpr std::string_view lifecycle_to_string(Lifecycle lifecycle) noexcept {
    return lifecycle == Lifecycle::Singleton ? "singleton" : "transient";
}

[[nodiscard]] constexpr Lifecycle parse_lifecycle(std::string_view lifecycle) {
    if (lifecycle == "transient") {
        return Lifecycle::Transient;
    }
    if (lifecycle == "singleton") {
        return Lifecycle::Singleton;
    }
    throw std::runtime_error("lifecycle must be 'transient' or 'singleton'");
}

static_assert(parse_lifecycle("transient") == Lifecycle::Transient);
static_assert(parse_lifecycle("singleton") == Lifecycle::Singleton);
static_assert(lifecycle_to_string(Lifecycle::Transient) == "transient");
static_assert(lifecycle_to_string(Lifecycle::Singleton) == "singleton");

// ---------------------------------------------------------------------------
// Autowiring policy
//
// The adapter reduces a Python constructor to neutral ParamSpec records; the
// pure policy below decides, per parameter, whether to inject from the
// container, fall back to its default value, or fail. Keeping the decision
// table free of Python lets it run (and be tested) under constant evaluation.
// ---------------------------------------------------------------------------

enum class ParamKind {
    PositionalOrKeyword = 0,
    KeywordOnly = 1,
    PositionalOnly = 2,
    Variadic = 3,  // *args / **kwargs
};

template<class Key>
struct ParamSpec {
    std::string name;
    ParamKind kind{ParamKind::PositionalOrKeyword};
    std::optional<Key> annotation;
    bool has_default{false};
};

template<class Key>
struct PlannedInjection {
    std::string name;
    Key annotation;
};

template<class Key, class ContainsFn>
[[nodiscard]] constexpr std::vector<PlannedInjection<Key>> plan_autowiring(
        const std::vector<ParamSpec<Key>>& params, ContainsFn&& contains) {
    std::vector<PlannedInjection<Key>> injections;
    for (const auto& param : params) {
        if (param.kind == ParamKind::Variadic) {
            continue;
        }
        if (param.kind == ParamKind::PositionalOnly) {
            throw std::runtime_error(
                "autowire does not support positional-only parameter '" + param.name + "'");
        }
        if (!param.annotation) {
            if (param.has_default) {
                continue;
            }
            throw std::runtime_error(
                "autowire requires a type annotation for parameter '" + param.name + "'");
        }
        if (!contains(*param.annotation)) {
            if (param.has_default) {
                continue;
            }
            throw std::runtime_error(
                "No provider registered for autowired dependency '" + param.name + "'");
        }
        injections.push_back({param.name, *param.annotation});
    }
    return injections;
}

namespace policy_tests {

constexpr bool injects_registered_annotation() {
    std::vector<ParamSpec<int>> params;
    params.push_back({"repo", ParamKind::PositionalOrKeyword, 7, false});
    auto plan = plan_autowiring(params, [](int) { return true; });
    return plan.size() == 1 && plan[0].annotation == 7 && plan[0].name == "repo";
}

constexpr bool defaults_when_provider_missing() {
    std::vector<ParamSpec<int>> params;
    params.push_back({"retries", ParamKind::PositionalOrKeyword, 3, true});
    return plan_autowiring(params, [](int) { return false; }).empty();
}

constexpr bool skips_variadics_and_untyped_defaults() {
    std::vector<ParamSpec<int>> params;
    params.push_back({"args", ParamKind::Variadic, std::nullopt, false});
    params.push_back({"flag", ParamKind::KeywordOnly, std::nullopt, true});
    return plan_autowiring(params, [](int) { return true; }).empty();
}

static_assert(injects_registered_annotation());
static_assert(defaults_when_provider_missing());
static_assert(skips_variadics_and_untyped_defaults());

} // namespace policy_tests

// Lazily-filled constructor introspection shared between descriptor copies.
// resolve() works on a descriptor copy for re-entrancy safety; the shared
// slot lets a fill made through the copy persist on the stored descriptor.
template<class Key>
struct AutowireSlot {
    std::shared_ptr<const std::vector<ParamSpec<Key>>> params;
};

template<class Interface, class Provider, class Decorator>
struct ServiceDescriptor {
    Interface interface;
    Provider provider;
    Lifecycle lifecycle{Lifecycle::Transient};
    std::optional<std::string> name;
    std::vector<Decorator> decorators;
    bool autowire{false};
    std::shared_ptr<AutowireSlot<Interface>> autowire_slot;

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
          autowire(autowire_),
          autowire_slot(autowire_ ? std::make_shared<AutowireSlot<Interface>>() : nullptr) {}
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
        m_generations.reserve(capacity);
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
            ++m_generations[it->second];
            return;
        }

        if (override_existing) {
            throw std::runtime_error("override=True requires existing service");
        }

        std::size_t index = m_registry.size();
        m_registry.emplace_back(std::move(descriptor));
        m_generations.push_back(0);
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

    template<class ProviderInvoker, class DecoratorApplier, class InstanceValidator>
    [[nodiscard]] instance_type resolve(
            const key_type& key,
            ProviderInvoker&& invoke_provider,
            DecoratorApplier&& apply_decorator,
            InstanceValidator&& validate_instance) {
        auto it = m_index_map.find(key);
        if (it == m_index_map.end()) {
            throw std::runtime_error("No provider for key");
        }
        const std::size_t index = it->second;

        if (m_registry[index].lifecycle == Lifecycle::Singleton) {
            auto singleton = m_singletons.find(index);
            if (singleton != m_singletons.end()) {
                return singleton->second;
            }
        }

        if (std::find(m_resolution_stack.begin(), m_resolution_stack.end(), index)
                != m_resolution_stack.end()) {
            throw std::runtime_error("Circular dependency detected");
        }
        m_resolution_stack.push_back(index);
        ResolutionGuard guard{m_resolution_stack};

        // Work on a copy: the provider and decorators run arbitrary code that
        // may re-enter register_or_override() and reallocate m_registry,
        // which would invalidate any reference held across those calls.
        const std::uint64_t generation = m_generations[index];
        descriptor_type descriptor = m_registry[index];

        instance_type instance = std::forward<ProviderInvoker>(invoke_provider)(descriptor);
        for (const auto& decorator : descriptor.decorators) {
            instance = apply_decorator(decorator, std::move(instance));
        }
        validate_instance(instance, descriptor.interface);

        // Skip caching when the registration was overridden mid-resolve;
        // the instance belongs to the descriptor generation we started from.
        if (descriptor.lifecycle == Lifecycle::Singleton && m_generations[index] == generation) {
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
    struct ResolutionGuard {
        std::vector<std::size_t>& stack;
        ~ResolutionGuard() { stack.pop_back(); }
    };

    std::vector<descriptor_type> m_registry;
    std::vector<std::uint64_t> m_generations;
    std::unordered_map<key_type, std::size_t, Hash, Eq> m_index_map;
    std::unordered_map<std::size_t, instance_type> m_singletons;
    std::vector<std::size_t> m_resolution_stack;
};

} // namespace pygim::core
