# type: ignore
"""Test data in one call: ``pygim.create_df`` and ``pygim.datagen.generate``.

A schema of column names and type names becomes a table of that many rows, made in C++ with
Arrow's builders and handed over without a copy. The same seed makes the same table every
time, and two seeds make two tables, so a test or a benchmark can name its data by its seed.

This example demonstrates:
- A schema of Arrow and SQL type names, and the table it makes
- What each kind of column holds: serial ids, named strings, values in a range
- Seeds: the same data again, and different data from another
- Nulls by fraction, and a PyArrow table when asked
- What is refused, and how
"""

import polars as pl
import pyarrow as pa

import pygim
from pygim import datagen

# ----------------------------------------------------------------------------
# 1. A schema, and the table it makes
# ----------------------------------------------------------------------------
#                       ┌─ schema: column name -> type name (Arrow's, or SQL's)
#                       ▼
users = pygim.create_df({"id": "serial", "name": "string", "score": "float64", "age": "tinyint"},
                        rows=1_000)
#                       ▲
#                       └─ rows: how many
assert isinstance(users, pl.DataFrame)          # polars by default
assert users.shape == (1_000, 4)
assert users["age"].dtype == pl.Int8            # SQL's tinyint is Arrow's int8

# ----------------------------------------------------------------------------
# 2. What each kind of column holds
# ----------------------------------------------------------------------------
assert users["id"].to_list()[:3] == [1, 2, 3]                     # serial: 1, 2, 3, ...
assert users["name"].to_list()[:2] == ["name_0000000", "name_0000001"]   # its name and its row
ages = users["age"]
assert ages.min() >= -120
assert ages.max() <= 120                        # each integer type has its range
assert ages.n_unique() > 100                    # spread over it

# ----------------------------------------------------------------------------
# 3. Seeds: the same table again, a different one from another
# ----------------------------------------------------------------------------
schema = {"id": "uuid", "amount": "int64"}
#                                    ┌─ seed: names the data; 42 when not given
#                                    ▼
first = pygim.create_df(schema, 100, seed=7)
again = pygim.create_df(schema, 100, seed=7)
other = pygim.create_df(schema, 100, seed=8)
assert first.equals(again)
assert not first.equals(other)                  # neighbouring seeds too

# ----------------------------------------------------------------------------
# 4. Nulls by fraction, and PyArrow when asked
# ----------------------------------------------------------------------------
nullable = {"id": "serial", "when": "timestamp"}
#                                         ┌─ null_fraction: each value's chance of being null
#                                         │                  ┌─ format: "polars" or "arrow"
#                                         ▼                  ▼
table = pygim.create_df(nullable, 10_000, null_fraction=0.2, format="arrow")
assert isinstance(table, pa.Table)
assert table.column("id").null_count == 0       # a serial id is never null
assert 1_800 < table.column("when").null_count < 2_200
assert table.schema.field("when").type == pa.timestamp("us")

# The stream itself, for a reader that takes Arrow's C stream (polars, DuckDB, pyarrow):
stream = datagen.generate({"x": "int32"}, rows=5)
assert pa.RecordBatchReader.from_stream(stream).read_all().num_rows == 5
assert "uniqueidentifier" not in datagen.supported_types()   # aliases are accepted, not listed
assert "uuid" in datagen.supported_types()

# ----------------------------------------------------------------------------
# 5. What is refused
# ----------------------------------------------------------------------------
refusals = []
for call in (lambda: pygim.create_df({"x": "decimal"}),             # a type it does not know
             lambda: pygim.create_df({"x": "int32"}, rows=0),        # no rows
             lambda: pygim.create_df({"x": "int32"}, format="pandas")):   # nor this format
    try:
        call()
    except ValueError as e:
        refusals.append(str(e).split(":")[0])
expected = ["Unknown column type", "rows must be positive",
            "format must be 'polars' or 'arrow', not 'pandas'"]
assert refusals == expected                      # each a ValueError that says why

print(f"datagen example OK: {users.shape[0]} users, {table.num_rows} timestamps "
      f"({table.column('when').null_count} null), seeds 7 and 8 apart")
