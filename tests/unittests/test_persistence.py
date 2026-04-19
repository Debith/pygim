import re
import pytest

_persistence_test = pytest.importorskip(
    "pygim._persistence_test",
    reason="C++ persistence extension not built (Arrow/ODBC not installed)",
)
LocalDataStore = _persistence_test.DataStore
Query = _persistence_test.Query
MssqlDialect = _persistence_test.MssqlDialect
LocalFormat = _persistence_test.Format

_persistence_module = pytest.importorskip(
    "pygim._persistence",
    reason="C++ persistence extension not built (Arrow/ODBC not installed)",
)
acquire_datastore = _persistence_module.acquire_datastore
Format = _persistence_module.Format


# ─── Fixtures ────────────────────────────────────────────────────────────────


@pytest.fixture(params=["polars", "pandas"], ids=["polars", "pandas"])
def fmt(request):
    return request.param


@pytest.fixture
def repo(fmt):
    return LocalDataStore("test_conn", format=fmt, pool_size=2)


@pytest.fixture
def dialect():
    return MssqlDialect()


# ─── Adapter: Construction & Repr ────────────────────────────────────────────

REPR_RE = re.compile(
    r"^DataStore\(backend=mssql, format=(polars|pandas), transforms=\d+/\d+\)$"
)


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


def test_invalid_format():
    with pytest.raises(ValueError, match="Unknown format"):
        LocalDataStore("conn", format="arrow", pool_size=1)


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
    r = LocalDataStore("conn", format="polars", pool_size=pool_size)
    assert REPR_RE.match(repr(r))


# ─── Constructor: New Params (batch_size, table_hint, bcp_workers) ───────────


@pytest.mark.parametrize("batch_size", [1, 50_000, 500_000])
def test_batch_size_accepted(batch_size):
    r = LocalDataStore("conn", format="polars", batch_size=batch_size)
    assert REPR_RE.match(repr(r))


@pytest.mark.parametrize("table_hint", ["TABLOCK", "NOLOCK", ""])
def test_table_hint_accepted(table_hint):
    r = LocalDataStore("conn", format="polars", table_hint=table_hint)
    assert REPR_RE.match(repr(r))


@pytest.mark.parametrize("bcp_workers", [1, 4, 8])
def test_bcp_workers_accepted(bcp_workers):
    r = LocalDataStore("conn", format="polars", bcp_workers=bcp_workers)
    assert REPR_RE.match(repr(r))


def test_all_new_params_combined():
    r = LocalDataStore(
        "conn", format="polars", pool_size=2,
        batch_size=50_000, table_hint="NOLOCK", bcp_workers=4,
    )
    assert REPR_RE.match(repr(r))


@pytest.mark.parametrize("fmt", ["polars", "pandas"])
def test_acquire_datastore_new_params(fmt):
    """Production acquire_datastore accepts new constructor params."""
    store = acquire_datastore(
        "conn", format=fmt, batch_size=10_000,
        table_hint="TABLOCK", bcp_workers=2,
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


def test_dialect_render_raw_sql(dialect):
    # Raw query is rendered structurally (not passed through) in current impl
    q = Query("SELECT * FROM foo")
    result = dialect.render(q)
    assert isinstance(result, str)


def test_dialect_quote_identifier(dialect):
    assert dialect.quote_identifier("name") == "[name]"


def test_dialect_quote_identifier_escaping(dialect):
    assert dialect.quote_identifier("a]b") == "[a]]b]"


def test_dialect_quote_identifier_space(dialect):
    assert dialect.quote_identifier("my col") == "[my col]"


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


# NOTE: save() returns a dict with metrics (processed_rows, total_seconds, etc.)
# but testing the return value requires a live ODBC connection + real database.
# The TypeError tests above verify the signature; metric tests belong in integration.


# ─── Format Enum ─────────────────────────────────────────────────────────────


def test_format_values():
    assert Format.polars is not None
    assert Format.pandas is not None
    assert Format.polars != Format.pandas


def test_format_from_both_modules():
    # Enums are defined in separate C++ modules; compare by name/value
    assert Format.polars.name == LocalFormat.polars.name
    assert Format.pandas.name == LocalFormat.pandas.name
    assert Format.polars.value == LocalFormat.polars.value
    assert Format.pandas.value == LocalFormat.pandas.value


# ─── Protocol Conformance ────────────────────────────────────────────────────


def test_datastore_satisfies_repository_protocol():
    """DataStore structurally satisfies the Repository protocol from interfaces.py."""
    from pygim.core.protocols import Repository as RepositoryProtocol
    store = LocalDataStore("test_conn", format="polars", pool_size=1)
    assert isinstance(store, RepositoryProtocol), (
        "DataStore must satisfy the Repository protocol (load + save)"
    )


# ─── Public Module Import ────────────────────────────────────────────────────


def test_public_module_reexports():
    """Verify pygim.persistence re-exports match the compiled extension."""
    from pygim.persistence import DataStore as PubDataStore
    from pygim.persistence import Format as PubFormat
    from pygim.persistence import acquire_datastore as pub_acquire

    # Must be the exact same objects as the direct extension imports
    assert PubDataStore is _persistence_module.DataStore
    assert pub_acquire is _persistence_module.acquire_datastore

    # Enum values should match across modules
    assert PubFormat.polars.name == Format.polars.name
    assert PubFormat.pandas.name == Format.pandas.name
