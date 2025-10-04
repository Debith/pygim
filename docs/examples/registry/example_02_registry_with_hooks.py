"""Advanced registry example with hooks enabled.

Demonstrates:
- Hooks: on_register, on_pre, on_post
- Decorator registration form
- Override semantics via decorator
- Post hook triggering
- Using capacity pre-reservation and registered_keys introspection
- Direct id lookups with find_id
"""
from pygim.registry import Registry, KeyPolicyKind

# Create registry with hooks enabled and capacity pre-reserved
reg = Registry(hooks=True, policy=KeyPolicyKind.qualname, capacity=16)

# Event counters for demonstration
counters = {"register": 0, "pre": 0, "post": 0}

# Hook subscriptions
reg.on_register(lambda key, value: counters.__setitem__("register", counters["register"] + 1))
reg.on_pre(lambda key, value: counters.__setitem__("pre", counters["pre"] + 1))
reg.on_post(lambda key, obj: counters.__setitem__("post", counters["post"] + 1))

# Decorator form (no override)
@reg.register("task.process")
def process(x: int) -> int:
    return x + 1

assert reg["task.process"](3) == 4

# Duplicate via decorator should fail without override
try:
    @reg.register("task.process")
    def process_conflict(x: int) -> int:  # pragma: no cover - expected not to run
        return x
except RuntimeError:
    pass
else:
    raise AssertionError("Expected duplicate decorator registration to raise")

# Override via decorator
@reg.register("task.process", override=True)
def process_new(x: int) -> int:
    return x + 2

assert reg["task.process"](3) == 5

# Direct function object registration (variant name empty)
@reg.register("task.square")
def square(x: int) -> int:
    return x * x

# Trigger lookups (fires pre hooks)
assert reg["task.square"](5) == 25
assert reg["task.process"](10) == 12

# Trigger post hook manually
reg.post("task.process", None)
reg.post("task.square", None)

# Introspection
keys = reg.registered_keys()
assert any(k[0] == "task.process" for k in keys)
assert any(k[0] == "task.square" for k in keys)

# find_id fast lookup
assert reg.find_id("task.process") is reg["task.process"]

# Verify counters: at least 2 registrations (process/process_new and square)
assert counters["register"] >= 2
assert counters["pre"] >= 2  # two lookups above
assert counters["post"] >= 2  # two manual post calls

print("Hooks registry example OK:", reg, counters)
