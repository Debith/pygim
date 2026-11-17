# type: ignore
"""Rebuild a method's code so it belongs to another class: ``pygim.traits.CodeBuilder``.

Inside ``class Trait:``, Python compiles ``self.__secret`` as ``self._Trait__secret`` — the
private name is "mangled" with the class's name. Copy that method's code into another class and
it still reads ``_Trait__secret``, an attribute the new class never stored. ``CodeBuilder``
rewrites the code for its new owner: private names and qualified names, in the code and in every
code object nested in it. This is the step a trait takes when it is composed into a class
(docs/design/traits.md).

This example demonstrates:
- what Python stores for a private name, and why copied code misses it
- moving a method's code from one class to another
- nested code (a lambda) rebuilt together with the code that holds it
- the original code object, untouched
"""

import types

from pygim.traits import CodeBuilder


class Trait:
    def secret(self):
        return self.__secret

    def secret_later(self):
        return (lambda: self.__secret)()


class Target:
    def __init__(self):
        self.__secret = 42


# ----------------------------------------------------------------------------
# 1. What Python compiled: each class spells the private name with its own name
# ----------------------------------------------------------------------------
assert Trait.secret.__code__.co_names == ("_Trait__secret",)
assert vars(Target()) == {"_Target__secret": 42}


# ----------------------------------------------------------------------------
# 2. Copied as it is, the code reads the first class's spelling
# ----------------------------------------------------------------------------
copied = types.FunctionType(Trait.secret.__code__, globals())
try:
    copied(Target())
except AttributeError as error:
    assert "_Trait__secret" in str(error)
else:
    raise AssertionError("expected the copied code to miss _Trait__secret")


# ----------------------------------------------------------------------------
# 3. Rebuilt for its new owner
# ----------------------------------------------------------------------------
#                     ┌─ code: the code object to start from; it is never changed
#                     ▼
builder = CodeBuilder(Trait.secret.__code__)
#                    ┌─ source: the class the code was compiled in
#                    │      ┌─ target: the class it will belong to
#                    ▼      ▼
code = builder.reown(Trait, Target).build()

assert code.co_names == ("_Target__secret",)
assert types.FunctionType(code, globals())(Target()) == 42


# ----------------------------------------------------------------------------
# 4. Nested code is rebuilt with the code that holds it
# ----------------------------------------------------------------------------
nested = CodeBuilder(Trait.secret_later.__code__).reown(Trait, Target).build()

assert types.FunctionType(nested, globals())(Target()) == 42


# ----------------------------------------------------------------------------
# 5. The original is untouched
# ----------------------------------------------------------------------------
assert Trait.secret.__code__.co_names == ("_Trait__secret",)

print("CodeBuilder example OK:", code.co_names)
