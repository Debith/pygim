# type: ignore
"""Interface-enforcing factories with ``pygim.factory.Factory``.

A factory that hands out arbitrary objects pushes type errors downstream to
whoever uses the product. Constructing the factory with an *interface* pulls
that check forward: every created object is validated with
``isinstance(product, interface)`` at creation time, so a miswired creator
fails at the factory boundary with a clear error -- not three calls later
deep inside consumer code.

This works with regular base classes and with ``@runtime_checkable``
protocols alike, since both support isinstance checks.

This example demonstrates:
- Constructing a Factory bound to an interface
- Creators that satisfy and violate the interface
- Where and how violations surface
"""

from pygim.factory import Factory


# ----------------------------------------------------------------------------
# 1. The contract and a compliant implementation
# ----------------------------------------------------------------------------
class Storage:
    """Every product of this factory must be a Storage instance."""


class DiskStorage(Storage):
    def __init__(self, root):
        self.root = root


#                 ┌─ interface: every product must satisfy
#                 │  isinstance(product, interface) at creation time
#                 ▼
factory = Factory(interface=Storage)


@factory.register("disk")
def make_disk(root="/tmp"):
    return DiskStorage(root)


# A compliant creator passes the check transparently.
storage = factory.create("disk", root="/data")
assert isinstance(storage, Storage)
assert storage.root == "/data"


# ----------------------------------------------------------------------------
# 2. A violating creator fails at the factory boundary
# ----------------------------------------------------------------------------
# Registration itself succeeds -- the factory cannot know what a callable
# will return until it runs -- but creation validates the actual product.
@factory.register("bogus")
def make_bogus():
    return object()  # not a Storage!


try:
    factory.create("bogus")
except RuntimeError:
    pass
else:
    raise AssertionError("Expected the interface check to fail")

# The failure is per-creation: the factory itself stays fully usable.
assert isinstance(factory.create("disk"), Storage)

print("Factory interface example OK:", type(storage).__name__)
