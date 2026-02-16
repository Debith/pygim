# Abstract Factory Repository Family

**Intent**: Bundle the families of `Query`, `Connection`, and `Transaction` types behind an abstract creator so the repository can remain agnostic of the concrete database backend.

## When to Use
- You need to ship multiple database backends (Postgres, MSSQL, SQLite) with identical repository semantics.
- Construction of the family members is interdependent (e.g., Postgres query object must know about its connection pooling policy).
- Runtime selection of a backend is required (environment variable, CLI flag, or configuration file).

## Key Principles
- Expose a single `RepositoryFamily` interface that manufactures the coordinated types.
- Keep factories cheap to copy or move; prefer storing references to shared services (logging, metrics) rather than duplicating state.
- Return `std::unique_ptr` (or `std::shared_ptr` when pooling) to keep ownership explicit.
- Surface small capability structs (e.g., `struct FamilyCapabilities { bool supports_bulk_copy; };`) when features differ between vendors.

## Minimal Sketch (C++20)

```cpp
struct QueryResult { std::span<const Row> rows; };
struct Connection { virtual void execute(std::string_view sql) = 0; virtual ~Connection() = default; };
struct Query { virtual QueryResult run(Connection &conn) = 0; virtual ~Query() = default; };

class RepositoryFamily {
public:
    virtual ~RepositoryFamily() = default;
    [[nodiscard]] virtual std::unique_ptr<Connection> make_connection() = 0;
    [[nodiscard]] virtual std::unique_ptr<Query> make_query(std::string_view text) = 0;
};

class PostgresFamily final : public RepositoryFamily {
public:
    [[nodiscard]] std::unique_ptr<Connection> make_connection() override {
        return std::make_unique<PostgresConnection>(m_pool.claim());
    }
    [[nodiscard]] std::unique_ptr<Query> make_query(std::string_view text) override {
        return std::make_unique<PostgresQuery>(std::string{text});
    }
private:
    PgConnectionPool &m_pool;
};

class Repository {
public:
    explicit Repository(std::unique_ptr<RepositoryFamily> family)
        : m_family(std::move(family)) {}

    QueryResult run(std::string_view sql) {
        auto conn = m_family->make_connection();
        auto query = m_family->make_query(sql);
        return query->run(*conn);
    }
private:
    std::unique_ptr<RepositoryFamily> m_family;
};
```

## PlantUML
Render `abstract_factory.puml` to visualize the factory hierarchy and collaborating types.
