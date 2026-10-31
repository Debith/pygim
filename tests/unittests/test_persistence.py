import os
import re
import uuid

import pytest

# The persistence extension is a mandatory part of the library: if it is
# missing, this module must fail to import rather than silently skip.
# (tests/conftest.py preloads Arrow so the extension resolves libarrow.)
# Everything here goes through the public module, the way a user reaches it.
from pygim import persistence as _persistence_module
from pygim.persistence import Format, MssqlDialect, Query, acquire_datastore


# ─── Fixtures ────────────────────────────────────────────────────────────────


@pytest.fixture(params=["polars", "pandas"], ids=["polars", "pandas"])
def fmt(request):
    return request.param


@pytest.fixture
def repo(fmt):
    return acquire_datastore("test_conn", format=fmt, pool_size=2)


@pytest.fixture
def dialect():
    return MssqlDialect()


# ─── Adapter: Construction & Repr ────────────────────────────────────────────

REPR_RE = re.compile(
    r"^DataStore\(backend=mssql, format=(polars|pandas), transforms=\d+/\d+\)$"
)


def _default_integration_conn_str():
    password = os.getenv("MSSQL_SA_PASSWORD", "NewP@ssw0rd#2025")
    return (
        "Driver={ODBC Driver 18 for SQL Server};Server=localhost,1433;"
        f"Database=tempdb;UID=sa;PWD={password};"
        "Encrypt=yes;TrustServerCertificate=yes;"
    )


def _integration_conn_str(pyodbc):
    conn_str = os.getenv("STRESS_CONN", "").strip() or _default_integration_conn_str()
    try:
        conn = pyodbc.connect(conn_str, timeout=2)
    except pyodbc.Error as exc:
        pytest.skip(
            "Live MSSQL integration unavailable; set STRESS_CONN or run the "
            f"local Docker SQL Server on localhost:1433 ({exc})"
        )
    else:
        conn.close()
    return conn_str


def test_construction(repo, fmt):
    assert REPR_RE.match(repr(repo)), f"Unexpected repr: {repr(repo)}"
    assert fmt in repr(repo)


def test_format_property(repo, fmt):
    assert repo.format.name == fmt


def test_repr_contract(repo):
    assert REPR_RE.match(repr(repo))


# ─── Adapter: Save / Load ───────────────────────────────────────────────────


def test_save_requires_arrow_data(repo):
    """save() now requires Arrow-compatible data as first argument."""
    with pytest.raises(TypeError, match="Arrow-compatible object"):
        repo.save("not_arrow_data", "table_name", bcp_workers=1)


def test_load_fails_without_odbc(repo):
    """load() fails at ODBC connect with fake connection string."""
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        repo.load("table_name", load_workers=1)


def test_load_raw_sql_string_fails_without_odbc(repo):
    """load() with raw SQL fails at ODBC connect."""
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        repo.load("SELECT * FROM foo", load_workers=1)


def test_load_with_query_fails_without_odbc(repo):
    q = Query().select("id").from_table("users").where("id > 5").limit(10)
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        repo.load(q, load_workers=1)


# ─── Adapter: Transforms ────────────────────────────────────────────────────


def test_transforms_repr_counts(repo):
    assert "transforms=0/0" in repr(repo)
    repo.add_pre_transform(lambda: None)
    assert "transforms=1/0" in repr(repo)
    repo.add_post_transform(lambda: None)
    assert "transforms=1/1" in repr(repo)


def test_transform_execution(repo):
    """Pre-transforms execute before ODBC failure; post-transforms don't."""
    log = []
    repo.add_pre_transform(lambda: log.append("pre"))
    repo.add_post_transform(lambda: log.append("post"))
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        repo.load("t", load_workers=1)
    assert log == ["pre"]  # post not reached due to ODBC failure


def test_transform_execution_order(repo):
    """Pre-transforms run in order before ODBC operations."""
    log = []
    repo.add_pre_transform(lambda: log.append("pre1"))
    repo.add_pre_transform(lambda: log.append("pre2"))
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        repo.load("t", load_workers=1)
    assert log == ["pre1", "pre2"]


def test_clear_transforms(repo):
    repo.add_pre_transform(lambda: None)
    repo.add_post_transform(lambda: None)
    assert "transforms=1/1" in repr(repo)
    repo.clear_transforms()
    assert "transforms=0/0" in repr(repo)


def test_multiple_transforms(repo):
    log = []
    repo.add_pre_transform(lambda: log.append("pre1"))
    repo.add_pre_transform(lambda: log.append("pre2"))
    repo.add_post_transform(lambda: log.append("post1"))
    assert "transforms=2/1" in repr(repo)
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        repo.load("t", load_workers=1)
    assert log == ["pre1", "pre2"]  # post not reached


# ─── Adapter: Pool Size ─────────────────────────────────────────────────────


@pytest.mark.parametrize("pool_size", [1, 8])
def test_pool_size(pool_size):
    r = acquire_datastore("conn", format="polars", pool_size=pool_size)
    assert REPR_RE.match(repr(r))


# ─── Constructor: New Params (batch_size, table_hint, bcp_workers) ───────────


@pytest.mark.parametrize("batch_size", [1, 50_000, 500_000])
def test_batch_size_accepted(batch_size):
    r = acquire_datastore("conn", format="polars", batch_size=batch_size)
    assert REPR_RE.match(repr(r))


@pytest.mark.parametrize("table_hint", ["TABLOCK", "NOLOCK", ""])
def test_table_hint_accepted(table_hint):
    r = acquire_datastore("conn", format="polars", table_hint=table_hint)
    assert REPR_RE.match(repr(r))


@pytest.mark.parametrize("bcp_workers", [1, 4, 8])
def test_bcp_workers_accepted(bcp_workers):
    r = acquire_datastore("conn", format="polars", bcp_workers=bcp_workers)
    assert REPR_RE.match(repr(r))


def test_all_new_params_combined():
    r = acquire_datastore(
        "conn",
        format="polars",
        pool_size=2,
        batch_size=50_000,
        table_hint="NOLOCK",
        bcp_workers=4,
    )
    assert REPR_RE.match(repr(r))


@pytest.mark.parametrize("fmt", ["polars", "pandas"])
def test_acquire_datastore_new_params(fmt):
    """Production acquire_datastore accepts new constructor params."""
    store = acquire_datastore(
        "conn",
        format=fmt,
        batch_size=10_000,
        table_hint="TABLOCK",
        bcp_workers=2,
    )
    assert store.format.name == fmt


# ─── Query: Builder ─────────────────────────────────────────────────────────


def test_query_fluent_builder():
    q = Query().select("id").from_table("t").where("x>1").limit(5)
    assert q.table == "t"
    assert q.columns == ["id"]
    assert q.where_clause == "x>1"
    assert q.limit_value == 5
    assert q.is_raw() is False


def test_query_raw_sql():
    q = Query("SELECT 1")
    assert q.is_raw() is True
    assert q.raw_sql == "SELECT 1"


def test_query_empty():
    q = Query()
    assert q.is_raw() is False
    assert q.table == ""
    assert q.columns == []


def test_query_multiple_selects():
    q = Query().select("a").select("b")
    assert q.columns == ["a", "b"]


# ─── Dialect ─────────────────────────────────────────────────────────────────


def test_dialect_render_simple(dialect):
    q = Query().from_table("users")
    assert dialect.render(q) == "SELECT * FROM [users]"


def test_dialect_render_with_limit(dialect):
    q = Query().select("id").from_table("users").limit(5)
    assert dialect.render(q) == "SELECT TOP 5 [id] FROM [users]"


def test_dialect_render_with_columns(dialect):
    q = Query().select("col1").select("col2").from_table("t")
    assert dialect.render(q) == "SELECT [col1], [col2] FROM [t]"


def test_dialect_quote_identifier(dialect):
    assert dialect.quote_identifier("name") == "[name]"


def test_dialect_quote_identifier_escaping(dialect):
    assert dialect.quote_identifier("a]b") == "[a]]b]"


def test_dialect_quote_identifier_space(dialect):
    assert dialect.quote_identifier("my col") == "[my col]"


# ─── Public Query and dialect: plain values, no session, no database ────────


def test_query_and_dialect_are_public():
    assert {"Query", "MssqlDialect"} <= set(_persistence_module.__all__)


def test_a_query_renders_without_a_session_or_a_database():
    q = Query().select("id").from_table("dbo.users").where("id > 5").limit(10)
    assert MssqlDialect().render(q) == "SELECT TOP 10 [id] FROM [dbo].[users] WHERE id > 5"


def test_raw_sql_renders_unchanged():
    """A raw query is SQL already: render passes it through, as load does (core::build_sql).
    Rendering the dialect directly gave 'SELECT * FROM []'."""
    assert MssqlDialect().render(Query("SELECT 1")) == "SELECT 1"


def test_a_public_query_reaches_the_store():
    store = acquire_datastore("conn", format="polars", pool_size=1)
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        store.load(Query().from_table("users"), load_workers=1)


# ─── Integration: Production API ─────────────────────────────────────────────


@pytest.mark.parametrize("fmt", ["polars", "pandas"])
def test_acquire_datastore_save_load(fmt):
    store = acquire_datastore("conn", format=fmt, pool_size=2)
    # save requires Arrow data, load requires ODBC
    with pytest.raises(TypeError, match="Arrow-compatible"):
        store.save("not_arrow", "table", bcp_workers=1)
    with pytest.raises(RuntimeError, match="SQLDriverConnect"):
        store.load("table", load_workers=1)
    assert store.format.name == fmt


def test_acquire_datastore_invalid_format():
    with pytest.raises(ValueError, match="Unknown format"):
        acquire_datastore("conn", format="arrow")


def test_acquire_datastore_live_round_trip():
    pyodbc = pytest.importorskip("pyodbc")
    conn_str = _integration_conn_str(pyodbc)   # first: no server is a skip, whatever else is missing
    import polars as pl  # hard dependency of pygim: must be present wherever pygim imports

    table_name = f"pygim_persist_{uuid.uuid4().hex[:8]}"
    qualified_table = f"dbo.{table_name}"
    df = pl.DataFrame(
        {
            "id": pl.Series([1, 2, 3], dtype=pl.Int32),
            "val_i32": pl.Series([10, 20, 30], dtype=pl.Int32),
            "val_i64": pl.Series(
                [10_000_000_001, 10_000_000_002, 10_000_000_003], dtype=pl.Int64
            ),
            "val_f64": pl.Series([1.25, 2.5, 3.75], dtype=pl.Float64),
            "val_str": ["first", "second", "third"],
        }
    )
    store = acquire_datastore(
        conn_str, format="polars", batch_size=1_000, bcp_workers=1
    )
    conn = pyodbc.connect(conn_str, timeout=30)
    conn.autocommit = True

    try:
        conn.execute(
            f"CREATE TABLE {qualified_table} ("
            "id INT NOT NULL PRIMARY KEY, "
            "val_i32 INT NOT NULL, "
            "val_i64 BIGINT NOT NULL, "
            "val_f64 FLOAT NOT NULL, "
            "val_str NVARCHAR(100) NOT NULL)"
        )

        store.save(df, qualified_table)

        expected_rows = df.sort("id").to_dicts()
        loaded_table = (
            store.load(qualified_table, load_workers=1).select(df.columns).sort("id")
        )
        loaded_sql = store.load(
            f"SELECT id, val_i32, val_i64, val_f64, val_str FROM {qualified_table} ORDER BY id",
            load_workers=1,
        ).select(df.columns)

        assert loaded_table.to_dicts() == expected_rows
        assert loaded_sql.to_dicts() == expected_rows
    finally:
        conn.execute(f"DROP TABLE IF EXISTS {qualified_table}")
        conn.close()


# NOTE: save() returns a dict with metrics (processed_rows, total_seconds, etc.)
# but testing the return value requires a live ODBC connection + real database.
# The TypeError tests above verify the signature; metric tests belong in integration.


# ─── Format Enum ─────────────────────────────────────────────────────────────


def test_format_values():
    assert Format.polars is not None
    assert Format.pandas is not None
    assert Format.polars != Format.pandas


# ─── Protocol Conformance ────────────────────────────────────────────────────


def test_datastore_satisfies_repository_protocol():
    """DataStore structurally satisfies the Repository protocol from interfaces.py."""
    from pygim.core.protocols import Repository as RepositoryProtocol

    store = acquire_datastore("test_conn", format="polars", pool_size=1)
    assert isinstance(store, RepositoryProtocol), (
        "DataStore must satisfy the Repository protocol (load + save)"
    )


# ─── Public Module Import ────────────────────────────────────────────────────


def test_public_module_reexports():
    """Verify pygim.persistence re-exports match the compiled extension."""
    from pygim.persistence import DataStore as PubDataStore
    from pygim.persistence import Format as PubFormat
    from pygim.persistence import MssqlDialect as PubMssqlDialect
    from pygim.persistence import Query as PubQuery
    from pygim.persistence import acquire_datastore as pub_acquire

    # Must be the exact same objects as the direct extension imports
    assert PubDataStore is _persistence_module.DataStore
    assert pub_acquire is _persistence_module.acquire_datastore
    assert PubQuery is Query and PubMssqlDialect is MssqlDialect

    # Enum values should match across modules
    assert PubFormat.polars.name == Format.polars.name
    assert PubFormat.pandas.name == Format.pandas.name
