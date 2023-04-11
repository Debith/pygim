# type: ignore
from pygim import EntangledClass, encls
from pygim.exceptions import EntangledMethodError

# This creates inheritable class locally so that it is more evident, which class is
# used.
EntangledExample = encls.Example2.EntangledClass
assert id(encls.Example2.EntangledClass) == id(EntangledClass["Example2"])

# This is the first occasion we are creating `ExampleObject` class, which directly
# inherits from `EntangledClass` in `pygim`'s scope.
class ExampleObject(EntangledExample):
    def __init__(self, value):
        self.__value = value  # Notice that this is a private value.

    @property
    def value(self):
        return self.__value

# The new class is automatically included in the namespace of the inherited class.
assert EntangledExample.__pygim_namespace__ == "Example2"
assert ExampleObject.__pygim_namespace__ == "Example2"

# Trying to overwrite value property via another class is not possible as that could
# have some serious consequences, if done carelessly.
try:
    class ExampleObject(EntangledExample):
        @property
        def value(self):
            return self.__value * 2

except EntangledMethodError as e:
    assert str(e) == "Can't override following names: value"

else:
    assert False, "Exception should be raised!"


# Method of the `EntangledClass` can be overridden only if it explitly allowed...
class ExampleObject(EntangledExample):
    @overrideable
    @property
    def another_value(self):
        return self.__value


# ...and it is explicitly overridden. The `EntangledClass` provides these decorators
# automagically within its namespace.
class ExampleObject(EntangledExample):
    @overrides
    @property
    def another_value(self):
        return self.__value * 10


# The instance of `ExampleObject` now holds overridden property.
obj = ExampleObject(42)
assert obj.value == 42
assert obj.another_value == 420