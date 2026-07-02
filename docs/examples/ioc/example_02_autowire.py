"""Opt-in constructor autowiring for class providers.

This example shows:
- Registering class providers with `autowire=True`
- Resolving constructor dependencies by type hints
- Falling back to default parameter values when no provider exists
- Keeping autowiring explicit instead of implicit for every class provider
"""

from pygim.ioc import Container


container = Container()


class Repository:
    pass


class MemoryRepository(Repository):
    def __init__(self):
        self.label = "memory"


class Settings:
    pass


class Service:
    def __init__(self, repository: Repository, retries: int = 3):
        self.repository = repository
        self.retries = retries


container.register(Repository, MemoryRepository, lifecycle="singleton")
container.register(Service, Service, autowire=True)

service = container.resolve(Service)

assert isinstance(service, Service)
assert isinstance(service.repository, Repository)
assert service.repository is container.resolve(Repository)
assert service.retries == 3


try:
    container.register(Settings, lambda: Settings(), autowire=True)
except TypeError as exc:
    assert "class provider" in str(exc)
else:
    raise AssertionError("Expected autowire=True to reject non-class providers")


print("IoC autowire example OK:", service.repository.label, service.retries)