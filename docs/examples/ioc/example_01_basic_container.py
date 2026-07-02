"""Basic usage of pygim.ioc.Container.

This example shows:
- Registering transient and singleton providers
- Named registrations
- Decorator registration form
- Provider decorators applied after construction
- Runtime interface/protocol validation on resolve
"""

from pygim.ioc import Container


container = Container(capacity=8)


class Repository:
    pass


class MemoryRepository(Repository):
    def __init__(self):
        self.kind = "memory"


class CachedRepository(Repository):
    def __init__(self):
        self.kind = "cached"


def mark_decorated(instance):
    instance.decorated = True
    return instance


container.register(Repository, MemoryRepository, decorators=[mark_decorated])
container.register(Repository, CachedRepository, name="cached", lifecycle="singleton")


@container.register(Repository, name="decorator_form")
class DecoratorFormRepository(Repository):
    def __init__(self):
        self.kind = "decorator_form"


default_repo = container.resolve(Repository)
cached_first = container.resolve((Repository, "cached"))
cached_second = container.resolve((Repository, "cached"))
decorator_form_repo = container.resolve((Repository, "decorator_form"))

assert isinstance(default_repo, Repository)
assert default_repo.kind == "memory"
assert default_repo.decorated is True

assert isinstance(cached_first, Repository)
assert cached_first.kind == "cached"
assert cached_first is cached_second

assert isinstance(decorator_form_repo, Repository)
assert decorator_form_repo.kind == "decorator_form"


class Service:
    pass


container.register(Service, lambda: object(), name="invalid")

try:
    container.resolve((Service, "invalid"))
except RuntimeError as exc:
    assert "required interface/protocol" in str(exc)
else:
    raise AssertionError("Expected invalid provider result to fail interface validation")


print("Basic IoC example OK:", container, container.registered_keys())