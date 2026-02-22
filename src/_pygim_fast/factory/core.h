#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../registry/core.h"

namespace pygim::core {

/*
 * NoValidation is the default product validator policy.
 *
 * Usage:
 * - FactoryCore can be instantiated with Validator=NoValidation<Product>
 *   when no interface/protocol validation is required.
 * - Python adapter currently uses PyObjectValidator (not NoValidation)
 *   because it optionally enforces Python interface checks.
 *
 * Usage example:
 *   NoValidation<MyProduct> validator;
 *   bool ok = validator(product);
 */
template<class Product>
struct NoValidation {
    /**
     * \brief Provide a zero-cost default validator for factory products.
     * \param[in] product Produced value (unused).
     * \return Always `true`.
     * \note Exists so `FactoryCore` can be used without validation concerns.
     */
    bool operator()(const Product&) const {
        return true;
    }
};

/*
 * FactoryCore is a pybind-free creator registry + construction engine.
 *
 * - Key: registry key type (e.g., std::string)
 * - Product: created object type
 * - Creator: callable type stored in registry
 * - Validator: policy invoked on produced Product
 *
 * Usage example:
 *   using Core = FactoryCore<std::string, Product, CreatorFn>;
 *   Core core;
 *   core.register_creator("item", creator, false);
 *   auto value = core.create("item", [&](CreatorFn& c){ return c(); });
 */
template<class Key, class Product, class Creator, class Validator = NoValidation<Product>>
class FactoryCore {
public:
    struct CreatorHash {
        std::size_t operator()(const Key& key) const noexcept {
            return std::hash<Key>{}(key);
        }
    };

    struct CreatorEq {
        bool operator()(const Key& lhs, const Key& rhs) const noexcept {
            return lhs == rhs;
        }
    };

    using RegistryType = RegistryCore<Key, Creator, CreatorHash, CreatorEq, NoHooks<Key, Creator, Product>, Product>;

    /**
     * \brief Create a factory core with a validation policy.
     * \param[in] validator Validation policy object stored by value.
     * \note Exists to keep construction/validation rules in core, independent from adapter layer.
     */
    explicit FactoryCore(Validator validator = Validator{})
        : m_validator(std::move(validator)) {}

    /**
     * \brief Register or override a creator function in the internal registry.
     * \param[in] name Lookup key for the creator.
     * \param[in] creator Callable object used to create a product.
     * \param[in] override_existing When `false`, duplicate keys throw; when `true`, missing keys throw.
     * \throws std::runtime_error On invalid override state.
     * \note Exists to centralize creator lifecycle and preserve strict override semantics.
     */
    void register_creator(const Key& name, Creator creator, bool override_existing = false) {
        m_registry.register_or_override(name, std::move(creator), override_existing);
    }

    /**
     * \brief Retrieve a previously registered creator by key.
     * \param[in] name Creator key.
     * \return Stored creator callable.
     * \throws std::runtime_error If key is unknown.
     * \note Exists so call sites can separate "lookup" from "invoke" where needed.
     */
    Creator get_creator(const Key& name) const {
        if (auto* creator = m_registry.try_get_const(name)) {
            return *creator;
        }
        throw std::runtime_error("Unknown creator: " + name);
    }

    /**
     * \brief Build a product by key, then validate it with configured policy.
     * \tparam InvokeFn Invoker type: callable that accepts `Creator&` and returns `Product`.
     * \param[in] name Creator key.
     * \param[in] invoke Invocation strategy used to execute the creator.
     * \return Created and validated product.
     * \throws std::runtime_error If key is unknown or validation fails.
     * \note Exists to keep invocation policy decoupled from storage and validation policy.
     */
    template<class InvokeFn>
    Product create(const Key& name, InvokeFn&& invoke) const {
        Creator creator = get_creator(name);
        Product product = std::forward<InvokeFn>(invoke)(creator);
        if (!m_validator(product)) {
            throw std::runtime_error("Created object does not implement required interface/protocol");
        }
        return product;
    }

    /**
     * \brief Return a snapshot of registered creator keys.
     * \return Vector of keys currently registered.
     * \note Exists for introspection, debugging, and test validation.
     */
    std::vector<Key> registered_names() const {
        return m_registry.keys();
    }

private:
    RegistryType m_registry;
    Validator m_validator;
};

} // namespace pygim::core
