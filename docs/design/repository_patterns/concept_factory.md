# Concept-Guided Factory

**Intent**: Use C++20 concepts and constrained templates to express families of repository collaborators at compile time. Instead of runtime polymorphism, the compiler ensures that a provider type exposes the required factories and behaviors.

## When to Use
- You can select the backend at compile time (different build configurations, static plugins, or header-only adapters).
- You want zero-overhead abstractions where the compiler inlines each backend without virtual dispatch.
- Providers must satisfy richer contracts than “has method X” (e.g., returns a connection that models `ConnectionLike`).

## Key Principles
- Model each collaborator (`Query`, `Connection`, `Transaction`) as a concept.
- Define a `RepositoryProvider` concept that bundles the individual collaborators.
- Use `if constexpr` to branch on provider capabilities (bulk copy, server-side cursors) while keeping the general algorithm shared.
- Offer helper aliases (e.g., `using DefaultRepository = Repository<Policy::MssqlProvider>;`) for readability.

## Minimal Sketch (C++20)

```cpp
template<typename C>
concept ConnectionLike = requires(C c) {
    { c.execute(std::string_view{}) } -> std::convertible_to<void>;
};

template<typename Q>
concept QueryLike = requires(Q q, ConnectionLike auto &conn) {
    { q.run(conn) } -> std::same_as<QueryResult>;
};

template<typename Provider>
concept RepositoryProvider = requires(Provider p, std::string_view sql) {
    { Provider::name } -> std::convertible_to<std::string_view>;
    { p.make_connection() } -> ConnectionLike;
    { p.make_query(sql) } -> QueryLike;
};

template<RepositoryProvider Provider>
class Repository {
public:
    QueryResult run(std::string_view sql) {
        auto conn = m_provider.make_connection();
        auto query = m_provider.make_query(sql);
        if constexpr (requires { Provider::supports_bulk(); }) {
            // compile-time feature flag
        }
        return query.run(conn);
    }
private:
    Provider m_provider;
};

struct MssqlProvider {
    static constexpr std::string_view name = "mssql";
    ConnectionHandle make_connection();
    Query make_query(std::string_view sql);
};
```

## PlantUML
Render `concept_factory.puml` to see how the concepts bind the provider, query, and connection families.
