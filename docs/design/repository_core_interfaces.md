# Core Repository Interfaces (Target)

This document defines the minimal C++20 interfaces for the pybind‑free core.

## `QuerySpec`
Immutable query descriptor used as the cache key and execution input.

```cpp
struct QuerySpec {
    std::string sql;
    std::vector<Param> params;
    std::string layout_id; // identifies row mapping layout

    [[nodiscard]] size_t cache_key() const noexcept;
};
```

**Rules**
- Immutable after construction.
- Cache key computed from `sql + param types + layout_id`.
- No dependency on pybind or Python objects.

## `StrategyBase`
Hot‑path execution template. Policy types are compile‑time parameters.

```cpp
template <typename Impl,
          typename CachePolicy,
          typename PreparedPolicy,
          typename RowMapperPolicy>
class StrategyBase {
public:
    RowSet execute(const QuerySpec& query);
    RowSet raw(const QuerySpec& query);

protected:
    RowSet fetch_rows(const QuerySpec& query); // provided by Impl
};
```

**Rules**
- `execute` may use cache + prepared cache + row mapper.
- `raw` bypasses row mapping and returns `RowSet`.

## Core Registry / Factory
Designed to be pybind‑free. Python layers wrap these when needed.

```cpp
class RecipeRegistry {
public:
    using RecipeFn = RepositoryFacade(*)(const ConnectionString&);
    void register_recipe(std::string name, RecipeFn fn);
    RecipeFn get(std::string_view name) const;
};

class StrategyFactory {
public:
    using CreateFn = std::unique_ptr<IStrategy>(*)(const ConnectionString&);
    void register_creator(std::string name, CreateFn fn);
    std::unique_ptr<IStrategy> create(std::string_view name, const ConnectionString&);
};
```

## `acquire_repository`
Runtime seam for selecting policies/strategies and assembling the facade.

```cpp
RepositoryFacade acquire_repository(std::string conn_str,
                                    std::string_view recipe = "default");
```

**Rules**
- Parsing `ConnectionString` is the only dynamic work.
- All heavy configuration resolved before query execution.
