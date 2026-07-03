# type: ignore
"""Basic usage of ``pygim.ioc.Container``.

Inversion of Control (IoC) separates *what* your code needs (an interface)
from *how* it is built (a provider). Instead of constructing collaborators
inline -- which hard-wires concrete classes into every call site -- code asks
the container for the interface it depends on. Swapping an implementation,
adding caching, or stubbing a dependency in tests then becomes a one-line
registration change instead of a hunt through the codebase.

This example demonstrates:
- Registering transient and singleton providers
- Named registrations for multiple variants of one interface
- The decorator registration form
- Provider decorators for cross-cutting concerns
- Runtime interface/protocol validation on resolve
- Introspection: ``len``, ``in``, ``registered_keys``, ``describe``, ``repr``
"""

from pygim.ioc import Container

# A container can reserve space upfront to avoid rehashing while you
# register services in bulk.
#
#                     ┌─ capacity: how many registrations to reserve room for
#                     ▼
container = Container(capacity=8)


# ----------------------------------------------------------------------------
# 1. Interfaces and implementations
# ----------------------------------------------------------------------------
# The "interface" can be any Python class or runtime-checkable protocol. The
# container validates every resolved object with isinstance(obj, interface),
# so implementations here simply subclass the interface.
class Repository:
    """The contract the rest of the application depends on."""


class MemoryRepository(Repository):
    def __init__(self):
        self.kind = "memory"


class CachedRepository(Repository):
    def __init__(self):
        self.kind = "cached"


# ----------------------------------------------------------------------------
# 2. Transient registration (the default lifecycle)
# ----------------------------------------------------------------------------
# A transient service is constructed anew on every resolve.
#
#                   ┌─ interface: what call sites ask for; also the lookup key
#                   │           ┌─ provider: how to build it (a class or any
#                   │           │  zero-arg callable returning an instance)
#                   ▼           ▼
container.register(Repository, MemoryRepository)

first = container.resolve(Repository)
second = container[Repository]  # container[key] is shorthand for resolve()

assert isinstance(first, Repository) and isinstance(second, Repository)
assert first is not second, "transient => a fresh instance per resolve"

# ----------------------------------------------------------------------------
# 3. Named registrations and the singleton lifecycle
# ----------------------------------------------------------------------------
# One interface can have several named variants registered side by side.
container.register(Repository, CachedRepository,
                   name="cached", lifecycle="singleton")
#                  ▲              ▲
#                  │              └─ lifecycle: "transient" (default) or
#                  │                 "singleton" (build once, then reuse)
#                  └─ name: optional label to distinguish variants of the same
#                     interface; it becomes part of the lookup key

#                              ┌─ interface: the contract, as before
#                              │           ┌─ name: the variant to fetch;
#                              │           │  (interface, name) is the full key
#                              ▼           ▼
cached_a = container.resolve((Repository, "cached"))
cached_b = container.resolve((Repository, "cached"))

assert cached_a is cached_b, "singleton => the same instance every resolve"
assert cached_a.kind == "cached"


# ----------------------------------------------------------------------------
# 4. Decorator registration form
# ----------------------------------------------------------------------------
# Omitting the provider turns register() into a class decorator: handy when
# the implementation is defined right where it is registered. The class
# itself remains usable as a normal class afterwards.
@container.register(Repository, name="decorated")
class DecoratedRepository(Repository):
    def __init__(self):
        self.kind = "decorated"


assert container.resolve((Repository, "decorated")).kind == "decorated"
assert DecoratedRepository().kind == "decorated"  # the class is untouched


# ----------------------------------------------------------------------------
# 5. Provider decorators: cross-cutting concerns
# ----------------------------------------------------------------------------
# Perfect for wrapping, tagging, or configuring objects without touching
# their classes.
def audit(instance):
    instance.audited = True
    return instance


container.register(Repository, MemoryRepository,
                   name="audited", decorators=[audit])
#                                  ▲
#                                  └─ decorators: callables applied in order
#                                     to the built instance, before the
#                                     interface check and before returning it

audited = container.resolve((Repository, "audited"))
assert audited.audited is True


# ----------------------------------------------------------------------------
# 6. The container guards the contract
# ----------------------------------------------------------------------------
# The *final* object -- after the provider and all decorators have run --
# must satisfy isinstance(obj, interface). A provider (or a misbehaving
# decorator) that returns something else fails at resolve time, loudly.
class Mailer:
    pass


container.register(Mailer, lambda: object())  # returns the wrong type!

try:
    container.resolve(Mailer)
except RuntimeError as error:
    assert "interface" in str(error)
else:
    raise AssertionError("Expected interface validation to fail")

# ----------------------------------------------------------------------------
# 7. Introspection
# ----------------------------------------------------------------------------
assert len(container) == 5           # number of registrations, not instances
assert Repository in container       # bare interface => unnamed registration
assert (Repository, "cached") in container

# registered_keys() lists every (interface, name|None) pair.
keys = [tuple(key) for key in container.registered_keys()]
assert (Repository, "cached") in keys
assert (Repository, None) in keys

# describe(key) exposes the stored ServiceDescriptor without resolving.
descriptor = container.describe((Repository, "cached"))
assert descriptor.provider is CachedRepository
assert descriptor.lifecycle == "singleton"
assert descriptor.name == "cached"

print("IoC basic container example OK:", container)
