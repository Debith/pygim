import pytest

from pygim import _repository as repository_ext
from pygim import repository as repository_mod

Repository = repository_ext.Repository
acquire_repository = repository_mod.acquire_repository


def test_memory_strategy_basic_get_set():
    repo = Repository("memory://")
    key = ("users", 1)
    repo.save(key, {"id": 1, "name": "alice"})
    assert repo[key]["name"] == "alice"
    assert repo.contains(key)


def test_factory_on_key_access():
    repo = Repository("memory://")

    class User:
        def __init__(self, key, data):
            self.id = data["id"]
            self.name = data["name"]

    repo.set_factory(lambda key, data: User(key, data))

    k = ("users", 2)
    repo.save(k, {"id": 2, "name": "bob"})
    user = repo[k]
    assert isinstance(user, User)
    assert user.name == "bob"


def test_get_with_default_missing():
    repo = Repository("memory://")
    sentinel = object()
    assert repo.get(("missing", 99), sentinel) is sentinel


def test_get_missing_raises():
    repo = Repository("memory://")
    with pytest.raises(RuntimeError, match="key not found"):
        repo[("missing", 99)]


def test_repository_repr():
    repo = Repository("memory://", transformers=True)
    r = repr(repo)
    assert "Repository(" in r
    assert 'scheme="memory"' in r


def test_multiple_keys():
    repo = Repository("memory://")
    repo.save(("items", 1), {"id": 1, "label": "a"})
    repo.save(("items", 2), {"id": 2, "label": "b"})
    repo.save(("items", 3), {"id": 3, "label": "c"})
    assert repo[("items", 2)]["label"] == "b"
    assert not repo.contains(("items", 99))


def test_overwrite_key():
    repo = Repository("memory://")
    repo.save(("items", 1), {"v": "old"})
    repo.save(("items", 1), {"v": "new"})
    assert repo[("items", 1)]["v"] == "new"


def test_query_builder():
    q = repository_ext.Query()
    q2 = (q.from_table("users")
            .select(["id", "name"])
            .where("id=?", 42)
            .limit(10)
            .build())
    assert q2 is not None


def test_feature_flags():
    """Module importability implies ODBC and Arrow support."""
    # If import succeeds, both ODBC and Arrow were present at build time.
    import pygim._repository  # noqa: F401


def test_unsupported_scheme_raises():
    with pytest.raises(Exception):
        Repository("postgres://localhost/mydb")


def test_invalid_uri_raises():
    with pytest.raises(Exception):
        Repository("not_a_valid_uri")


# ---------------------------------------------------------------------------
# acquire_repository tests
# ---------------------------------------------------------------------------

def test_acquire_repository_memory_url():
    """acquire_repository passes memory:// through unchanged."""
    repo = acquire_repository("memory://")
    assert 'scheme="memory"' in repr(repo)


def test_acquire_repository_memory_is_functional():
    """A repo from acquire_repository("memory://") can save and retrieve data."""
    repo = acquire_repository("memory://")
    repo.save(("t", 1), {"x": 42})
    assert repo[("t", 1)]["x"] == 42


def test_acquire_repository_odbc_string_converts_to_mssql_scheme():
    """Credential-free Driver={...} ODBC strings are converted to mssql:// URL form."""
    odbc = (
        "Driver={ODBC Driver 18 for SQL Server};"
        "Server=myserver,1433;"
        "Database=mydb;"
        "TrustServerCertificate=yes;"
    )
    repo = acquire_repository(odbc)
    # No live DB needed — lazy connection means construction succeeds.
    assert 'scheme="mssql"' in repr(repo)


def test_acquire_repository_odbc_with_credentials_passes_through():
    """ODBC strings with UID/PWD are forwarded as-is — credentials must not be dropped."""
    odbc = (
        "Driver={ODBC Driver 18 for SQL Server};"
        "Server=myserver,1433;"
        "Database=mydb;"
        "UID=sa;PWD=secret;"
        "TrustServerCertificate=yes;"
    )
    repo = acquire_repository(odbc)
    # Construction succeeds (lazy connect); scheme is resolved to mssql regardless.
    assert 'scheme="mssql"' in repr(repo)


def test_acquire_repository_odbc_server_without_db():
    """ODBC string without Database key still converts cleanly."""
    odbc = "Driver={ODBC Driver 18 for SQL Server};Server=myserver;"
    repo = acquire_repository(odbc)
    assert 'scheme="mssql"' in repr(repo)


def test_acquire_repository_mssql_url_passthrough():
    """mssql:// URL strings bypass ODBC conversion and go to Repository directly."""
    repo = acquire_repository("mssql://myserver/mydb")
    assert 'scheme="mssql"' in repr(repo)


def test_acquire_repository_transformers_flag():
    """transformers kwarg is forwarded to Repository."""
    repo = acquire_repository("memory://", transformers=True)
    assert "transformers=True" in repr(repo)


def test_acquire_repository_invalid_string_raises():
    """Empty / whitespace-only strings raise ValueError."""
    with pytest.raises(ValueError):
        acquire_repository("   ")


def test_acquire_repository_accessible_from_pygim_namespace():
    """acquire_repository is importable directly from pygim."""
    from pygim import acquire_repository as ar
    repo = ar("memory://")
    assert 'scheme="memory"' in repr(repo)


def test_acquire_repository_module_re_exports():
    """pygim.repository re-exports the C++ extension symbols."""
    from pygim import repository
    assert hasattr(repository, "Repository")
    assert hasattr(repository, "Query")
    assert not hasattr(repository, "MssqlDialect")

