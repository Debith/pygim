# type: ignore
"""Broadcasting attribute access and method calls with ``pygim.each``.

``each`` wraps any iterable in a lightweight C++ proxy that fans a single
attribute access or method call out to every element, collecting the results
into a list. It replaces one-line-but-noisy comprehensions like
``[item.method(arg) for item in items]`` with ``each(items).method(arg)``,
keeping the *intent* -- do this to all of them -- front and center.

This example demonstrates:
- Broadcasting attribute reads, property reads, and method calls
- Forwarding positional and keyword arguments
- Broadcasting over arbitrary iterables, including built-in types
- The dunder guard rail that keeps proxy behaviour unambiguous
"""

from pygim.each import each


class Sensor:
    def __init__(self, name, value):
        self.name = name
        self.value = value

    @property
    def label(self):
        return f"{self.name}={self.value}"

    def scaled(self, factor=2):
        return self.value * factor


sensors = [Sensor("t1", 10), Sensor("t2", 20), Sensor("t3", 30)]

# ----------------------------------------------------------------------------
# 1. Attribute and property broadcast
# ----------------------------------------------------------------------------
# Accessing an attribute on the proxy reads it from every element and
# returns the collected list. Properties are just attributes, so they
# broadcast the same way.
assert each(sensors).name == ["t1", "t2", "t3"]
assert each(sensors).value == [10, 20, 30]
assert each(sensors).label == ["t1=10", "t2=20", "t3=30"]

# ----------------------------------------------------------------------------
# 2. Method broadcast with arguments
# ----------------------------------------------------------------------------
# Accessing a method returns a broadcastable callable; calling it invokes
# the method on every element with the same arguments -- positional and
# keyword arguments are forwarded untouched.
assert each(sensors).scaled() == [20, 40, 60]
assert each(sensors).scaled(3) == [30, 60, 90]
assert each(sensors).scaled(factor=10) == [100, 200, 300]

# Any iterable works, including tuples of built-in types:
assert each(("a", "bb", "ccc")).upper() == ["A", "BB", "CCC"]

# ----------------------------------------------------------------------------
# 3. Guard rail: dunder access is refused
# ----------------------------------------------------------------------------
# Forwarding dunders (__len__, __iter__, ...) would make it impossible to
# tell whether you mean the *proxy's* protocol or a broadcast over the
# elements, so `each` refuses them explicitly instead of guessing.
try:
    each(sensors).__len__
except AttributeError as error:
    assert "dunder" in str(error)
else:
    raise AssertionError("Expected dunder access to be refused")

print("each broadcasting example OK:", each(sensors).label)
