import pytest

from pygim import repository as repository_ext
from pygim.repo_helpers import MemoryStrategy

Repository = repository_ext.Repository


def test_memory_strategy_basic_get_set():
    repo = Repository(transformers=False)
    mem = MemoryStrategy()
    repo.add_strategy(mem)
    key = ("users", 1)
    repo.save(key, {"id": 1, "name": "alice"})
    assert repo[key]["name"] == "alice"
    assert repo.contains(key)


def test_post_transform_and_factory():
    repo = Repository(transformers=True)
    repo.add_strategy(MemoryStrategy())

    def upper_name(key, value):
        if isinstance(value, dict) and "name" in value:
            value = dict(value)
            value["name"] = value["name"].upper()
        return value
    repo.add_post_transform(upper_name)

    # Factory wraps dict into simple object
    class User:
        def __init__(self, key, data):
            self.id = data["id"]
            self.name = data["name"]

    repo.set_factory(lambda key, data: User(key, data))

    k = ("users", 2)
    repo.save(k, {"id": 2, "name": "bob"})
    user = repo[k]
    assert user.name == "BOB"


def test_get_with_default_missing():
    repo = Repository(transformers=False)
    repo.add_strategy(MemoryStrategy())
    sentinel = object()
    assert repo.get(("missing", 99), sentinel) is sentinel


def test_mssql_native_placeholder():
    # Native module may or may not be compiled; import should succeed (module exists) but
    # operations should raise if not built with PYGIM_ENABLE_MSSQL.
    import importlib
    m = importlib.import_module("pygim.mssql_strategy")
    Strat = getattr(m, "MssqlStrategyNative")
    strat = Strat("Driver={ODBC Driver 17 for SQL Server};Server=.;")
    with pytest.raises(RuntimeError):
        strat.fetch(("users", 1))


def test_repository_repr():
    repo = Repository(transformers=True)
    repo.add_strategy(MemoryStrategy())
    r = repr(repo)
    assert "Repository(" in r and "strategies=1" in r
