# type: ignore
"""Registry hooks: observing registration and lookup events.

Hooks let you attach observers to a registry without wrapping it: audit
logging, metrics, lazy warm-up, or cache invalidation can react to events
while registration and lookup call sites stay untouched.

Three hook points exist:

- ``on_register`` -- fires when an entry is added (or overridden)
- ``on_pre``      -- fires on each lookup, before the value is returned
- ``on_post``     -- fired explicitly via ``post(key, obj)``, e.g. after the
  caller has finished constructing or configuring the looked-up object

Hook support is chosen at construction time (``hooks=True``); a registry
built without hooks compiles them out entirely and pays nothing for them.

This example demonstrates:
- Subscribing to all three hook points
- Decorator-form registration with and without override
- Manually triggering post hooks
- Capacity pre-reservation for bulk registration
"""

from pygim.registry import Registry, KeyPolicyKind

# ----------------------------------------------------------------------------
# 1. Create a registry with hooks and pre-reserved capacity
# ----------------------------------------------------------------------------
#              ┌─ capacity: reserve space for this many entries upfront,
#              │  avoiding rehashes while bulk-registering
#              ▼
reg = Registry(capacity=16, hooks=True, policy=KeyPolicyKind.qualname)

# The observers below just record events; real code might log or invalidate.
events = {"register": [], "pre": [], "post": []}

reg.on_register(lambda key, value: events["register"].append(key))
reg.on_pre(lambda key, value: events["pre"].append(key))
reg.on_post(lambda key, obj: events["post"].append(key))


# ----------------------------------------------------------------------------
# 2. Decorator-form registration
# ----------------------------------------------------------------------------
# register() doubles as a decorator...
@reg.register("task.process")
def process(x: int) -> int:
    return x + 1


assert reg["task.process"](3) == 4

# ...with the same strict duplicate semantics as the direct form.
try:

    @reg.register("task.process")
    def process_conflict(x: int) -> int:
        return x

except RuntimeError:
    pass
else:
    raise AssertionError("Expected duplicate decorator registration to raise")


# Overriding through the decorator form works too.
@reg.register("task.process", override=True)
def process_v2(x: int) -> int:
    return x + 2


assert reg["task.process"](3) == 5


@reg.register("task.square")
def square(x: int) -> int:
    return x * x


# ----------------------------------------------------------------------------
# 3. Watching the hooks fire
# ----------------------------------------------------------------------------
# Successful registrations so far: process, process_v2 (override), square.
# The rejected duplicate never fired on_register.
assert len(events["register"]) == 3

# Every lookup fires on_pre once: two lookups happened in section 2, and the
# two below bring the total to four. (find_id, by contrast, does not fire it.)
assert reg["task.square"](5) == 25
assert reg["task.process"](10) == 12
assert len(events["pre"]) == 4

# Post hooks are explicit: the registry cannot know when the caller is
# "done" with a value, so you announce that moment yourself.
reg.post("task.process", None)
reg.post("task.square", None)
assert len(events["post"]) == 2

# ----------------------------------------------------------------------------
# 4. Introspection works as usual alongside hooks
# ----------------------------------------------------------------------------
keys = [key[0] for key in reg.registered_keys()]
assert "task.process" in keys and "task.square" in keys
assert reg.find_id("task.process") is reg["task.process"]

print("Hooks registry example OK:", reg, {k: len(v) for k, v in events.items()})
