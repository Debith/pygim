#######################
Python Gimmicks (pygim)
#######################

| |docs| |downloads| |wheel| |pyversions|

.. |docs| image:: https://readthedocs.org/projects/pygim/badge/
    :target: https://readthedocs.org/projects/pygim
    :alt: Documentation Status

.. |downloads| image:: http://img.shields.io/pypi/dm/pygim.png
    :alt: PyPI Package monthly downloads
    :target: https://pypi.python.org/pypi/pygim

.. |wheel| image:: https://img.shields.io/pypi/format/pygim.svg
    :alt: PyPI Wheel
    :target: https://pypi.python.org/pypi/pygim

.. |pyversions| image:: https://img.shields.io/pypi/pyversions/pygim.svg


Python Gimmicks is a library that contains magical but useful tools
that can be used to improve productivity of any Python project. The
goal is to use whatever Pythonic means to provide as light-weight
and high-performance solutions as possible.

Installation
============

To install this project, simply write the following command:

.. code-block:: bash

    $ pip install pygim

Command Line Interface
======================

Installing the package also exposes a ``pygim`` command that wraps the
project's housekeeping helpers. Run ``pygim --help`` to see the available
sub-commands, including quick clean-up tools and a shortcut for running the
coverage workflow used in this repository.

.. code-block:: bash

    $ pygim clean-up --all --yes
    Starting clean up in `/your/project/path`
    Excellent! You never see them again!

You can also trigger the test coverage routine in one line:

.. code-block:: bash

    $ pygim show-test-coverage

Both commands accept the same flags described in ``pygim --help``, so you can
mix and match automation-friendly options (like ``--quiet`` or ``--yes``) to
fit your workflow.

Sub-modules
-----------

This library is divided into multiple different smaller packages.

  * pygim: This is the main project that contains the CLI and all the examples.

Repository (Experimental)
-------------------------

An experimental high-performance Repository abstraction (DDD-style) now exists as a C++ extension:

* Strategies: pluggable objects with ``fetch(key)->data|None`` and ``save(key,value)``.
* Optional transformer pipeline (pre-save / post-load) when enabled at construction.
* Optional factory callable to turn raw data into rich entities.
* Native MSSQL strategy skeleton (ODBC) guarded by ``PYGIM_ENABLE_MSSQL`` macro.
* Fluent ``Query`` for lightweight SQL assembly without manual string concatenation.
* Arrow IPC utilities for zero-copy hand-off between Polars and C++ pipelines.

Example (read):

.. code-block:: python

        from pygim import repository, mssql_strategy
        from pygim.repo_helpers import MemoryStrategy
        from pygim.query import Query

        repo = repository.Repository(transformers=False)
        repo.add_strategy(MemoryStrategy())
        repo.add_strategy(mssql_strategy.MssqlStrategyNative("Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=test;UID=sa;PWD=Passw0rd!;"))

        q = Query().select(["id","name"]).from_table("users").where("id=?", 1).build()
        row = repo.get(("users", 1))  # Strategy interprets key
        print(row)

Write (upsert) example: ``docs/examples/repository/mssql_write_example.py``.

Architecture Diagram:

See PlantUML: ``docs/design/repository_architecture.puml`` for component relationships.

.. note:: The MSSQL native logic is a skeleton and NOT production-ready (no parameterization, simplified upsert). It establishes extension points; expect API refinements.

Query Security & Dialect Notes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Passing a built ``Query`` object directly to ``MssqlStrategyNative.fetch(query)``
will bind parameters using ODBC (when compiled with ``PYGIM_ENABLE_MSSQL``). The builder emits
``LIMIT n`` which is naively rewritten to ``TOP n`` for SQL Server; more sophisticated dialect
adaptation (ORDER BY preservation, OFFSET emulation) is planned.

Arrow IPC Bridge
~~~~~~~~~~~~~~~~

The helper module :mod:`pygim.arrow_bridge` wraps the workflow recommended in the
accompanying article: export a Polars ``DataFrame`` to Apache Arrow IPC (Feather v2) and
let C++ consume the memory-mappable columnar payload.  Example:

.. code-block:: python

    import polars as pl
    from pygim.arrow_bridge import to_ipc_bytes, write_ipc_file

    df = pl.DataFrame({"id": [1, 2], "value": [3.14, 2.72]})

    # In-memory transfer (e.g. via socket/Flight)
    payload = to_ipc_bytes(df)

    # Or persist for a C++ process to memory-map and read with Arrow C++ APIs
    write_ipc_file(df, "payload.arrow")

Downstream C++ code can memory-map ``payload.arrow`` and read it using Arrow's C++
``RecordBatchFileReader`` for near zero-copy ingestion.



Changelog
=========

See the detailed list of changes in ``CHANGELOG.rst``. For upcoming (unreleased) work, consult the top "Unreleased" section before the next version tag.
