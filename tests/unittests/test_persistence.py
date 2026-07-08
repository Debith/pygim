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

    # Classes are the exact same objects as the direct extension imports;
    # acquire_datastore is a thin Python wrapper adding SQLAlchemy-URL
    # translation, so identity is deliberately NOT preserved for it.
    assert PubDataStore is _persistence_module.DataStore
    assert pub_acquire is not _persistence_module.acquire_datastore
    assert callable(pub_acquire)

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


def test_access_token_credential_conflict():
    """Credentials in the connection string conflict with token auth, loudly.

    The check now runs on the parsed ConnectionString, so it catches both a raw
    DSN carrying UID/PWD and a URL carrying userinfo (user:pass@) — the latter
    slipped past the old raw-substring scan, which never saw a 'uid=' keyword.
    """
    with pytest.raises(ValueError, match="conflicts with"):
        acquire_datastore("Driver=x;Server=h;UID=sa;PWD=p;", access_token="tok")
    with pytest.raises(ValueError, match="conflicts with"):
        acquire_datastore("mssql+pyodbc://sa:p@h/db?driver=x", access_token="tok")


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


# ─── P1/P2: typed errors, schema validation, bound params, maintenance ───────


def test_error_classification_mapping():
    """SQLSTATE/native-code classification drives the typed error hierarchy.

    Integrity (constraints), Transient (connection/deadlock/timeout), Data
    (conversion/truncation) — mirroring the DB-API taxonomy so callers can
    write retry/skip policies (design doc 2.5).
    """
    classify = LocalDataStore.classify_error
    assert classify("23000", 2627) == "integrity"
    assert classify("23505", 0) == "integrity"
    assert classify("", 547) == "integrity"
    assert classify("42000", 50000, "pygim ... duplicate merge-key ...") == "integrity"
    assert classify("08S01", 0) == "transient"
    assert classify("40001", 1205) == "transient"
    assert classify("HYT00", 0) == "transient"
    assert classify("22001", 0) == "data"
    assert classify("22018", 0) == "data"
    assert classify("42S02", 208) == "generic"


def test_error_hierarchy_exports():
    """The typed errors subclass GimError and are re-exported publicly."""
    from pygim.persistence import (
        DataStoreDataError,
        DataStoreIntegrityError,
        DataStoreTransientError,
        GimError,
    )

    for exc in (DataStoreIntegrityError, DataStoreTransientError, DataStoreDataError):
        assert issubclass(exc, GimError)
        assert issubclass(exc, RuntimeError)


def test_sqlalchemy_url_translation():
    """mssql+pyodbc URLs translate to ODBC DSNs; raw DSNs pass through (2.9)."""
    from pygim.persistence import _translate_conn_str as tr

    raw = "Driver={X};Server=h;Database=d;"
    assert tr(raw) is raw

    dsn = tr("mssql+pyodbc://u:p%40ss@host:1433/db"
             "?driver=ODBC+Driver+18+for+SQL+Server&Encrypt=yes")
    assert "Driver={ODBC Driver 18 for SQL Server}" in dsn
    assert "Server=host,1433" in dsn
    assert "Database=db" in dsn
    assert "UID=u" in dsn and "PWD=p@ss" in dsn
    assert "Encrypt=yes" in dsn

    # odbc_connect form wins verbatim
    from urllib.parse import quote_plus
    inner = "Driver={D};Server=s;Database=x;"
    assert tr(f"mssql+pyodbc:///?odbc_connect={quote_plus(inner)}") == inner

    # Non-mssql URLs are rejected by the C++ ConnectionString parser, which
    # surfaces as GimError (a RuntimeError subclass) rather than ValueError.
    with pytest.raises(RuntimeError, match="Unsupported URL scheme"):
        tr("postgresql://u@h/db")


def test_query_params_and_where_in(dialect):
    """Query composes AND-combined predicates with bound parameters (2.7).

    where_in renders one '?' per value; an empty IN-list must match nothing
    (1 = 0) rather than rendering invalid SQL. Column identifiers are
    validated — injection through where_in is impossible.
    """
    q = Query().from_table("t").where("status = ?", ["Approved"]).where_in("id", [1, 2])
    assert q.where_clause == "(status = ?) AND ([id] IN (?, ?))"
    assert q.param_count == 3

    empty = Query().from_table("t").where_in("id", [])
    assert empty.where_clause == "1 = 0"
    assert empty.param_count == 0

    with pytest.raises(RuntimeError, match="invalid column"):
        Query().where_in("id]; DROP TABLE t--", [1])


def test_dialect_three_part_names(dialect):
    """Table names quote part-by-part up to database.schema.table (2.10)."""
    assert dialect.quote_identifier("t") == "[t]"
    assert MssqlDialect().render(Query().from_table("db.dbo.t")) == "SELECT * FROM [db].[dbo].[t]"


def test_live_schema_validation_diagnostics():
    """Pre-save validation names every offending column before any write (2.4).

    Unknown columns, NULLs headed into NOT NULL columns, missing required
    columns, and type-family mismatches must each be identified in one
    structured error — and the table must remain untouched.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_schema_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} "
                "(id INT NOT NULL, val NVARCHAR(20) NOT NULL, n INT NULL)")

    try:
        # unknown column + missing required column
        with pytest.raises(RuntimeError) as excinfo:
            store.save(pl.DataFrame({"id": [1], "bogus": ["x"]}), table)
        msg = str(excinfo.value)
        assert "bogus" in msg and "val" in msg and table in msg

        # NULLs into NOT NULL
        with pytest.raises(RuntimeError, match="NULLs headed into NOT NULL.*val"):
            store.save(pl.DataFrame({"id": [1], "val": [None]}), table)

        # type-family mismatch (string frame column vs INT table column)
        with pytest.raises(RuntimeError, match="type mismatches.*id"):
            store.save(pl.DataFrame({"id": ["one"], "val": ["x"]}), table)

        # nonexistent table
        with pytest.raises(RuntimeError, match="does not exist"):
            store.save(pl.DataFrame({"id": [1]}), "no_such_table_xyz")

        assert cur.execute(f"SELECT COUNT(*) FROM dbo.{table}").fetchone()[0] == 0
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_describe():
    """describe(table) returns the per-column catalog for pre-flight checks (2.4)."""
    pyodbc = pytest.importorskip("pyodbc")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_desc_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} "
                "(id INT IDENTITY(1,1) PRIMARY KEY, val NVARCHAR(50) NULL, "
                "n INT NOT NULL DEFAULT 7)")

    try:
        cols = {c["name"]: c for c in store.describe(table)}
        assert cols["id"]["is_identity"] and not cols["id"]["nullable"]
        assert cols["val"]["type"] == "nvarchar" and cols["val"]["nullable"]
        assert cols["n"]["has_default"] and not cols["n"]["nullable"]

        with pytest.raises(RuntimeError, match="does not exist"):
            store.describe("no_such_table_xyz")
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_typed_errors():
    """Live failures raise the matching typed exception with SQLSTATE attached (2.5)."""
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    from pygim.persistence import DataStoreDataError, DataStoreIntegrityError

    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_typed_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT NOT NULL PRIMARY KEY)")

    try:
        store.save(pl.DataFrame({"id": [1]}), table)
        # Duplicate PK via plain append: the driver reports BCP row rejection
        # only through the committed-row count (no SQLSTATE), so the commit
        # shortfall raises IntegrityError by construction.
        with pytest.raises(DataStoreIntegrityError, match="committed 0 of 1"):
            store.save(pl.DataFrame({"id": [1]}), table)

        # duplicate merge keys in frame → guard THROW → IntegrityError
        with pytest.raises(DataStoreIntegrityError):
            store.save(pl.DataFrame({"id": [2, 2]}), table, mode="upsert", keys=["id"])

        # conversion failure in a load → 22018 → DataError
        with pytest.raises(DataStoreDataError):
            store.load("SELECT CONVERT(INT, 'not a number') AS x")
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_bound_parameter_loads():
    """Predicate-safe loads: '?' markers bind values, no string concatenation (2.7).

    Covers Query.where(clause, params) + where_in, raw SQL with params=,
    session loads with params, hostile string values (quote injection is
    inert as a bound value), and NULL/unicode parameters.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    from pygim.persistence import Query as PubQuery

    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_params_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT, val NVARCHAR(50) NULL)")

    try:
        store.save(pl.DataFrame(
            {"id": [1, 2, 3, 4], "val": ["a'--", "béta", "c", None]}), table)

        q = PubQuery().from_table(table).where("val = ?", ["a'--"])
        assert len(store.load(q)) == 1  # quote in value is inert

        q = PubQuery().from_table(table).where_in("id", [2, 3])
        assert len(store.load(q)) == 2

        df = store.load(f"SELECT * FROM dbo.{table} WHERE val = ?", params=["béta"])
        assert len(df) == 1  # unicode round-trips via UTF-16 binding

        with store.session() as session:
            df = session.load(f"SELECT * FROM dbo.{table} WHERE id > ?", [2])
            assert len(df) == 2

        with pytest.raises(ValueError, match="raw SQL"):
            store.load(table, params=[1])  # bare table name takes no params
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_truncate_delete_and_atomic_replace():
    """truncate/delete work standalone and compose into an atomic replace (2.11)."""
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_maint_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT, val NVARCHAR(20))")
    count = lambda: cur.execute(f"SELECT COUNT(*) FROM dbo.{table}").fetchone()[0]

    try:
        store.save(pl.DataFrame({"id": [1, 2, 3], "val": ["a", "b", "c"]}), table)

        deleted = store.delete(table, where="id > ?", params=[1])
        assert deleted == 2 and count() == 1

        store.truncate(table)
        assert count() == 0

        # atomic replace: truncate + save in one session transaction;
        # a rollback restores the pre-replace contents.
        store.save(pl.DataFrame({"id": [9], "val": ["old"]}), table)
        session = store.session()
        session.truncate(table)
        session.save(pl.DataFrame({"id": [10, 11], "val": ["new", "new"]}), table)
        session.rollback()
        session.close()
        rows = cur.execute(f"SELECT id FROM dbo.{table}").fetchall()
        assert [r[0] for r in rows] == [9]  # replace rolled back atomically

        with store.session() as session:
            session.truncate(table)
            session.save(pl.DataFrame({"id": [10, 11], "val": ["new", "new"]}), table)
        assert count() == 2

        # delete-all (where=None) and params-without-where validation
        assert store.delete(table) == 2
        with pytest.raises(ValueError, match="without a where"):
            store.delete(table, params=[1])
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_three_part_table_names():
    """Save/load resolve database.schema.table targets (2.10)."""
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_3part_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE tempdb.dbo.{table} (id INT)")

    try:
        store.save(pl.DataFrame({"id": [1, 2]}), f"tempdb.dbo.{table}")
        assert len(store.load(f"tempdb.dbo.{table}")) == 2
    finally:
        cur.execute(f"DROP TABLE tempdb.dbo.{table}")


def test_live_parallel_load_with_filter_falls_back_correctly():
    """Filtered queries never take the parallel partition path (bug fix).

    The range-partitioned parallel loader builds its own SQL; before this
    branch it silently dropped WHERE clauses when load_workers > 1. Filtered
    queries must now fall back to single-worker and honor the predicate.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    from pygim.persistence import Query as PubQuery

    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_parfilter_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT NOT NULL PRIMARY KEY, val NVARCHAR(10))")

    try:
        store.save(pl.DataFrame({"id": list(range(100)), "val": ["x"] * 100}), table)
        q = PubQuery().from_table(table).where("id < ?", [10])
        df = store.load(q, load_workers=4)
        assert len(df) == 10  # predicate honored, not dropped by partitioning
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


# ─── Regression: adversarial-review findings ─────────────────────────────────


def test_sqlalchemy_url_edge_cases():
    """URL translation brace-quotes hostile values and supports the named-DSN form.

    A password containing ';' or '}' must be brace-quoted (with '}' doubled) so
    it cannot inject extra ODBC attributes; a percent-escaped instance name
    (%5C) must decode to a backslash; and a bare host with no port, database, or
    driver is treated as a named ODBC ``DSN=`` reference.
    """
    from pygim.persistence import _translate_conn_str as tr

    pwd = tr("mssql+pyodbc://sa:p%3Bw%7Dd@host/db?driver=ODBC+Driver+18+for+SQL+Server")
    assert "PWD={p;w}}d};" in pwd  # ';' triggers braces, '}' doubled inside

    inst = tr("mssql+pyodbc://host%5CSQLEXPRESS/db?driver=D")
    assert "Server=host\\SQLEXPRESS" in inst  # %5C decoded to a backslash

    named = tr("mssql+pyodbc://sa:secret@MyDsn")
    assert named.startswith("DSN=") and "UID=sa" in named and "PWD=secret" in named


def test_connection_string_value_object():
    """ConnectionString parses DSNs/URLs and masks secrets by default (design).

    str()/repr()/render() must hide the password so it never leaks into a log
    line or traceback; render(reveal=True) yields the connect string. Value
    equality lets the parallel-load cache key on the connection regardless of
    whether it was given as a raw DSN or a URL.
    """
    from pygim.persistence import ConnectionString as CS

    cs = CS.parse("Driver={ODBC Driver 18 for SQL Server};Server=h,1433;"
                  "Database=d;UID=sa;PWD=s3cr3t;")
    masked = cs.render()
    assert "PWD=***" in masked and "s3cr3t" not in masked
    assert str(cs) == masked and repr(cs) == masked      # both default to masked
    assert "PWD=s3cr3t" in cs.render(reveal=True)         # secrets only on demand
    assert cs.server == "h,1433" and cs.database == "d"

    # The equivalent URL parses to the same value (order-independent equality).
    url = CS.parse("mssql+pyodbc://sa:s3cr3t@h:1433/d"
                   "?driver=ODBC+Driver+18+for+SQL+Server")
    assert url == cs
    # Raw DSNs round-trip verbatim so reconnection is byte-for-byte identical.
    raw = "Driver={X};Server=h;Database=d;"
    assert CS.parse(raw).render(reveal=True) == raw


def test_top_level_acquire_datastore_is_translating_wrapper():
    """pygim.acquire_datastore is the URL-translating wrapper, not the raw ext.

    The top-level entry point must share pygim.persistence's SQLAlchemy-URL
    translation; importing it straight from the compiled extension would bypass
    that, so the two names must be the same object.
    """
    import pygim
    import pygim.persistence as pers

    assert pygim.acquire_datastore is pers.acquire_datastore


def test_where_in_rejects_oversized_lists():
    """where_in guards SQL Server's ~2100-parameter statement cap.

    Each IN value binds one parameter; a list beyond the cap must raise a clear
    error at build time rather than letting the driver fail cryptically at
    execute, when the caller can no longer tell which query overflowed.
    """
    from pygim.persistence import Query as PubQuery

    q = PubQuery().from_table("t")
    with pytest.raises(RuntimeError, match="parameter limit"):
        q.where_in("id", list(range(2001)))


def test_gim_error_has_default_attributes():
    """Every GimError exposes sqlstate/native_error, not just ODBC-translated ones.

    The type stub promises these attributes on the base class; class-level
    defaults must make that true for errors that never passed through ODBC, so
    consumers can read err.sqlstate unconditionally.
    """
    from pygim.persistence import GimError

    assert GimError.sqlstate is None
    assert GimError.native_error == 0


def test_live_append_rejects_column_order_mismatch():
    """Positional append refuses a frame whose columns don't line up with the table.

    Plain append binds frame column i to table ordinal i+1, so a frame with the
    right names in the wrong order — or one that omits a column — would write
    values into the wrong columns. Validation must reject both and leave the
    table empty; the keyed path (MERGE by name) remains available for those.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_ord_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT NOT NULL, val NVARCHAR(20) NULL)")

    try:
        # Correct names, reversed order → positional mismatch.
        with pytest.raises(RuntimeError, match="append is positional"):
            store.save(pl.DataFrame({"val": ["a"], "id": [1]}), table)
        # Omits a column → column-count mismatch.
        with pytest.raises(RuntimeError, match="append is positional"):
            store.save(pl.DataFrame({"id": [1]}), table)
        assert cur.execute(f"SELECT COUNT(*) FROM dbo.{table}").fetchone()[0] == 0
        # In-order full frame still works.
        store.save(pl.DataFrame({"id": [1], "val": ["a"]}), table)
        assert cur.execute(f"SELECT COUNT(*) FROM dbo.{table}").fetchone()[0] == 1
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_duplicate_frame_columns_rejected():
    """A frame with duplicate column names is rejected before any write.

    Arrow permits duplicate field names but they can never map onto a SQL
    table, and by-name catalog lookups would be ambiguous, so validation must
    fail fast with a clear message rather than crash or silently mis-bind.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pa = pytest.importorskip("pyarrow")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_dup_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT, val NVARCHAR(20))")

    try:
        dup = pa.table([pa.array([1]), pa.array([2])], names=["id", "id"])
        with pytest.raises(RuntimeError, match="duplicate frame column names"):
            store.save(dup, table)
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_cross_database_three_part_names():
    """3-part saves validate against the TARGET database's catalog (2.10 fix).

    sys.columns is per-database, so validating a database.schema.table target
    must query [database].sys.columns — not the connection's current database.
    Connecting to master while targeting tempdb proves the catalog lookup
    follows the 3-part name: a real save succeeds and an unknown column fails.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    base = _integration_conn_str(pyodbc)
    # Point the store's connection at a DIFFERENT database than the target.
    master_conn = re.sub(r"Database=[^;]+", "Database=master", base, count=1, flags=re.I)
    store = acquire_datastore(master_conn, format="polars", pool_size=2)
    ctl = pyodbc.connect(base, timeout=30)
    ctl.autocommit = True
    table = f"pygim_xdb_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE tempdb.dbo.{table} (id INT NOT NULL, val NVARCHAR(20) NULL)")

    try:
        target = f"tempdb.dbo.{table}"
        # describe reaches the tempdb catalog from a master connection.
        assert {c["name"] for c in store.describe(target)} == {"id", "val"}
        store.save(pl.DataFrame({"id": [1, 2], "val": ["a", "b"]}), target)
        assert len(store.load(target)) == 2
        # unknown column still caught via the tempdb catalog, not master's.
        with pytest.raises(RuntimeError, match="columns not in table"):
            store.save(pl.DataFrame({"id": [3], "val": ["c"], "nope": [1]}), target)
    finally:
        cur.execute(f"DROP TABLE tempdb.dbo.{table}")


def test_live_describe_numeric_metadata():
    """describe() reports max_length / precision / scale for sizing decisions.

    Beyond names and nullability, callers need the numeric catalog fields to
    validate widths: nvarchar(50) reports 100 bytes (2 per UTF-16 unit) and a
    decimal(10,2) reports precision 10 / scale 2.
    """
    pyodbc = pytest.importorskip("pyodbc")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_descnum_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (name NVARCHAR(50) NULL, amount DECIMAL(10,2) NULL)")

    try:
        cols = {c["name"]: c for c in store.describe(table)}
        assert cols["name"]["max_length"] == 100  # 50 UTF-16 units × 2 bytes
        assert cols["amount"]["precision"] == 10 and cols["amount"]["scale"] == 2
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")


def test_live_typed_null_and_scalar_params():
    """Bound parameters cover NULL, bool, float, and int without concatenation.

    The predicate ``? IS NULL`` proves a bound NULL reaches the server as a real
    NULL, and float/int/bool bind through their own ODBC C types — each value is
    parameterized, never string-formatted into the SQL text.
    """
    pyodbc = pytest.importorskip("pyodbc")
    store, ctl = _live_store_and_cursor(pyodbc)
    _ = ctl  # server reachability only

    # A NULL parameter makes "? IS NULL" true (1 row), a non-NULL makes it false.
    assert len(store.load("SELECT 1 AS x WHERE ? IS NULL", params=[None])) == 1
    assert len(store.load("SELECT 1 AS x WHERE ? IS NULL", params=[7])) == 0
    # float, int, and bool round-trip through their bound C types.
    assert len(store.load("SELECT 1 AS x WHERE ? > 1.5", params=[2.5])) == 1
    assert len(store.load("SELECT 1 AS x WHERE ? = 1", params=[True])) == 1


def test_live_session_delete_with_rollback():
    """session.delete participates in the session transaction and rolls back.

    A delete issued inside a session must be undone by rollback() together with
    the rest of the transaction, so the rows reappear — proving delete is not
    silently autocommitted on the session connection.
    """
    pyodbc = pytest.importorskip("pyodbc")
    pl = pytest.importorskip("polars")
    store, ctl = _live_store_and_cursor(pyodbc)
    table = f"pygim_sessdel_{uuid.uuid4().hex[:8]}"
    cur = ctl.cursor()
    cur.execute(f"CREATE TABLE dbo.{table} (id INT, val NVARCHAR(10))")
    count = lambda: cur.execute(f"SELECT COUNT(*) FROM dbo.{table}").fetchone()[0]

    try:
        store.save(pl.DataFrame({"id": [1, 2, 3], "val": ["a", "b", "c"]}), table)
        session = store.session()
        assert session.delete(table, where="id <= ?", params=[2]) == 2
        session.rollback()
        session.close()
        assert count() == 3  # delete rolled back with the transaction
    finally:
        cur.execute(f"DROP TABLE dbo.{table}")
