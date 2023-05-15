# type: ignore
from pygim import EntangledClass

# The `EntangledClass` is capable of detecting the underlying package. However, it is
# advisable to specify the name of your project. The namespace's name must be hashable;
# using strings is the easiest approach. However, enumerations can make the code more
# structured and organized.
assert EntangledClass.__pygim_namespace__ == EntangledClass["pygim"].__pygim_namespace__
assert EntangledClass["AnotherNamespace"].__pygim_namespace__ != EntangledClass["pygim"].__pygim_namespace__
assert id(EntangledClass) == id(EntangledClass["pygim"])
assert EntangledClass.__name__ == "EntangledClass"

# At this point, we can be sure that:

# This is the first occasion we are creating `ExampleObject` class, which directly
# inherits from `EntangledClass` in `pygim`'s scope.
class ExampleObject(EntangledClass):
    def __init__(self, value):
        self.__value = value  # Notice that this is a private value.

    def __eq__(self, other):
        return self.__value == other.__value


# The new class has few properties, verified here.
assert ExampleObject.__pygim_namespace__ == "pygim"
assert id(ExampleObject) != id(EntangledClass)
assert ExampleObject.__name__ == "ExampleObject"

# At this point, we can instantiate `ExampleObject` classes normally.
example1 = ExampleObject(42)
example2 = ExampleObject(42)
example3 = ExampleObject(24)

# Since `ExampleObject` implements the `__eq__()` method, it is possible to ensure
# that functions work correctly and each object is still individual.
assert id(example1) != id(example2), f"{id(example1)} == {id(example2)}"
assert id(example1) != id(example3), f"{id(example1)} == {id(example3)}"
assert example1 == example2
assert example2 != example3

# This is the second time we define `ExampleObject` class. This time, we add `__int__()`
# method that returns the member variable that was declared private.
class ExampleObject(EntangledClass):
    def __int__(self):
        return self.__value

# Creating a new object here provides similar results.
example4 = ExampleObject(24)

assert id(example4) != id(example3), f"{id(example4)} == {id(example3)}"
assert id(example1) != id(example2), f"{id(example1)} == {id(example2)}"
assert id(example1) != id(example3), f"{id(example1)} == {id(example3)}"
assert example1 == example2
assert example2 != example3

# Finally, the actual magic. The new instance contains the `__eq__()` method from the
# first definition of the class. Also, the subsequent class defining `__int__()` method
# is also found from all instances of the `ExampleObject` class.
assert example4 == example3
assert int(example4) == 24  # Spooky action at a distance
assert int(example1) == 42  # New function applies also to old instances.
