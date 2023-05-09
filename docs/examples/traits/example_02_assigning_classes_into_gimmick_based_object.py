# type: ignore
from pygim import gimmick

# Create a new class `ExampleObject` that inherits from the `gimmick` class
class ExampleObject(gimmick):
    def __init__(self):
        # Define three instance variables with different levels of accessibility
        self.public = 1
        self._protected = 2  # protected variable (single underscore prefix)
        self.__private = 3  # private variable (double underscore prefix)

    # Define a method that returns a tuple of the instance variables
    def original(self):
        return self.public, self._protected, self.__private


# Define a new class `AdditionalClass1` that has methods that return a tuple
# of the instance variables of its future class.
class AdditionalClass1:
    def method1(self):
        return self.public, self._protected, self.__private

    def method2(self):
        return self.public, self._protected, self.__private

# Define a new class `AdditionalClass2` that has methods that return a tuple
# of the instance variables of its future class.
class AdditionalClass2:
    def method3(self):
        return self.public, self._protected, self.__private

    def method4(self):
        return self.public, self._protected, self.__private

# The `<<` operator is used to add a class that already exists to `ExampleObject`
ExampleObject << AdditionalClass1 << AdditionalClass2

# Create a new `ExampleObject` object
obj = ExampleObject()

# Assert that the added methods return the same values as the original `original` method
assert obj.method1() == obj.original()
assert obj.method2() == obj.original()
assert obj.method3() == obj.original()
assert obj.method4() == obj.original()

# It's important to note that there is always the option to introspect the contents of the
# `gimmick` object and its potential traits. The `gimmick` object holds the traceback from
# where the trait was assigned to the class, as well as the line where the trait is defined.
assert f"{__name__}.ExampleObject.method1" in obj.__pygim_traits__
assert f"{__name__}.ExampleObject.method2" in obj.__pygim_traits__
assert f"{__name__}.ExampleObject.method3" in obj.__pygim_traits__
assert f"{__name__}.ExampleObject.method4" in obj.__pygim_traits__
assert len(obj.__pygim_traits__) == 4