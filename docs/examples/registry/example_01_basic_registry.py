# type: ignore
"""Basic usage of ``pygim.registry.Registry`` (hooks disabled).

A registry is a process-wide lookup table that maps stable identifiers to
callables (or any objects). It decouples the code that *provides* a
capability from the code that *uses* it: plugins, handlers, and strategies
register themselves under well-known ids, and consumers look them up without
importing the provider module directly.

The pygim registry is implemented in C++ and offers two key policies:

- ``qualname`` -- keys are strings, either handcrafted ids or the
  ``module.qualname`` derived from a registered object
- ``identity`` -- keys are the object instances themselves (compared by id)

This example demonstrates:
- Registering under a handcrafted string id and via the function object
- Lookup, containment, and calling through the registry
- Strict override semantics (duplicates raise without ``override=True``)
- Introspecting registered keys and fast id lookup with ``find_id``
- The identity policy for object-keyed registration
"""

from pygim.registry import Registry, KeyPolicyKind

# ----------------------------------------------------------------------------
# 1. Create a registry
# ----------------------------------------------------------------------------
# Hooks are compiled out entirely when disabled, so a hook-free registry
# pays zero overhead for them.
#
#              ┌─ hooks: compile in on_register/on_pre/on_post support?
#              │            ┌─ policy: how keys are formed -- from strings
#              │            │  (qualname) or from objects (identity)
#              ▼            ▼
reg = Registry(hooks=False, policy=KeyPolicyKind.qualname)


# ----------------------------------------------------------------------------
# 2. Register under a handcrafted string id
# ----------------------------------------------------------------------------
def add(a, b):
    return a + b


#            ┌─ id: the key; any string works under the qualname policy
#            │           ┌─ value: the object to store (any Python object)
#            ▼           ▼
reg.register("math.add", add)

assert "math.add" in reg
assert reg["math.add"](2, 3) == 5


# ----------------------------------------------------------------------------
# 3. Register by passing the object itself
# ----------------------------------------------------------------------------
# When given a non-string key object, the qualname policy derives the id as
# module.qualname -- convenient when you don't want to invent names.
def mul(a, b):
    return a * b


reg.register(mul, mul)

qual_id = f"{mul.__module__}.{mul.__qualname__}"
assert qual_id in [key[0] for key in reg.registered_keys()]
assert reg[qual_id](4, 5) == 20

# ----------------------------------------------------------------------------
# 4. Strict override semantics
# ----------------------------------------------------------------------------
# Duplicates are rejected unless explicitly overridden. This inverts the
# "last writer wins" behaviour of a plain dict and turns accidental
# double-registration into an immediate, debuggable error.
try:
    reg.register("math.add", lambda a, b: a - b)
except RuntimeError as error:
    assert "Duplicate" in str(error)
else:
    raise AssertionError("Expected duplicate registration to raise")

#                                            ┌─ override: replace instead of
#                                            │  rejecting the duplicate
#                                            ▼
reg.register("math.add", lambda a, b: a - b, override=True)
assert reg["math.add"](5, 3) == 2

# ----------------------------------------------------------------------------
# 5. Introspection
# ----------------------------------------------------------------------------
# registered_keys() returns (id, variant_name) tuples; the variant name is
# the empty string unless one was supplied at registration.
keys = reg.registered_keys()
assert any(key[0] == "math.add" for key in keys)

# find_id is a fast path for qualname lookups: it returns the value or None
# instead of raising, which suits speculative "is this available?" checks.
assert reg.find_id("math.add") is reg["math.add"]
assert reg.find_id("does.not.exist") is None

# ----------------------------------------------------------------------------
# 6. The identity policy: object-keyed registration
# ----------------------------------------------------------------------------
# With policy=identity the *object itself* is the key (compared by id).
# String ids are rejected -- the two policies do not mix.
id_reg = Registry(hooks=False, policy=KeyPolicyKind.identity)


class Sentinel:
    pass


token = Sentinel()
id_reg.register(token, lambda: "found by identity")

assert token in id_reg
assert id_reg[token]() == "found by identity"

try:
    id_reg.register("a.string.id", lambda: None)
except Exception:
    pass
else:
    raise AssertionError("Expected identity policy to reject a string id")

print("Basic registry example OK:", reg, id_reg)
