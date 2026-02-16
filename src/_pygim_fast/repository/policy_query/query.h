#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "mssql_policy.h"

namespace pygim::policy_query {

namespace py = pybind11;

class QueryFactory;

class Query {
public:
    Query();
    Query(std::string sql, std::vector<py::object> params = {});
    Query(const Query &other);
    Query(Query &&) noexcept = default;
    Query &operator=(const Query &other);
    Query &operator=(Query &&) noexcept = default;
    ~Query();

    Query &select(std::vector<std::string> cols);
    Query &from_table(std::string table);
    Query &where(std::string clause, py::object param);
    Query &limit(int n);
    Query build() const;

    const std::string &sql() const;
    std::vector<py::object> params() const;

    static Query for_connection(std::string_view conn);

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual std::unique_ptr<Concept> clone() const = 0;
        virtual void select(std::vector<std::string> cols) = 0;
        virtual void from_table(std::string table) = 0;
        virtual void where(std::string clause, py::object param) = 0;
        virtual void limit(int n) = 0;
        virtual void set_manual(std::string sql, std::vector<py::object> params) = 0;
        virtual void ensure_sql() const = 0;
        virtual const std::string &sql() const = 0;
        virtual std::vector<py::object> params_copy() const = 0;
    };

    template <typename Policy>
    struct Model final : Concept {
        Policy m_policy{};
        std::vector<std::string> m_columns;
        std::string m_table;
        std::vector<std::string> m_whereClauses;
        std::vector<py::object> m_params;
        std::optional<int> m_limit;
        mutable std::string m_sql;
        mutable bool m_dirty{true};
        bool m_manual_sql{false};

        Model() = default;
        Model(const Model &) = default;

        std::unique_ptr<Concept> clone() const override {
            return std::make_unique<Model<Policy>>(*this);
        }

        void select(std::vector<std::string> cols) override {
            activate_builder();
            m_columns = std::move(cols);
            m_dirty = true;
        }

        void from_table(std::string table) override {
            activate_builder();
            m_table = std::move(table);
            m_dirty = true;
        }

        void where(std::string clause, py::object param) override {
            activate_builder();
            m_whereClauses.push_back(std::move(clause));
            m_params.push_back(std::move(param));
            m_dirty = true;
        }

        void limit(int n) override {
            activate_builder();
            if (n <= 0) {
                m_limit.reset();
            } else {
                m_limit = n;
            }
            m_dirty = true;
        }

        void set_manual(std::string sql, std::vector<py::object> params) override {
            m_manual_sql = true;
            m_sql = std::move(sql);
            m_params = std::move(params);
            m_dirty = false;
        }

        void ensure_sql() const override {
            if (m_manual_sql) {
                return;
            }
            if (!m_dirty) {
                return;
            }
            m_sql.clear();
            m_policy.write_select(m_sql, m_columns);
            m_policy.write_from(m_sql, m_table);
            m_policy.write_where(m_sql, m_whereClauses);
            if (m_limit) {
                m_policy.write_limit(m_sql, *m_limit);
            }
            m_dirty = false;
        }

        const std::string &sql() const override {
            ensure_sql();
            return m_sql;
        }

        std::vector<py::object> params_copy() const override {
            return m_params;
        }

    private:
        void activate_builder() {
            if (!m_manual_sql) {
                return;
            }
            m_manual_sql = false;
            m_sql.clear();
            m_params.clear();
            m_columns.clear();
            m_table.clear();
            m_whereClauses.clear();
            m_limit.reset();
            m_dirty = true;
        }
    };

    explicit Query(std::unique_ptr<Concept> impl);
    static std::unique_ptr<Concept> make_default_model();

    std::unique_ptr<Concept> m_impl;

    friend class QueryFactory;
};

class QueryFactory {
public:
    static Query make_default();
    static Query make_for_connection(std::string_view conn);

private:
    friend class Query;
    template <typename Policy>
    static std::unique_ptr<Query::Concept> make_model();
};

} // namespace pygim::policy_query

namespace pygim {
using Query = policy_query::Query;
} // namespace pygim

// -------------------- Inline Implementations --------------------

namespace pygim::policy_query {

inline Query::Query() : Query(make_default_model()) {}

inline Query::Query(std::string sql, std::vector<py::object> params)
    : Query(make_default_model()) {
    m_impl->set_manual(std::move(sql), std::move(params));
}

inline Query::Query(std::unique_ptr<Concept> impl) : m_impl(std::move(impl)) {}

inline Query::~Query() = default;

inline Query::Query(const Query &other) : m_impl(other.m_impl ? other.m_impl->clone() : nullptr) {}

inline Query &Query::operator=(const Query &other) {
    if (this == &other) {
        return *this;
    }
    m_impl = other.m_impl ? other.m_impl->clone() : nullptr;
    return *this;
}

inline Query &Query::select(std::vector<std::string> cols) {
    m_impl->select(std::move(cols));
    return *this;
}

inline Query &Query::from_table(std::string table) {
    m_impl->from_table(std::move(table));
    return *this;
}

inline Query &Query::where(std::string clause, py::object param) {
    m_impl->where(std::move(clause), std::move(param));
    return *this;
}

inline Query &Query::limit(int n) {
    m_impl->limit(n);
    return *this;
}

inline Query Query::build() const {
    m_impl->ensure_sql();
    return Query(m_impl->clone());
}

inline const std::string &Query::sql() const {
    m_impl->ensure_sql();
    return m_impl->sql();
}

inline std::vector<py::object> Query::params() const {
    return m_impl->params_copy();
}

inline Query Query::for_connection(std::string_view conn) {
    return QueryFactory::make_for_connection(conn);
}

inline std::unique_ptr<Query::Concept> Query::make_default_model() {
    return QueryFactory::make_model<MssqlQueryPolicy>();
}

template <typename Policy>
inline std::unique_ptr<Query::Concept> QueryFactory::make_model() {
    return std::make_unique<Query::Model<Policy>>();
}

inline Query QueryFactory::make_default() {
    return Query(make_model<MssqlQueryPolicy>());
}

inline Query QueryFactory::make_for_connection(std::string_view /*conn*/) {
    // Only MSSQL policy is currently implemented. Future heuristics can inspect
    // the connection string to select a policy.
    return make_default();
}

} // namespace pygim::policy_query
