"""Example demonstrating saving (upsert) with Repository + native MSSQL strategy.

NOTE:
    * The native MSSQL strategy is a minimal skeleton. Real production usage MUST use
        proper parameter binding for DML statements (INSERT/UPDATE) to avoid SQL injection.
    * MSSQL support now auto-detects ODBC headers at build (``PYGIM_HAVE_ODBC``). Install
        unixODBC dev headers + a Microsoft ODBC driver (e.g. msodbcsql18) before installing
        this package to enable the native implementation; otherwise a stub raises at runtime.
"""
import os

from pygim import mssql_strategy, repository
from pygim.query import Query
from pygim.repo_helpers import MemoryStrategy


# For Docker/local development with the Microsoft ODBC Driver 18 the default is Encrypt=yes and
# certificate validation ON, which fails against the container's self-signed cert. You can either
# disable encryption (Encrypt=no) or keep encryption but trust the self-signed certificate.
# Keeping encryption while trusting the cert is usually preferable for local dev:
#   Encrypt=yes;TrustServerCertificate=yes
# Adjust to production-grade settings (provide a real certificate and set TrustServerCertificate=no)
# before deploying.
# Preferred: read SA password (or user-defined) from environment, fallback to a dev default.
PASSWORD = os.getenv("MSSQL_SA_PASSWORD", "NewP@ssw0rd#2025")

# Common local Docker run (published port 1433):
#   docker run -e 'ACCEPT_EULA=Y' -e 'MSSQL_SA_PASSWORD=NewP@ssw0rd#2025' -p 1433:1433 --name mssql-dev -d mcr.microsoft.com/mssql/server:2022-latest
# Connection string components:
#   Server=localhost,1433   -> explicit port (use service name if using docker compose network: Server=mssql-dev,1433)
#   Encrypt=yes;TrustServerCertificate=yes   -> allow encrypted channel without validating self-signed cert
# If you prefer to disable encryption entirely (dev only) replace the last part with: Encrypt=no;
CONNECTION = (
    "Driver={ODBC Driver 18 for SQL Server};"
    "Server=localhost,1433;Database=master;"
    "UID=sa;PWD=NewP@ssw0rd#2025;Encrypt=yes;TrustServerCertificate=yes;"
)

repo = repository.Repository(transformers=True)
repo.add_strategy(MemoryStrategy())
repo.add_strategy(mssql_strategy.MssqlStrategyNative(CONNECTION))


try:  # pragma: no cover - environment dependent
    # 1. Perform an upsert via Repository.save: key -> (table, primary_key)
    repo.save(("users", 42), {"name": "Douglas Adams", "email": "douglas@example.com"})

    # 2. Fetch the just-upserted row using fluent query builder (parameterized WHERE)
    query = (
        Query()
        .select(["id", "name", "email"])  # columns
        .from_table("users")                  # table
        .where("id=?", 42)                    # simple predicate
        .limit(1)                              # row limit
        .build()
    )
    rows = repo.fetch_raw(query)

    print("Fetched rows after save:", rows)
except Exception as e:  # pragma: no cover
    print("Example failed (missing driver, headers, or table not present):", e)
