# Policy-Based Repository Composition

**Intent**: Compose repository behavior from orthogonal policy classes (query building, connection management, transaction scoping) that can be mixed and matched at compile time. This yields a family of repositories without deep inheritance trees.

## When to Use
- You want to keep backend-specific code header-only for maximal optimization.
- Feature permutations grow combinatorially (hooks on/off, retry modes, serialization formats) and should not explode the number of concrete classes.
- The build already relies on templates, CRTP, or pybind11 bindings where compile-time composition is natural.

## Key Principles
- Define small policy interfaces (`QueryPolicy`, `ConnectionPolicy`, `TransactionPolicy`) expressed as templates or CRTP base classes.
- Provide ready-made policy bundles (e.g., `using MssqlPolicies = RepositoryPolicies<MssqlQueryPolicy, OdbcConnectionPolicy, SnapshotTransactionPolicy>;`).
- Prefer stateless policy types; stateful policies can own references to pools or configuration structs.
- Use `constexpr` booleans (e.g., `static constexpr bool supports_merge = true;`) to gate features.

## Minimal Sketch (C++20)

```cpp
template<typename QueryPolicy, typename ConnectionPolicy>
class Repository : private QueryPolicy, private ConnectionPolicy {
public:
    QueryResult run(std::string_view sql) {
        auto conn = ConnectionPolicy::acquire();
        auto stmt = QueryPolicy::prepare(sql, conn);
        return QueryPolicy::execute(stmt, conn);
    }

    static constexpr bool supports_merge = QueryPolicy::supports_merge && ConnectionPolicy::supports_transactions;
};

struct MssqlQueryPolicy {
    static constexpr bool supports_merge = true;
    template<typename Conn>
    static auto prepare(std::string_view sql, Conn &conn) { return conn.prepare(sql); }
    template<typename Stmt, typename Conn>
    static QueryResult execute(Stmt &stmt, Conn &conn) { return stmt.run(conn); }
};

struct ArrowBulkConnectionPolicy {
    static constexpr bool supports_transactions = false;
    static ConnectionHandle acquire();
};

using ArrowMssqlRepository = Repository<MssqlQueryPolicy, ArrowBulkConnectionPolicy>;
```

## PlantUML
Render `policy_composition.puml` to visualize how policies plug into the composed repository.
