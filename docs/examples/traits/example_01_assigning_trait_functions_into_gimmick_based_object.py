# type: ignore
from pygim import gimmick

# Create a new class ExampleObject that inherits from the gimmick class
class ExampleObject(gimmick):
    def __init__(self):
        # Define three instance variables with different levels of accessibility
        self.public = 1
        self._protected = 2  # protected variable (single underscore prefix)
        self.__private = 3  # private variable (double underscore prefix)

    # Define a method that returns a tuple of the instance variables
    def original(self):
        return self.public, self._protected, self.__private

# Define a standalone function that takes a self parameter
def module_level_function(self):
    # The function returns a tuple of the instance variables
    return self.public, self._protected, self.__private

# Define a new class AdditionalClass
class AdditionalClass:
    # Define a method that takes a self parameter
    def method(self):
        # The method returns a tuple of the instance variables
        return self.public, self._protected, self.__private

# The << operator is used to add a new method to the class that already exists
# outside of the class definition. This is known as monkey-patching. In the code,
# the `module_level_function` and `AdditionalClass.method` methods are attached to
# the `ExampleObject` class using the << operator.
ExampleObject << module_level_function
ExampleObject << AdditionalClass.method

# The other way to add methods to a class is by assignment using dot notation.
# This is done by assigning a function or method to a new variable in the class
# definition. In the code, two new methods are added to the ExampleObject class using
# dot notation: `new_method1` and `new_method2`.
ExampleObject.new_method1 = module_level_function
ExampleObject.new_method2 = AdditionalClass.method

# Create a new ExampleObject object
obj = ExampleObject()

# Assert that the added methods return the same values as the original 'original' method
assert obj.module_level_function() == obj.original()
assert obj.new_method1() == obj.original()
assert obj.new_method2() == obj.original()

# It's important to note that there is always the option to introspect the contents of the
# `gimmick` object and its potential traits. The `gimmick` object holds the traceback from
# where the trait was assigned to the class, as well as the line where the trait is defined.
assert f"{__name__}.ExampleObject.module_level_function" in obj.__pygim_traits__
assert f"{__name__}.ExampleObject.method" in obj.__pygim_traits__
assert f"{__name__}.ExampleObject.new_method1" in obj.__pygim_traits__
assert f"{__name__}.ExampleObject.new_method2" in obj.__pygim_traits__
