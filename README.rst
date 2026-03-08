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

    $ pip install python-gimmicks

Pre-built wheels are available for Linux, macOS, and Windows (Python 3.8–3.13).
If no wheel is available for your platform, pip will attempt to build the package
from source, which requires a C++20-capable compiler:

* **Linux / macOS**: GCC 10+ or Clang 12+ (usually already installed)
* **Windows**: `Microsoft C++ Build Tools`_ (Visual Studio 2019 or later)

.. _Microsoft C++ Build Tools: https://visualstudio.microsoft.com/visual-cpp-build-tools/

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

Changelog
=========

See the detailed list of changes in ``CHANGELOG.rst``. For upcoming (unreleased) work, consult the top "Unreleased" section before the next version tag.
