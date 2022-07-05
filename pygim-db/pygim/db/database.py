# -*- coding: utf-8 -*-
"""
Database accessor.
"""

from dataclasses import dataclass, field

from loguru import logger


__all__ = ['Database', 'Cursor']


@dataclass(frozen=True)
class Database:
    """ This class pulls data from the database.

    ```python

    ```
    """
    _connection_string: str
    _create_engine: callable

    def __repr__(self):
        return f"<Database>"

    def __enter__(self):
        logger.info(f"Connecting: {str(self._connection_string)}")

        engine = self._create_engine(self._connection_string.resolve())
        conn = engine.connect()

        super().__setattr__("_engine", engine)
        super().__setattr__("_conn", conn)

        conn.execute("commit")

        return self

    def __exit__(self, *args):
        self._conn.close()
        self._engine.dispose()
        super().__delattr__("_engine")
        super().__delattr__("_conn")



@dataclass(frozen=True)
class Cursor:
    """ This class pulls data from the database using raw SQL queries.

    ```python

    ```
    """
    _connection_string: str
    _create_engine: callable

    def __repr__(self):
        return f"<Cursor>"

    def __enter__(self):
        logger.info(f"Connecting cursor: {str(self._connection_string)}")

        engine = self._create_engine(self._connection_string.resolve())
        conn = engine.raw_connection()
        cursor = conn.cursor()

        super().__setattr__("_engine", engine)
        super().__setattr__("_conn", conn)
        super().__setattr__("_cursor", cursor)

        cursor.execute("commit")

        return self

    def __exit__(self, *args):
        self._cursor.execute("commit")

        self._cursor.close()
        self._conn.close()
        self._engine.dispose()
        super().__delattr__("_cursor")
        super().__delattr__("_conn")
        super().__delattr__("_engine")

    def execute(self, sql):
        return self._cursor.execute(sql)
