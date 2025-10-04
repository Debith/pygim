"""Basic usage of pygim.registry.Registry without hooks.

This example shows:
- Creating a registry (qualname policy, hooks disabled)
- Registering callables via string id and via function object
- Looking up, calling, containment, and repr
- Using override semantics (raises on duplicate unless override=True)
- Introspecting registered keys
"""
from pygim.registry import Registry, KeyPolicyKind

# Create a registry (default policy=qualname, hooks disabled)
reg = Registry(hooks=False, policy=KeyPolicyKind.qualname)

# 1. Register a function by string id
# (For qualname policy the id can be any string; collisions are treated as duplicates.)
def add(a, b):
    return a + b

reg.register("math.add", add)
assert "math.add" in reg
assert reg["math.add"](2, 3) == 5

# 2. Register by passing the function object itself (id becomes module.qualname)
#    This is convenient when you don't want to handcraft a string id.

def mul(a, b):
    return a * b

reg.register(mul, mul)
qual_id = mul.__module__ + "." + mul.__qualname__
assert qual_id in [k[0] for k in reg.registered_keys()]

# 3. Duplicate registration without override => error
try:
    reg.register("math.add", lambda a, b: a - b)
except RuntimeError as e:
    assert "Duplicate" in str(e)
else:
    raise AssertionError("Expected duplicate registration to raise")

# 4. Override existing entry
reg.register("math.add", lambda a, b: a - b, override=True)
assert reg["math.add"](5, 3) == 2

# 5. Introspection: list registered keys (id_or_object, variant_name)
keys = reg.registered_keys()
# variant_name is empty string when not supplied
assert any(t[0] == "math.add" for t in keys)

# 6. Direct id lookup via find_id (qualname policy) -- returns callable or None
found = reg.find_id("math.add")
assert found is reg["math.add"]
missing = reg.find_id("math.add.missing")
assert missing is None

print("Basic registry example OK:", reg)
