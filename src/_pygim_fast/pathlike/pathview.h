#pragma once
// pathlike/pathview.h — a path as a handle on a row of a path_table: (table, row, pin).
//
// A handle carries no parsed value of its own: its name, parent, suffix, equality and hash are
// read from the table by row, and the core value (basic_file, core.h) is built on demand for the
// few operations whose rules live there (with_suffix, joins with complex text, URIs, filesystem
// calls), then interned back into the same table. Every derived handle inherits the table of the
// one it came from.
//
// Two handles on one row are equal and hash alike, and are not promised to be one object — as
// CPython interns strings for storage and fast equality without promising `is`. A handle keeps
// its table alive (shared_ptr), so a table lives as long as any handle or set over it.
//
// Threading: a table is append-only with a single writer at a time, and reads never lock.
//
//     pathview p{table, intern_text(*table, "a/b/c.yaml"), nullptr};   rows: 0 ".", 1 a, 2 a/b, 3 a/b/c.yaml
//     p.name() -> "c.yaml"     p.ext_key() -> ".yaml"     p.at(2).fspath() -> "a/b"
//
// CORE layer: pybind-free. Python's `pygim.path` and its typed classes are adapter/path_bindings.h.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "core.h"
#include "path_store.h"
#include "path_table.h"

namespace pygim::pathlike {

/// The handle. Copies nothing; keeps the table alive.
class pathview {
public:
    pathview(table_ptr table, std::uint32_t row, const engine_info* pin) noexcept : m_table(std::move(table)), m_row(row), m_pin(pin) {}

    [[nodiscard]] const table_ptr& table() const noexcept { return m_table; }
    [[nodiscard]] std::uint32_t row() const noexcept { return m_row; }
    /// The engine pinned at construction, inherited by derived paths; null for none.
    [[nodiscard]] const engine_info* pin() const noexcept { return m_pin; }
    /// This handle moved onto another row of its table: PathSet.scan()'s one object, advancing.
    void move_to(std::uint32_t r) noexcept { m_row = r; }

    /// The core value of the row, with the pin: what the engines' load/write
    /// and the value-route operations take.
    [[nodiscard]] file value() const { return file(m_table->value(m_row), m_pin); }
    /// pathlib's str() of the row (path_table::render).      p.fspath() -> "a/b/c.yaml"
    [[nodiscard]] std::string fspath() const { return m_table->render<native_strategy>(m_row); }
    /// The same written into `out`, reusing its room.
    void fspath_into(std::string& out) const { m_table->render_into<native_strategy>(m_row, out); }
    /// The final component (a view into the arena).          p.name() -> "c.yaml"
    [[nodiscard]] std::string_view name() const noexcept { return m_table->name(m_row); }
    /// The lower-cased final suffix: the registry's dispatch key.   p.ext_key() -> ".yaml"
    [[nodiscard]] std::string ext_key() const { return chars::lower(detail::suffix_of(name())); }
    /// Room for a lower-cased suffix: every engine's is far shorter.
    static constexpr std::size_t ext_key_room = 32;
    /// The same into a caller's buffer, no allocation for suffixes up to ext_key_room bytes.
    [[nodiscard]] std::string_view ext_key_into(char (&buf)[ext_key_room]) const noexcept {
        const std::string_view sfx = detail::suffix_of(name());
        if (sfx.size() > sizeof(buf)) return sfx;   // dispatch will miss; a suffix this long has no engine
        for (std::size_t i = 0; i < sfx.size(); ++i) buf[i] = chars::to_lower(sfx[i]);
        return {buf, sfx.size()};
    }
    /// Same path? Same table: same row. Different tables: `o`'s chain looked up here.
    [[nodiscard]] bool equals(const pathview& o) const {
        if (m_table.get() == o.m_table.get()) return m_row == o.m_row;
        return m_table->find(*o.m_table, o.m_row) == m_row;
    }
    /// Another row of the same table, the same pin.           p.at(2) -> p.parent
    [[nodiscard]] pathview at(std::uint32_t r) const { return {m_table, r, m_pin}; }
    /// A value interned into this handle's table, the same pin: the tail of
    /// every value-route operation.
    [[nodiscard]] pathview intern(const file& f) const {
        const std::uint32_t r = m_table->insert<native_strategy>(f.value());
        return {m_table, r, m_pin};
    }

private:
    table_ptr m_table;
    std::uint32_t m_row;
    const engine_info* m_pin;
};

/// The row for path TEXT in a table: native text (no scheme) is tokenised
/// straight in; a file:// URI goes through the core constructor, which owns
/// the URI rules (and raises for a remote host or a relative URI).
[[nodiscard]] inline std::uint32_t intern_text(path_table& t, std::string_view text) {
    if (uri::scheme_of(text).empty()) return t.insert<native_strategy>(text);
    return t.insert<native_strategy>(file(text).value());
}

}  // namespace pygim::pathlike
