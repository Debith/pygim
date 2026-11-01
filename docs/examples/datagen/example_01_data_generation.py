# type: ignore
"""Data generation in one call: ``pygim.datagen.generate``.

A schema of column names and type names becomes that many rows of data, made in C++ with Arrow's
builders and handed over as Arrow's C stream. polars, pyarrow and DuckDB build their own table
from it with no copy: the example names the library, and pygim names none. The same seed makes
the same data every time, and two seeds make two, so a test or a benchmark can name its data by
its seed.

This example demonstrates:
- A schema of Arrow and SQL type names, and a table built from its data
- What each kind of column holds: serial ids, named strings, values in a range
- Seeds: the same data again, and different data from another
- Nulls by fraction, and the same data read twice
- What is refused, and how
"""

import polars as pl
import pyarrow as pa

from pygim import datagen

# ----------------------------------------------------------------------------
# 1. A schema, and a table built from its data
# ----------------------------------------------------------------------------
# column name -> type name (Arrow's, or SQL's)
schema = {"id": "serial", "name": "string", "score": "float64", "age": "tinyint"}
#                                             ┌─ rows: how many
#                                             ▼
users = pl.DataFrame(datagen.generate(schema, rows=1_000))   # polars builds its own frame
assert users.shape == (1_000, 4)
assert users["age"].dtype == pl.Int8            # SQL's tinyint is Arrow's int8

# ----------------------------------------------------------------------------
# 2. What each kind of column holds
# ----------------------------------------------------------------------------
assert list(users["id"][:3]) == [1, 2, 3]                         # serial: 1, 2, 3, ...
assert list(users["name"][:2]) == ["name_0000000", "name_0000001"]       # its name and its row
ages = users["age"]
assert ages.min() >= -120
assert ages.max() <= 120                        # each integer type has its range
assert ages.n_unique() > 100                    # spread over it

# ----------------------------------------------------------------------------
# 3. Seeds: the same data again, different data from another
# ----------------------------------------------------------------------------
payments = {"id": "uuid", "amount": "int64"}
#                                                ┌─ seed: names the data; 42 when not given
#                                                ▼
first = pa.table(datagen.generate(payments, 100, seed=7))
again = pa.table(datagen.generate(payments, 100, seed=7))
other = pa.table(datagen.generate(payments, 100, seed=8))
assert first.equals(again)
assert not first.equals(other)                  # neighbouring seeds too

# ----------------------------------------------------------------------------
# 4. Nulls by fraction, and the same data read twice
# ----------------------------------------------------------------------------
nullable = {"id": "serial", "when": "timestamp"}
#                                         ┌─ null_fraction: each value's chance of being null
#                                         ▼
data = datagen.generate(nullable, 10_000, null_fraction=0.2)
table = pa.table(data)                          # pyarrow's table this time
assert table.column("id").null_count == 0       # a serial id is never null
assert 1_800 < table.column("when").null_count < 2_200
assert table.schema.field("when").type == pa.timestamp("us")
assert pa.table(data).equals(table)             # read again: a fresh stream, the same data
assert "uniqueidentifier" not in datagen.supported_types()   # aliases are accepted, not listed
assert "uuid" in datagen.supported_types()

# ----------------------------------------------------------------------------
# 5. What is refused
# ----------------------------------------------------------------------------
refusals = []
for call in (lambda: datagen.generate({"x": "decimal"}),             # a type it does not know
             lambda: datagen.generate({"x": "int32"}, rows=0)):      # no rows
    try:
        call()
    except ValueError as e:
        refusals.append(str(e).split(":")[0])
assert refusals == ["Unknown column type", "rows must be positive"]   # a ValueError that says why
try:
    datagen.generate({"x": "int32"}, 100, 7)                       # a seed is named, never placed
    raise AssertionError("a seed given by place is refused")
except TypeError:
    pass

print(f"datagen example OK: {users.shape[0]} users, {table.num_rows} timestamps "
      f"({table.column('when').null_count} null), seeds 7 and 8 apart")
