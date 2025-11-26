"""C++-backed connection string helpers exposed through the public repo API."""

from pygim import connection as _connection_ext

ConnectionString = _connection_ext.ConnectionString
ConnectionStringError = _connection_ext.ConnectionStringError
ConnectionStringFactory = _connection_ext.ConnectionStringFactory
KeyValueConnectionString = _connection_ext.KeyValueConnectionString
MssqlConnectionString = _connection_ext.MssqlConnectionString
UrlConnectionString = _connection_ext.UrlConnectionString

__all__ = [
    "ConnectionString",
    "ConnectionStringError",
    "ConnectionStringFactory",
    "KeyValueConnectionString",
    "MssqlConnectionString",
    "UrlConnectionString",
]
