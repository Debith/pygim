# type: ignore
# First, import the dispatch decorator from pygim.
from pygim.performance.dispatch import dispatch

# Create a default function that raises an exception if called with an unregistered type.
# This effectively creates a default function with no behavior, which can be useful
# when no behavior should be provided for unregistered types.
dispatch_by_type = dispatch.default_unregistered_function()

# Register functions to handle different argument types using the .register
# method on the dispatch function.
@dispatch_by_type.register(int)
def _(value):
    # Handle the case where the argument is an integer.
    return f"This is {type(value).__name__}"

@dispatch_by_type.register(float)
def _(value):
    # Handle the case where the argument is a float.
    return f"This is {type(value).__name__}"

# Assert that the dispatch function produces the expected results for various inputs.
assert dispatch_by_type(123) == "This is int"
assert dispatch_by_type(123.0) == "This is float"

# Demonstrate that an exception is raised when calling with an unregistered type.
try:
    dispatch_by_type("unregistered type")
except NotImplementedError:
    print("Caught expected exception for unregistered type")

# You can also use dispatch within a class definition.
class ClassDispatchExample:
    # Define the dispatch function within the class and decorate it with dispatch.
    dispatch_by_type = dispatch.default_unregistered_function()

    # Define methods to handle different argument types and register them using .register.
    @dispatch_by_type.register(int)
    def _(self, value):
        # Handle the case where the argument is an integer.
        return f"This is {type(value).__name__}"

    @dispatch_by_type.register(float)
    def _(self, value):
        # Handle the case where the argument is a float.
        return f"This is {type(value).__name__}"

# Create an instance of ClassDispatchExample and test the dispatch function.
example = ClassDispatchExample()
assert example.dispatch_by_type(123) == "This is int"
assert example.dispatch_by_type(123.0) == "This is float"

# Demonstrate that an exception is raised when calling with an unregistered type.
try:
    example.dispatch_by_type("unregistered type")
except NotImplementedError:
    print("Caught expected exception for unregistered type")