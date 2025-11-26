import pytest

from pygim import connection as connection_ext

ConnectionStringError = connection_ext.ConnectionStringError
ConnectionStringFactory = connection_ext.ConnectionStringFactory
MssqlConnectionString = connection_ext.MssqlConnectionString
UrlConnectionString = connection_ext.UrlConnectionString


def test_mssql_connection_string_masking():
    raw = (
        "Driver={ODBC Driver 18 for SQL Server};Server=localhost,1433;"
        "Database=master;UID=sa;PWD=SuperSecret!"
    )
    conn = ConnectionStringFactory.from_string(raw)
    assert isinstance(conn, MssqlConnectionString)
    assert conn.is_mssql()
    assert conn.server == "localhost,1433"
    masked = conn.masked()
    assert "PWD=***" in masked
    assert "UID=***" in masked


def test_url_connection_string_detection():
    raw = "mssql://user:password@localhost:1433/master"
    conn = ConnectionStringFactory.from_string(raw)
    assert isinstance(conn, UrlConnectionString)
    assert conn.is_mssql()
    assert "***" in conn.masked()


def test_invalid_connection_string():
    with pytest.raises(ConnectionStringError):
        ConnectionStringFactory.from_string("   ")