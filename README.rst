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

Persistence (Experimental)
--------------------------

An experimental high-performance persistence layer (DDD-style DataStore) now exists as a C++ extension:

* Strategies: pluggable objects with ``fetch(key)->data|None`` and ``save(key,value)``.
* Optional transformer pipeline (pre-save / post-load) when enabled at construction.
* Optional factory callable to turn raw data into rich entities.
* Native MSSQL strategy (ODBC) with pybind-free core/adapter architecture.
* Fluent ``Query`` for lightweight SQL assembly without manual string concatenation.
* Arrow IPC utilities for zero-copy hand-off between Polars and C++ pipelines.

Example (read):

.. code-block:: python

        from pygim import persistence

        store = persistence.acquire_datastore("Driver={ODBC Driver 18 for SQL Server};Server=localhost;...")

        df = store.load("users")
        print(df)

Write (upsert) example: ``docs/examples/persistence/mssql_write_example.py``.

Architecture Diagram:

See PlantUML: ``docs/design/persistence_class_diagram.puml`` for component relationships.

.. note:: The MSSQL native strategy uses a pybind-free core/adapter split. ODBC headers must be available at build time for native SQL Server support.

Query Security & Dialect Notes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Passing a built ``Query`` object directly to ``DataStore.load(query)``
will bind parameters using ODBC. The builder renders
queries via ``MssqlDialect``, emitting ``TOP n`` for SQL Server.

Native Arrow Persist Path
~~~~~~~~~~~~~~~~~~~~~~~~~

Bulk DataFrame persistence is handled inside native bindings via
``DataStore.save(...)``. The strategy prefers Arrow C Data
Interface (``__arrow_c_stream__``) and falls back to IPC serialization only
when needed.

.. code-block:: python

    from pygim import persistence

    conn = "Driver={ODBC Driver 18 for SQL Server};Server=localhost;..."
    store = persistence.acquire_datastore(conn)
    df = generate_polars_dataset(n=100_000)

    metrics = store.save(df, "stress_data")
    print(metrics)



Changelog
=========

See the detailed list of changes in ``CHANGELOG.rst``. For upcoming (unreleased) work, consult the top "Unreleased" section before the next version tag.
