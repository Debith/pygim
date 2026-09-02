# Preload Arrow's shared libraries before any test module is collected.  The
# Arrow-linked extensions (_persistence, _persistence_test, _fetch_benchmark,
# datagen) resolve libarrow via ``$ORIGIN/../pyarrow``, which only exists for a
# wheel installed into site-packages.  In an editable checkout the loader finds
# libarrow only if pyarrow has already been imported into the process, so make
# that true regardless of which test module imports an extension first.
import pyarrow
import pyarrow.parquet  # noqa: F401

collect_ignore_glob = ["**/mssql_example.py", "**/mssql_write_example.py"]
