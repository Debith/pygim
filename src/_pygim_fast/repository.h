#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <vector>
#include <string>
#include <optional>
#include <stdexcept>

namespace pygim {
namespace py = pybind11;

// Lightweight transformer enable/disable compile-time style (boolean flag runtime for simplicity)
class Repository {
public:
    Repository(bool enable_transformers=false)
        : m_enable_transformers(enable_transformers) {}

    // Strategy registration (Python object must implement fetch/save methods)
    void add_strategy(py::object strategy) {
        m_strategies.push_back(std::move(strategy));
    }

    // Factory management (callable or None). Signature: factory(key, data) -> entity
    void set_factory(py::object factory) {
        m_factory = factory; m_has_factory = true;
    }
    void clear_factory() { m_factory = py::none(); m_has_factory = false; }

    bool has_factory() const noexcept { return m_has_factory; }

    // Transformer registration (no-op if disabled)
    void add_pre_transform(py::function f) {
        if (!m_enable_transformers) return;
        m_pre_save.push_back(std::move(f));
    }
    void add_post_transform(py::function f) {
        if (!m_enable_transformers) return;
        m_post_load.push_back(std::move(f));
    }

    py::object fetch_raw(const py::object& key) {
        for (auto & strat : m_strategies) {
            if (!py::hasattr(strat, "fetch")) continue;
            py::object out = strat.attr("fetch")(key);
            if (!out.is_none()) {
                return out; // raw (no transforms / factory)
            }
        }
        return py::none();
    }

    // Public get operation (mapping-like __getitem__ path)
    py::object get(const py::object& key) {
        py::object raw = fetch_raw(key);
        if (raw.is_none()) {
            throw std::runtime_error("Repository: key not found");
        }
        py::object current = raw;
        if (m_enable_transformers) {
            for (auto & t : m_post_load) {
                current = t(key, current);
            }
        }
        if (m_has_factory) {
            current = m_factory(key, current);
        }
        return current;
    }

    py::object get_default(const py::object& key, const py::object& default_value) {
        py::object raw = fetch_raw(key);
        if (raw.is_none()) return default_value;
        py::object current = raw;
        if (m_enable_transformers) {
            for (auto & t : m_post_load) current = t(key, current);
        }
        if (m_has_factory) current = m_factory(key, current);
        return current;
    }

    bool contains(const py::object& key) {
        // Fast path: attempt strategies until one returns non-None
        for (auto & strat : m_strategies) {
            if (!py::hasattr(strat, "fetch")) continue;
            py::object out = strat.attr("fetch")(key);
            if (!out.is_none()) return true;
        }
        return false;
    }

    // Save operation (mapping-like __setitem__ path). Value can be raw or entity.
    void save(const py::object& key, py::object value) {
        py::object current = value;
        if (m_enable_transformers) {
            for (auto & t : m_pre_save) current = t(key, current);
        }
        // Broadcast save to all strategies having 'save'
        bool any = false;
        for (auto & strat : m_strategies) {
            if (py::hasattr(strat, "save")) {
                strat.attr("save")(key, current);
                any = true;
            }
        }
        if (!any) throw std::runtime_error("Repository: no strategy accepted save() call");
    }

    // Optional bulk insert broadcast; strategies without bulk_insert are skipped.
    void bulk_insert(const std::string& table, const std::vector<std::string>& columns,
                     const py::object& rows, int batch_size=1000, const std::string& table_hint="TABLOCK") {
        bool any = false;
        for (auto & strat : m_strategies) {
            if (py::hasattr(strat, "bulk_insert")) {
                strat.attr("bulk_insert")(table, columns, rows, batch_size, table_hint);
                any = true;
            }
        }
        if (!any) throw std::runtime_error("Repository: no strategy supports bulk_insert()");
    }

    void bulk_upsert(const std::string& table, const std::vector<std::string>& columns,
                     const py::object& rows, const std::string& key_column="id", int batch_size=500, const std::string& table_hint="TABLOCK") {
        bool any = false;
        for (auto & strat : m_strategies) {
            if (py::hasattr(strat, "bulk_upsert")) {
                strat.attr("bulk_upsert")(table, columns, rows, key_column, batch_size, table_hint);
                any = true;
            }
        }
        if (!any) throw std::runtime_error("Repository: no strategy supports bulk_upsert()");
    }

    std::vector<py::object> strategies() const { return m_strategies; }
    std::vector<py::function> pre_transforms() const { return m_pre_save; }
    std::vector<py::function> post_transforms() const { return m_post_load; }

    std::string repr() const {
        return "Repository(strategies=" + std::to_string(m_strategies.size()) +
               ", transformers=" + (m_enable_transformers?"True":"False") +
               ", factory=" + (m_has_factory?"True":"False") + ")";
    }

private:
    std::vector<py::object> m_strategies;
    std::vector<py::function> m_pre_save;
    std::vector<py::function> m_post_load;
    py::object m_factory = py::none();
    bool m_has_factory = false;
    bool m_enable_transformers;
};

// MSSQL strategy moved to dedicated mssql_strategy.{h,cpp}

} // namespace pygim
