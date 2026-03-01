import pytest

from pygim import repository_v2 as repository_ext

Repository = repository_ext.Repository


def test_memory_strategy_basic_get_set():
    repo = Repository(transformers=False)
    repo.add_memory_strategy()
    key = ("users", 1)
    repo.save(key, {"id": 1, "name": "alice"})
    assert repo[key]["name"] == "alice"
    assert repo.contains(key)


def test_factory_on_key_access():
    repo = Repository(transformers=False)
    repo.add_memory_strategy()

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
    repo = Repository(transformers=False)
    repo.add_memory_strategy()
    sentinel = object()
    assert repo.get(("missing", 99), sentinel) is sentinel


def test_get_missing_raises():
    repo = Repository(transformers=False)
    repo.add_memory_strategy()
    with pytest.raises(RuntimeError, match="key not found"):
        repo[("missing", 99)]


def test_repository_repr():
    repo = Repository(transformers=True)
    repo.add_memory_strategy()
    r = repr(repo)
    assert "Repository(" in r and "strategies=1" in r


def test_multiple_keys():
    repo = Repository()
    repo.add_memory_strategy()
    repo.save(("items", 1), {"id": 1, "label": "a"})
    repo.save(("items", 2), {"id": 2, "label": "b"})
    repo.save(("items", 3), {"id": 3, "label": "c"})
    assert repo[("items", 2)]["label"] == "b"
    assert not repo.contains(("items", 99))


def test_overwrite_key():
    repo = Repository()
    repo.add_memory_strategy()
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


def test_mssql_dialect_quoting():
    d = repository_ext.MssqlDialect()
    assert d.quote_identifier("users") == "[users]"
    assert d.quote_identifier("my col") == "[my col]"


def test_feature_flags():
    """Feature flags reflect build-time ODBC/Arrow detection."""
    assert isinstance(repository_ext.HAVE_ODBC, bool)
    assert isinstance(repository_ext.HAVE_ARROW, bool)
