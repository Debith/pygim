"""pygim.datagen.generate and pygim.create_df: columns of test data, made in C++, as Arrow."""

from __future__ import annotations

import locale

import pytest

pa = pytest.importorskip("pyarrow")

import pygim  # noqa: E402  (after pyarrow: the extension links against Arrow's libraries)
from pygim import datagen  # noqa: E402

ARROW_TYPES = {
    "int8": pa.int8(), "int16": pa.int16(), "int32": pa.int32(), "int64": pa.int64(),
    "uint8": pa.uint8(), "uint16": pa.uint16(), "uint32": pa.uint32(), "uint64": pa.uint64(),
    "bool": pa.bool_(), "float32": pa.float32(), "float64": pa.float64(), "string": pa.utf8(),
    "date": pa.date32(), "time": pa.time64("ns"), "timestamp": pa.timestamp("us"),
    "duration": pa.duration("us"), "binary": pa.binary(), "uuid": pa.binary(16), "serial": pa.int32(),
}

SQL_ALIASES = {
    "tinyint": "int8", "smallint": "int16", "bigint": "int64", "bit": "bool", "real": "float32",
    "double": "float64", "nvarchar": "string", "varchar": "string", "datetime": "timestamp",
    "datetime2": "timestamp", "varbinary": "binary", "uniqueidentifier": "uuid",
}


def table(schema, rows=100, **options):
    return pa.RecordBatchReader.from_stream(datagen.generate(schema, rows, **options)).read_all()


@pytest.mark.parametrize("name", sorted(ARROW_TYPES))
def test_each_type_is_its_arrow_type(name):
    assert table({"c": name}).schema.field("c").type == ARROW_TYPES[name]


def test_every_listed_type_is_one_generate_takes():
    assert sorted(datagen.supported_types()) == sorted(ARROW_TYPES)


@pytest.mark.parametrize("alias", sorted(SQL_ALIASES))
def test_a_sql_name_is_the_type_it_names(alias):
    assert table({"c": alias}).schema.field("c").type == ARROW_TYPES[SQL_ALIASES[alias]]


def test_a_type_name_is_read_in_any_case():
    assert table({"a": "INT", "b": "BigInt", "c": "UniqueIdentifier"}).schema.types == [pa.int32(), pa.int64(), pa.binary(16)]


@pytest.mark.parametrize("name", ["tr_TR.ISO8859-9", "tr_TR.ISO-8859-9", "tr_TR.UTF-8"])
def test_a_type_name_is_read_in_any_case_whatever_the_locale(name):
    """Turkish lowers I to a dotless i: a name read by the locale's case would miss "int"."""
    before = locale.setlocale(locale.LC_CTYPE)
    try:
        locale.setlocale(locale.LC_CTYPE, name)
    except locale.Error:
        pytest.skip(f"no {name} locale here")
    try:
        assert table({"a": "INT", "b": "TIMESTAMP"}).schema.types == [pa.int32(), pa.timestamp("us")]
    finally:
        locale.setlocale(locale.LC_CTYPE, before)


def test_an_unknown_type_is_refused_with_the_ones_there_are():
    with pytest.raises(ValueError, match=r"Unknown column type: 'decimal'.*int8.*serial"):
        datagen.generate({"c": "decimal"})


@pytest.mark.parametrize(("arguments", "message"), [
    (({"c": "int32"}, 0), "rows must be positive"),
    (({"c": "int32"}, -5), "rows must be positive"),
    (({}, 10), "schema must have at least one column"),
])
def test_what_cannot_be_made_is_refused(arguments, message):
    with pytest.raises(ValueError, match=message):
        datagen.generate(*arguments)


@pytest.mark.parametrize("fraction", [-0.1, 1.5])
def test_a_null_fraction_outside_zero_to_one_is_refused(fraction):
    with pytest.raises(ValueError, match=r"null_fraction must be in \[0.0, 1.0\]"):
        datagen.generate({"c": "int32"}, 10, null_fraction=fraction)


def test_the_rows_asked_are_the_rows_made():
    assert table({"a": "int32", "b": "string"}, rows=1234).num_rows == 1234


def test_one_seed_makes_the_same_data_every_time():
    schema = {name: name for name in ARROW_TYPES}
    assert table(schema, seed=7).equals(table(schema, seed=7))


@pytest.mark.parametrize(("one", "other"), [(42, 43), (0, 1), (6, 7)])
def test_two_seeds_make_different_data(one, other):
    """Found 2026-10-31: the generator started from seed | 1, so every even seed made what the odd
    one after it made."""
    schema = {"a": "int64", "b": "float64", "c": "uuid"}
    assert not table(schema, seed=one).equals(table(schema, seed=other))


def test_a_serial_column_counts_from_one():
    assert table({"id": "serial"}, rows=5).column("id").to_pylist() == [1, 2, 3, 4, 5]


def test_a_string_is_its_column_name_and_row():
    assert table({"name": "string"}, rows=3).column("name").to_pylist() == ["name_0000000", "name_0000001", "name_0000002"]


def test_a_string_of_a_long_column_name_is_whole():
    """Found 2026-10-31: the text was written into 128 bytes and appended at the length snprintf
    would have written, so a name of 120 characters or more read past the buffer."""
    name = "c" * 200
    assert table({name: "string"}, rows=2).column(name).to_pylist() == [f"{name}_0000000", f"{name}_0000001"]


def test_no_null_fraction_makes_no_nulls():
    made = table({name: name for name in ARROW_TYPES if name != "serial"}, rows=500)
    assert [column.null_count for column in made.columns] == [0] * made.num_columns


def test_a_null_fraction_of_one_makes_every_value_null():
    made = table({name: name for name in ARROW_TYPES if name != "serial"}, rows=50, null_fraction=1.0)
    assert [column.null_count for column in made.columns] == [50] * made.num_columns


def test_a_null_fraction_makes_about_that_share_null():
    nulls = table({"c": "int32"}, rows=10_000, null_fraction=0.25).column("c").null_count
    assert 2_200 < nulls < 2_800


def test_a_serial_column_is_never_null():
    assert table({"id": "serial"}, rows=100, null_fraction=1.0).column("id").null_count == 0


@pytest.mark.parametrize(("name", "low", "high"), [
    ("int8", -120, 120), ("int16", -15_000, 15_000), ("int32", 0, 100_000), ("uint8", 0, 250),
])
def test_an_integer_stays_in_its_range(name, low, high):
    values = table({"c": name}, rows=5_000).column("c").to_pylist()
    assert low <= min(values) and max(values) <= high and len(set(values)) > 100


def test_a_bool_column_holds_both():
    values = table({"c": "bool"}, rows=1_000).column("c").to_pylist()
    assert 400 < sum(values) < 600


def test_create_df_is_polars_by_default():
    pl = pytest.importorskip("polars")
    df = pygim.create_df({"id": "serial", "name": "string"}, rows=3)
    assert isinstance(df, pl.DataFrame) and df["id"].to_list() == [1, 2, 3]


def test_create_df_is_an_arrow_table_when_asked():
    made = pygim.create_df({"id": "uuid"}, rows=3, format="arrow")
    assert isinstance(made, pa.Table) and made.schema.field("id").type == pa.binary(16)


@pytest.mark.parametrize("format", ["pandas", "Arrow", ""])
def test_create_df_refuses_a_format_it_does_not_make(format):
    """Found 2026-10-31: any format but "arrow" gave a polars DataFrame, so a typo was never told."""
    with pytest.raises(ValueError, match=r"format must be 'polars' or 'arrow'"):
        pygim.create_df({"id": "serial"}, rows=3, format=format)
