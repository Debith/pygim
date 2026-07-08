"""Type stubs for pygim.persistence (design doc 2.8).

The compiled extension is untyped at runtime; this stub documents the
stable public contract, including the save-metrics schema.
"""

from enum import Enum
from typing import Any, Callable, Sequence, TypedDict, overload
from typing_extensions import NotRequired

QueryParamValue = int | float | str | bool | None

class SaveMetrics(TypedDict):
    """Stable metrics contract returned by every save()."""

    total_seconds: float
    connect_seconds: float
    bind_seconds: float
    row_loop_seconds: float
    batch_flush_seconds: float
    processed_rows: int
    sent_rows: int
    record_batches: int
    affected_rows: NotRequired[int]  # keyed modes only; best-effort with triggers/NOCOUNT

class ColumnInfo(TypedDict):
    name: str
    type: str
    max_length: int  # bytes; -1 = (MAX)
    precision: int
    scale: int
    nullable: bool
    is_identity: bool
    is_computed: bool
    has_default: bool

class Format(Enum):
    polars = ...
    pandas = ...

class GimError(RuntimeError):
    sqlstate: str | None
    native_error: int

class DataStoreIntegrityError(GimError): ...
class DataStoreTransientError(GimError): ...
class DataStoreDataError(GimError): ...

class ConnectionString:
    """Immutable ODBC connection string; str()/repr()/render() mask the password."""

    @staticmethod
    def parse(source: str) -> ConnectionString: ...
    def render(self, reveal: bool = False) -> str: ...
    @property
    def driver(self) -> str | None: ...
    @property
    def server(self) -> str | None: ...
    @property
    def database(self) -> str | None: ...
    @property
    def is_named_dsn(self) -> bool: ...
    def __str__(self) -> str: ...
    def __repr__(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...

class Query:
    def __init__(self, raw_sql: str = ..., params: Sequence[QueryParamValue] = ...) -> None: ...
    def select(self, col: str) -> Query: ...
    def from_table(self, table: str) -> Query: ...
    def where(self, clause: str, params: Sequence[QueryParamValue] = ...) -> Query: ...
    def where_in(self, column: str, values: Sequence[QueryParamValue]) -> Query: ...
    def limit(self, n: int) -> Query: ...

class DataStore:
    @property
    def format(self) -> Format: ...
    def save(
        self,
        data: Any,
        table_name: str,
        bcp_workers: int = -1,
        *,
        mode: str = "append",
        keys: Sequence[str] = (),
    ) -> SaveMetrics: ...
    # Bound params apply only to raw-SQL sources; a Query already carries its
    # own params, so passing params= alongside a Query is a runtime TypeError.
    @overload
    def load(
        self,
        source: str,
        load_workers: int = 1,
        partition_column: str = "",
        params: Sequence[QueryParamValue] = (),
    ) -> Any: ...
    @overload
    def load(
        self,
        source: Query,
        load_workers: int = 1,
        partition_column: str = "",
    ) -> Any: ...
    def describe(self, table_name: str) -> list[ColumnInfo]: ...
    def truncate(self, table_name: str) -> None: ...
    def delete(
        self,
        table_name: str,
        *,
        where: str | None = None,
        params: Sequence[QueryParamValue] = (),
    ) -> int: ...
    def session(self) -> DataStoreSession: ...
    def add_pre_transform(self, fn: Callable[[], Any]) -> None: ...
    def add_post_transform(self, fn: Callable[[], Any]) -> None: ...
    def clear_transforms(self) -> None: ...

class DataStoreSession:
    @property
    def closed(self) -> bool: ...
    def save(
        self,
        data: Any,
        table_name: str,
        *,
        mode: str = "append",
        keys: Sequence[str] = (),
    ) -> SaveMetrics: ...
    def load(self, source: str, params: Sequence[QueryParamValue] = ()) -> Any: ...
    def truncate(self, table_name: str) -> None: ...
    def delete(
        self,
        table_name: str,
        *,
        where: str | None = None,
        params: Sequence[QueryParamValue] = (),
    ) -> int: ...
    def commit(self) -> None: ...
    def rollback(self) -> None: ...
    def close(self) -> None: ...
    def __enter__(self) -> DataStoreSession: ...
    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> bool: ...

def acquire_datastore(
    conn_str: str,
    format: str = "polars",
    pool_size: int = 4,
    batch_size: int = 100000,
    table_hint: str = "TABLOCK",
    bcp_workers: int = 1,
    block_size: int = 4096,
    packet_size: int = 16384,
    *,
    access_token: str | bytes | Callable[[], str | bytes] | None = None,
) -> DataStore: ...
