"""High-level repository construction helpers.

Placed under `pygim.repo` to avoid clashing with the compiled extension module
`pygim.repository` which exposes the C++ class `Repository`.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Sequence

from pygim import repository as _ext  # compiled extension
from pygim.query import QueryBuilder
from pygim.repo_helpers import MemoryStrategy

try:  # optional native MSSQL strategy
    from pygim import mssql_strategy as _mssql_ext  # type: ignore
except Exception:  # pragma: no cover
    _mssql_ext = None  # type: ignore

Repository = _ext.Repository  # re-export for convenience
__all__ = ["create_repository", "RepositoryBuilder", "Repository"]


def _is_mssql_connection(conn_str: str) -> bool:
    s = conn_str.lower()
    return "driver=" in s and "server=" in s and "database=" in s


class _FluentQuery:
    def __init__(self, repo):
        self._repo = repo
        self._builder = QueryBuilder()

    def select(self, cols: Sequence[str]):
        self._builder.select(cols)
        return self
    def from_table(self, table: str):
        self._builder.from_table(table)
        return self
    def where(self, clause: str, param):
        self._builder.where(clause, param)
        return self
    def limit(self, n: int):
        self._builder.limit(n)
        return self
    def build(self):
        return self._builder.build()
    def fetch(self):
        return self._repo.fetch_raw(self._builder.build())


@dataclass
class RepositoryBuilder:
    enable_transformers: bool = False
    include_memory: bool = True
    connection_string: Optional[str] = None

    def build(self):
        repo = Repository(self.enable_transformers)
        if self.include_memory:
            repo.add_strategy(MemoryStrategy())
        if self.connection_string and _is_mssql_connection(self.connection_string) and _mssql_ext:
            repo.add_strategy(_mssql_ext.MssqlStrategyNative(self.connection_string))
        repo.query = _FluentQuery(repo)  # type: ignore[attr-defined]
        return repo


def create_repository(connection_string: str | None = None, *, enable_transformers: bool = False,
                      include_memory: bool = True):
    return RepositoryBuilder(enable_transformers=enable_transformers,
                             include_memory=include_memory,
                             connection_string=connection_string).build()
