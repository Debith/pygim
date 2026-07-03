# type: ignore
"""Opt-in constructor autowiring with ``pygim.ioc.Container``.

Manually threading dependencies through constructors gets repetitive as an
object graph grows: every composition site must know the full recipe of every
collaborator. Autowiring lets the container read a class's constructor type
hints and resolve each typed parameter from its own registrations -- the
recipe lives in one place.

pygim keeps autowiring *opt-in* (``autowire=True`` per registration) and
limited to class providers. Magic that is on by default is hard to reason
about; explicit opt-in keeps the wiring discoverable and grep-able.

This example demonstrates:
- Resolving constructor dependencies from type hints
- Autowiring across several layers of an object graph
- Falling back to default values when no provider matches a typed parameter
- The class-provider restriction and other guard rails
"""

from pygim.ioc import Container

container = Container()


# ----------------------------------------------------------------------------
# 1. A small object graph: Controller -> Service -> Repository
# ----------------------------------------------------------------------------
class Repository:
    pass


class MemoryRepository(Repository):
    def __init__(self):
        self.label = "memory"


class Service:
    # `repository` is resolved from the container by its type hint;
    # `retries` has no registered provider, so its default value is kept.
    def __init__(self, repository: Repository, retries: int = 3):
        self.repository = repository
        self.retries = retries


class Controller:
    # Autowiring composes: resolving Controller autowires Service, which in
    # turn autowires Repository. The container walks the whole graph.
    def __init__(self, service: Service):
        self.service = service


container.register(Repository, MemoryRepository, lifecycle="singleton")

#                  ┌─ a class can be its own interface (the key)...
#                  │        ┌─ ...and its own provider
#                  │        │        ┌─ autowire: resolve type-hinted
#                  │        │        │  constructor params from the container
#                  ▼        ▼        ▼
container.register(Service, Service, autowire=True)
container.register(Controller, Controller, autowire=True)

# ----------------------------------------------------------------------------
# 2. Resolve the top of the graph; the container builds everything below
# ----------------------------------------------------------------------------
controller = container.resolve(Controller)

assert isinstance(controller.service, Service)
assert isinstance(controller.service.repository, MemoryRepository)
# The singleton repository is shared with any other resolve.
assert controller.service.repository is container.resolve(Repository)
# `retries: int` had no provider; the default value 3 was preserved.
assert controller.service.retries == 3


# ----------------------------------------------------------------------------
# 3. Guard rails
# ----------------------------------------------------------------------------
# (a) Autowiring is limited to class providers -- a lambda or factory
# function has no constructor to inspect, so registration fails immediately.
class Settings:
    pass


try:
    container.register(Settings, lambda: Settings(), autowire=True)
except TypeError as error:
    assert "class provider" in str(error)
else:
    raise AssertionError("Expected autowire=True to reject non-class providers")


# (b) A parameter without a type hint *and* without a default cannot be
# autowired; the failure happens at resolve time with a clear message.
class Unannotated:
    def __init__(self, dependency):
        self.dependency = dependency


container.register(Unannotated, Unannotated, autowire=True)

try:
    container.resolve(Unannotated)
except RuntimeError as error:
    assert "type annotation" in str(error)
else:
    raise AssertionError("Expected missing annotation to fail resolution")


# (c) A *typed* dependency with no provider and no default also fails --
# autowiring never silently injects None.
class Database:
    pass


class NeedsDatabase:
    def __init__(self, database: Database):
        self.database = database


container.register(NeedsDatabase, NeedsDatabase, autowire=True)

try:
    container.resolve(NeedsDatabase)
except RuntimeError as error:
    assert "No provider" in str(error)
else:
    raise AssertionError("Expected unresolvable dependency to fail resolution")

print("IoC autowire example OK:", controller.service.repository.label)
