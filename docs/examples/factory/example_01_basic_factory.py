# type: ignore
"""Basic usage of ``pygim.factory.Factory``.

A factory maps *names* to *creator callables*. Call sites ask for a product
by name (often read from config or user input) and the factory invokes the
matching creator with the arguments provided -- no if/elif chains over type
names, and new products can be added without touching the consumers.

This example demonstrates:
- Registering creators directly and via the decorator form
- Creating objects with positional and keyword arguments
- Retrieving the raw creator with ``factory[name]``
- Strict override semantics, the same contract as Registry and Container
- Listing registered creators and triggering registrations via ``use_module``
"""

from pygim.factory import Factory

factory = Factory()


# ----------------------------------------------------------------------------
# 1. Register creators
# ----------------------------------------------------------------------------
# Any callable works: classes, functions, lambdas. Register directly...
class Circle:
    def __init__(self, radius):
        self.radius = radius


#                ┌─ name: the product's public lookup name
#                │         ┌─ creator: any callable that builds the product
#                ▼         ▼
factory.register("circle", Circle)


# ...or with the decorator form, which keeps the definition and the
# registration side by side.
@factory.register("square")
def make_square(side):
    return {"shape": "square", "side": side}


# `factory[name]` returns the raw creator without calling it.
assert factory["circle"] is Circle

# ----------------------------------------------------------------------------
# 2. Create products by name
# ----------------------------------------------------------------------------
#                       ┌─ which creator to invoke
#                       │         ┌─ forwarded to the creator (args & kwargs)
#                       ▼         ▼
circle = factory.create("circle", 3)
square = factory.create("square", side=4)

assert circle.radius == 3
assert square == {"shape": "square", "side": 4}

# The name usually comes from data, which is the whole point: the consumer
# below handles any shape ever registered without knowing a single class.
for shape_name, argument in [("circle", 1), ("square", 2)]:
    product = factory.create(shape_name, argument)
    assert product  # no if/elif over shape names anywhere

# Asking for an unknown name fails loudly.
try:
    factory.create("triangle")
except RuntimeError:
    pass
else:
    raise AssertionError("Expected unknown name to raise")

# ----------------------------------------------------------------------------
# 3. Strict override semantics
# ----------------------------------------------------------------------------
# The same contract as Registry and Container: duplicates raise unless
# override=True, and override=True requires an existing entry.
try:
    factory.register("circle", lambda radius: None)
except RuntimeError:
    pass
else:
    raise AssertionError("Expected duplicate registration to raise")

try:
    #                                         ┌─ override: demands an existing
    #                                         │  entry; "hexagon" is unknown,
    #                                         │  so this raises
    #                                         ▼
    factory.register("hexagon", lambda: None, override=True)
except RuntimeError:
    pass
else:
    raise AssertionError("Expected override of a missing entry to raise")

factory.register("circle", lambda radius: Circle(radius * 2), override=True)
assert factory.create("circle", 3).radius == 6

# ----------------------------------------------------------------------------
# 4. Introspection and module-driven registration
# ----------------------------------------------------------------------------
assert set(factory.registered_callables()) == {"circle", "square"}

# use_module() imports a module purely for its registration side effects --
# the classic plugin pattern where each plugin registers itself on import.
factory.use_module("math")  # any importable module works; math registers nothing
try:
    factory.use_module("definitely_not_an_installed_module")
except ModuleNotFoundError:
    pass
else:
    raise AssertionError("Expected a missing module to raise")

print("Basic factory example OK:", sorted(factory.registered_callables()))
