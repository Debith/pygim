import os
import re
import uuid

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


def test_acquire_datastore_live_round_trip():
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")

    conn_str = _integration_conn_str(pyodbc)
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


# ─── Save modes: validation (no database needed) ─────────────────────────────
# Mode/keys validation and the empty-frame contract run before any connection
# checkout, so they are testable without a live server.


def test_save_mode_validation_errors():
    """Invalid mode/keys combinations fail fast, before any connection.

    parse_save_mode runs first in save(): unknown modes, keys with append,
    and keyed modes without keys must all raise ValueError without touching
    the (unreachable) database.
    """
    pl = pytest.importorskip("polars")
    store = acquire_datastore("Driver=unreachable;", pool_size=1)
    df = pl.DataFrame({"id": [1]})

    with pytest.raises(ValueError, match="Unknown save mode"):
        store.save(df, "t", mode="merge")
    with pytest.raises(ValueError, match="keys are only valid"):
        store.save(df, "t", keys=["id"])
    with pytest.raises(ValueError, match="requires key columns"):
        store.save(df, "t", mode="upsert")
    with pytest.raises(ValueError, match="requires key columns"):
        store.save(df, "t", mode="insert_missing")


def test_empty_frame_save_is_noop_without_connection():
    """save() on an empty frame is a no-op that never opens a connection.

    The connection string is deliberately unreachable: if the empty-frame
    path checked out a connection, this test would error instead of
    returning zero-row metrics. Keyed modes additionally report zero
    affected rows.
    """
    pl = pytest.importorskip("polars")
    store = acquire_datastore("Driver=unreachable;", pool_size=1)
    empty = pl.DataFrame({"id": pl.Series([], dtype=pl.Int32)})

    metrics = store.save(empty, "t")
    assert metrics["processed_rows"] == 0
    assert metrics["sent_rows"] == 0
    assert metrics["total_seconds"] == 0.0

    keyed = store.save(empty, "t", mode="upsert", keys=["id"])
    assert keyed["processed_rows"] == 0
    assert keyed["affected_rows"] == 0


def test_access_token_worker_guards():
    """access_token is incompatible with parallel workers, loudly.

    Parallel BCP/load workers open extra connections straight from the
    connection string, bypassing token auth — so combining them must fail
    at creation (bcp_workers) or call time (load_workers), not half-work.
    """
    with pytest.raises(ValueError, match="bcp_workers=1"):
        acquire_datastore("Driver=unreachable;", bcp_workers=2, access_token="tok")

    store = acquire_datastore("Driver=unreachable;", access_token="tok")
    with pytest.raises(ValueError, match="load_workers=1"):
        store.load("some_table", load_workers=2)


def test_session_reexported():
    """pygim.persistence re-exports DataStoreSession alongside DataStore."""
    from pygim.persistence import DataStoreSession

    assert DataStoreSession is _persistence_module.DataStoreSession


# ─── Live integration: keyed writes and sessions ─────────────────────────────
# Auto-skip unless a SQL Server is reachable (STRESS_CONN or the local Docker
# instance on localhost:1433). Verified behaviors were established against
# SQL Server 2022: BCP rows participate in the session's manual-commit
# transaction, so rollback undoes bulk saves.


def _live_store_and_cursor(pyodbc):
    conn_str = _integration_conn_str(pyodbc)
    store = acquire_datastore(conn_str, format="polars", pool_size=2)
    ctl = pyodbc.connect(conn_str, timeout=30)
    ctl.autocommit = True
    return store, ctl


def test_live_upsert_and_insert_missing():
    """Upsert updates matched keys and inserts new ones; insert_missing skips.

    End-to-end through the BCP staging + MERGE path: an overlapping frame
    must update rows 2-3, insert row 4, and report affected_rows; the
    anti-join mode must leave existing keys untouched.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_upsert_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT NOT NULL PRIMARY KEY, val NVARCHAR(50), n INT)")

    try:
        store.save(pl.DataFrame({"id": [1, 2, 3], "val": ["one", "two", "three"], "n": [10, 20, 30]}),
                   table)
        metrics = store.save(
            pl.DataFrame({"id": [2, 3, 4], "val": ["TWO", "THREE", "four"], "n": [22, 33, 44]}),
            table, mode="upsert", keys=["id"])
        assert metrics["affected_rows"] == 3

        rows = cur.execute(f"SELECT id, val, n FROM dbo.{table} ORDER BY id").fetchall()
        assert [(r[0], r[1], r[2]) for r in rows] == [
            (1, "one", 10), (2, "TWO", 22), (3, "THREE", 33), (4, "four", 44)]

        metrics = store.save(
            pl.DataFrame({"id": [4, 5], "val": ["KEEP-OUT", "five"], "n": [0, 55]}),
            table, mode="insert_missing", keys=["id"])
        assert metrics["affected_rows"] == 1

        rows = cur.execute(f"SELECT val FROM dbo.{table} WHERE id IN (4, 5) ORDER BY id").fetchall()
        assert [r[0] for r in rows] == ["four", "five"]  # id=4 untouched
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_upsert_rejects_missing_key_column():
    """A key absent from the frame fails with a message naming the column."""
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_badkey_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT NOT NULL PRIMARY KEY, val NVARCHAR(50))")
    try:
        # Validation is pre-checkout: precise ValueError, not a late GimError.
        with pytest.raises(ValueError, match="not_a_column"):
            store.save(pl.DataFrame({"id": [1], "val": ["x"]}), table,
                       mode="upsert", keys=["not_a_column"])
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_session_commit_and_rollback():
    """One session transaction spans multiple tables, atomically.

    The load-bearing contract from the design doc (2.2): rollback undoes
    BCP saves to BOTH tables; commit lands both; the session sees its own
    uncommitted writes; __exit__ commits on success and rolls back on
    exception.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    ta = f"pygim_sess_a_{uuid.uuid4().hex[:8]}"
    tb = f"pygim_sess_b_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{ta} (id INT, val NVARCHAR(50))")
    cur.execute(f"CREATE TABLE dbo.{tb} (id INT, val NVARCHAR(50))")
    count = lambda t: cur.execute(f"SELECT COUNT(*) FROM dbo.{t}").fetchone()[0]

    try:
        # rollback undoes bulk saves across tables
        session = store.session()
        session.save(pl.DataFrame({"id": [1], "val": ["a"]}), ta)
        session.save(pl.DataFrame({"id": [2], "val": ["b"]}), tb)
        session.rollback()
        session.close()
        assert count(ta) == 0 and count(tb) == 0

        # context manager commits on clean exit; session reads its own writes
        with store.session() as session:
            session.save(pl.DataFrame({"id": [1], "val": ["a"]}), ta)
            session.save(pl.DataFrame({"id": [2], "val": ["b"]}), tb)
            assert len(session.load(ta)) == 1  # uncommitted but visible here
        assert count(ta) == 1 and count(tb) == 1

        # exception inside the context rolls back
        with pytest.raises(RuntimeError, match="boom"):
            with store.session() as session:
                session.save(pl.DataFrame({"id": [9], "val": ["z"]}), ta)
                raise RuntimeError("boom")
        assert count(ta) == 1

        # a closed session refuses further work
        assert session.closed
        with pytest.raises(RuntimeError, match="closed"):
            session.save(pl.DataFrame({"id": [1], "val": ["x"]}), ta)
    finally:
        cur.execute(f"DROP TABLE dbo.{ta}")
        cur.execute(f"DROP TABLE dbo.{tb}")


def test_live_session_keyed_save():
    """Keyed saves compose with sessions: staging + MERGE inside the caller txn."""
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_sess_up_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT NOT NULL PRIMARY KEY, val NVARCHAR(50))")

    try:
        store.save(pl.DataFrame({"id": [1], "val": ["one"]}), table)
        with store.session() as session:
            session.save(pl.DataFrame({"id": [1, 2], "val": ["ONE", "two"]}), table,
                         mode="upsert", keys=["id"])
        rows = cur.execute(f"SELECT id, val FROM dbo.{table} ORDER BY id").fetchall()
        assert [(r[0], r[1]) for r in rows] == [(1, "ONE"), (2, "two")]
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_session_rollback_is_atomic_across_bcp_batches():
    """Rollback undoes a session save even when BCP flushed multiple batches.

    batch_size=3 against a 10-row frame forces several mid-save bcp_batch
    calls; all of them must remain inside the session's manual-commit
    transaction (verified against SQL Server 2022 + ODBC Driver 18). If this
    ever fails, session atomicity silently breaks for multi-batch saves.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    conn_str = _integration_conn_str(pyodbc)
    store = acquire_datastore(conn_str, format="polars", pool_size=2, batch_size=3)
    ctl = pyodbc.connect(conn_str, timeout=30)
    ctl.autocommit = True
    table = f"pygim_batchtxn_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT, val NVARCHAR(20))")

    try:
        df = pl.DataFrame({"id": list(range(10)), "val": [f"r{i}" for i in range(10)]})
        session = store.session()
        session.save(df, table)
        session.rollback()
        session.close()
        assert cur.execute(f"SELECT COUNT(*) FROM dbo.{table}").fetchone()[0] == 0
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def _strip_credentials(conn_str):
    """Remove UID/PWD/Trusted_Connection segments (token auth forbids them)."""
    kept = [seg for seg in conn_str.split(";")
            if seg and not re.match(r"\s*(uid|pwd|trusted_connection)\s*=", seg, re.I)]
    return ";".join(kept) + ";"


def test_live_access_token_callable_invoked_per_connect_and_pool_recovers():
    """The token callable runs once per physical connect; failures free the slot.

    The local server does not accept Entra tokens, so each connect must fail
    with a driver error — but only AFTER the callable produced a token,
    which proves the per-connect token path (including its GIL acquisition
    inside a released-GIL checkout) executes without deadlock. The SECOND
    failed save must invoke the callable again: a throwing connect has to
    return its pool slot (with pool_size=1, a leaked slot would turn every
    later save into a 5s timeout instead of a fresh connect attempt).
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    conn_str = _strip_credentials(_integration_conn_str(pyodbc))

    calls = []

    def token_source():
        calls.append(1)
        return "dummy-token-value"

    store = acquire_datastore(conn_str, access_token=token_source, pool_size=1)
    for _ in range(2):
        with pytest.raises(RuntimeError):
            store.save(pl.DataFrame({"id": [1]}), "any_table")
    assert len(calls) == 2  # slot recovered → second connect attempted


def test_access_token_packing_and_validation():
    """SQL_COPT_SS_ACCESS_TOKEN packing is byte-exact and rejects bad input.

    The wire format is a 4-byte little-endian byte length followed by the
    token expanded so each byte is followed by a zero byte (no BOM, no
    terminator) — a wrong prefix or expansion would break auth for every
    Azure user while the suite stayed green if untested. Pre-packed pyodbc
    tokens (embedded NULs), empty and non-ASCII tokens, and non-token types
    must be rejected with precise errors.
    """
    pack = LocalDataStore.pack_access_token

    assert pack("AB") == b"\x04\x00\x00\x00A\x00B\x00"
    assert pack(b"AB") == b"\x04\x00\x00\x00A\x00B\x00"
    assert pack(lambda: "AB") == b"\x04\x00\x00\x00A\x00B\x00"
    assert pack(lambda: b"X") == b"\x02\x00\x00\x00X\x00"

    with pytest.raises(ValueError, match="empty"):
        pack("")
    with pytest.raises(ValueError, match="ASCII"):
        pack("tökén")
    with pytest.raises(ValueError, match="pre-packed"):
        pack(b"\x04\x00\x00\x00A\x00B\x00")
    with pytest.raises(TypeError):
        pack(123)


def test_access_token_conflicts_and_type_checked_eagerly():
    """Token misconfiguration fails at acquire time, not first checkout.

    A wrong token type or credential keywords alongside token auth would
    otherwise only surface on the first save, deep in a connect path.
    """
    with pytest.raises(TypeError, match="access_token must be"):
        acquire_datastore("Driver=x;", access_token=123)
    for kw in ("UID=sa;", "PWD=secret;", "Trusted_Connection=yes;", "Authentication=SqlPassword;"):
        with pytest.raises(ValueError, match="conflicts"):
            acquire_datastore(f"Driver=x;{kw}", access_token="tok")


def test_keyed_save_null_and_missing_keys_fail_before_connecting():
    """Key validation runs on the Arrow frame before any connection exists.

    NULL key values would silently duplicate rows on every re-run (NULL
    never equality-matches in MERGE ON), and a typo'd key must fail on
    empty frames too — both against an unreachable server to prove the
    checks are pre-checkout.
    """
    pl = pytest.importorskip("polars")
    store = acquire_datastore("Driver=unreachable;", pool_size=1)

    with_nulls = pl.DataFrame({"id": [1, None], "val": ["a", "b"]})
    with pytest.raises(ValueError, match="contain NULLs.*id"):
        store.save(with_nulls, "t", mode="upsert", keys=["id"])

    empty = pl.DataFrame({"id": pl.Series([], dtype=pl.Int32)})
    with pytest.raises(ValueError, match="not present in the frame.*nope"):
        store.save(empty, "t", mode="upsert", keys=["nope"])


def test_live_upsert_identity_target():
    """Keyed writes work when the merge key is the target's IDENTITY column.

    The most common upsert shape. Requires IDENTITY_INSERT handling: BCP can
    stage explicit values (ISNULL strips identity there), but the final
    MERGE INSERT would fail with error 544 without SET IDENTITY_INSERT ON,
    and updating the identity column in WHEN MATCHED is illegal.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_ident_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT IDENTITY(1,1) PRIMARY KEY, val NVARCHAR(50))")
    cur.execute(f"INSERT INTO dbo.{table} (val) VALUES ('one'), ('two')")  # ids 1, 2

    try:
        metrics = store.save(
            pl.DataFrame({"id": [2, 3], "val": ["TWO", "three"]}),
            table, mode="upsert", keys=["id"])
        rows = cur.execute(f"SELECT id, val FROM dbo.{table} ORDER BY id").fetchall()
        assert [(r[0], r[1]) for r in rows] == [(1, "one"), (2, "TWO"), (3, "three")]
        assert "affected_rows" in metrics
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_duplicate_merge_keys_rejected():
    """Duplicate merge-key values in the frame fail fast, for BOTH keyed modes.

    Matched duplicates would error late (8672) and new-key duplicates would
    insert silently — the guard turns every duplicate into one clear error
    before the target is touched.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_dup_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT NOT NULL, val NVARCHAR(50))")

    try:
        dup = pl.DataFrame({"id": [7, 7], "val": ["a", "b"]})  # new keys, duplicated
        for mode in ("upsert", "insert_missing"):
            with pytest.raises(RuntimeError, match="duplicate merge-key"):
                store.save(dup, table, mode=mode, keys=["id"])
        assert cur.execute(f"SELECT COUNT(*) FROM dbo.{table}").fetchone()[0] == 0
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_session_raw_sql_load_and_closed_semantics():
    """Session loads accept raw SQL; closed sessions refuse every operation.

    Covers the space-heuristic branch of the session load path and pins
    close() idempotency plus commit/rollback-after-close errors.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_rawsql_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT, val NVARCHAR(50))")

    try:
        session = store.session()
        session.save(pl.DataFrame({"id": [1, 2], "val": ["a", "b"]}), table)
        df = session.load(f"SELECT id FROM dbo.{table} WHERE id = 2")
        assert len(df) == 1
        session.commit()
        session.close()
        session.close()  # idempotent
        for op in (session.commit, session.rollback):
            with pytest.raises(RuntimeError, match="closed"):
                op()
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")
