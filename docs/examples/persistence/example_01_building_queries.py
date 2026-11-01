# type: ignore
"""Building and rendering SQL with ``pygim.persistence.Query`` — no database needed.

A ``Query`` holds intent: which columns, which table, which rows, how many. A
dialect turns that intent into the SQL its backend speaks. Neither needs a
connection, a store or a session, so a query can be built, read, logged and
tested anywhere; ``DataStore.load(query)`` is what finally runs it, rendering it
with the same function shown here.

This example demonstrates:
- The fluent builder, and the same query built from its parts
- Rendering for SQL Server: ``[bracket]`` quoting, ``TOP N``, schema-qualified tables
- Raw SQL, which every dialect passes through unchanged
- Quoting one identifier, including the escape for an embedded ``]``
"""

from pygim.persistence import MssqlDialect, Query

# ----------------------------------------------------------------------------
# 1. Build a query from plain values
# ----------------------------------------------------------------------------
# Every step stores intent on the same object and returns it, so steps chain.
#
#                  ┌─ col: one column; call again for more
#                  │                               ┌─ table: optionally schema-qualified
#                  ▼                               ▼
q = Query().select("id").select("name").from_table("dbo.users")
q.where("active = 1").limit(10)
#       ▲                   ▲
#       │                   └─ n: keep at most n rows
#       └─ clause: the WHERE condition, as SQL text

# A query is a value: the same intent built from its parts is the same query.
assert q == Query(columns=["id", "name"], table="dbo.users", where="active = 1", limit=10)
assert q.table == "dbo.users"                  # and each part reads back


# ----------------------------------------------------------------------------
# 2. Render it for SQL Server
# ----------------------------------------------------------------------------
# The dialect needs nothing to construct: no connection string, no store.
dialect = MssqlDialect()

assert dialect.render(q) == "SELECT TOP 10 [id], [name] FROM [dbo].[users] WHERE active = 1"

# Without columns the query selects everything; without limit there is no TOP.
assert dialect.render(Query().from_table("users")) == "SELECT * FROM [users]"


# ----------------------------------------------------------------------------
# 3. Raw SQL passes through
# ----------------------------------------------------------------------------
# A query given as SQL is already in the backend's language: rendering returns it
# unchanged, exactly as DataStore.load would run it.
#
#           ┌─ raw_sql: used as-is; the builder steps do not apply to it
#           ▼
raw = Query("SELECT COUNT(*) FROM dbo.users")
assert raw.is_raw() is True
assert dialect.render(raw) == "SELECT COUNT(*) FROM dbo.users"


# ----------------------------------------------------------------------------
# 4. Quote one identifier
# ----------------------------------------------------------------------------
# SQL Server quotes with brackets and escapes an embedded ']' by doubling it,
# so any column name — spaces, keywords, brackets — becomes a safe identifier.
assert dialect.quote_identifier("order") == "[order]"
assert dialect.quote_identifier("my col") == "[my col]"
assert dialect.quote_identifier("a]b") == "[a]]b]"

print(f"persistence query example OK: {raw.is_raw()} raw, {q.table}")
