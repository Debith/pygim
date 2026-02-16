# Bridge + Strategy Repository

**Intent**: Split the repository abstraction (Bridge) from the database provider implementations while allowing each provider to supply Strategy objects for connection management, statement preparation, and retry policies.

## When to Use
- Different backends share high-level repository semantics but vary drastically in transport (ODBC vs libpq vs raw TCP).
- You want to swap providers at runtime without recompiling while keeping the core repository testable via mocks.
- Providers must expose multiple tunable behaviors (prepared statement caching, retry policies, telemetry sinks).

## Key Principles
- Define a narrow `DbProvider` interface the bridge depends on; each provider packages its internal strategies (`ConnectionStrategy`, `RetryStrategy`, ...).
- Keep strategies value types where possible so they can be composed or pipelined with STL algorithms.
- Use `std::shared_ptr` or `std::unique_ptr` to inject the provider into the bridge, enabling lifetime control and lazy initialization.
- Apply C++20 coroutines or executors for async execution without leaking provider-specific event loops into the bridge.

## Minimal Sketch (C++20)

```cpp
struct QuerySpec { std::string text; std::vector<Binding> parameters; };

struct ConnectionStrategy {
    virtual ~ConnectionStrategy() = default;
    virtual std::unique_ptr<ConnectionHandle> acquire() = 0;
};

struct DbProvider {
    virtual ~DbProvider() = default;
    virtual ConnectionStrategy &connections() = 0;
    virtual QueryResult execute(QuerySpec spec) = 0;
};

class RepositoryBridge {
public:
    explicit RepositoryBridge(std::shared_ptr<DbProvider> provider)
        : m_provider(std::move(provider)) {}

    QueryResult run(QuerySpec spec) {
        auto &strategy = m_provider->connections();
        auto conn = strategy.acquire();
        return m_provider->execute(std::move(spec));
    }
private:
    std::shared_ptr<DbProvider> m_provider;
};

class PostgresProvider final : public DbProvider {
public:
    QueryResult execute(QuerySpec spec) override {
        return PgExecutor{m_pool}.run(std::move(spec));
    }
    ConnectionStrategy &connections() override { return m_conn_strategy; }
private:
    PgPool m_pool;
    PgConnectionStrategy m_conn_strategy{m_pool};
};
```

## PlantUML
Render `bridge_strategy.puml` to inspect how the bridge delegates to provider-specific strategies.
