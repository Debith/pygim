# type: ignore
from pygim.gimmicks.abc import Interface

# ExampleInterface is defined as an abstract class (interface) using Interface
# from pygim.gimmicks.abc. In Python, interfaces define a set of methods that
# must be implemented by any concrete class that inherits from them.
class ExampleInterface(Interface):
    # Declaring an abstract method. This method should be implemented by any
    # subclass of ExampleInterface. It acts as a contract, indicating that
    # subclasses must provide their own version of this method.
    def test_func(): pass

    # Declaring an abstract property. Similar to abstract methods, this
    # defines a property that must be implemented by subclasses. Properties in
    # Python provide a way to use getter and setter functions like regular
    # attributes, which can add logic around accessing and setting a value.
    @property
    def test_prop(): pass


# ExampleClass is a concrete class that implements the ExampleInterface.
# It must provide implementations for all abstract methods and properties
# defined in the interface.
class ExampleClass(ExampleInterface):
    # Providing the implementation of the abstract method 'test_func' from the
    # interface. This concrete implementation is what makes ExampleClass a
    # non-abstract (concrete) class.
    def test_func(self):
        return "Actual method implementation"

    # Implementing the abstract property 'test_prop' from the interface.
    # The property decorator is used here to define getter method for
    # the property.
    @property
    def test_prop(self):
        return "Actual property implementation"


# Creating an instance of ExampleClass.
inst = ExampleClass()

# Assert statements to test the functionality of ExampleClass.
# These statements verify that the implementations of the method and property
# work as expected.
assert inst.test_func() == "Actual method implementation"
assert inst.test_prop == "Actual property implementation"

# Assert statements to ensure that the methods and properties in the interface
# are still abstract. This is more of a demonstration that the interface
# itself still uses abstract methods and properties from python's abc module.
assert ExampleInterface.test_func.__isabstractmethod__, "test_func should be an abstract method!"
assert ExampleInterface.test_prop.__isabstractmethod__, "test_prop should be an abstract property!"
