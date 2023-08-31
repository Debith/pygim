# type: ignore
# First, import the dispatch decorator from pygim.
from pygim.performance.dispatch import dispatch

# The purpose of dispatching is to enable a single function name to handle
# multiple types of input. In the provided example code, the dispatch_by_type
# function is decorated with @dispatch, allowing it to have multiple
# implementations. Each implementation is registered to handle a specific type
# of input.
#
# Individual implementations are registered through the .register method on the
# dispatched function. Dispatching leads to more concise and readable code, especially
# when dealing with various types of input, by allowing a single function to handle
# different data types instead of defining separate functions for each type.

@dispatch
def dispatch_by_type():
    """
    Dispatch function to handle various types.

    Raises
    ------
    NotImplementedError
        If no function is registered to handle the given arguments.
    """
    # Raise an error if no function is registered to handle the given arguments.
    raise NotImplementedError()

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
    @dispatch
    def dispatch_by_type(self):
        """
        Dispatch function to handle various types within a class.

        Raises
        ------
        NotImplementedError
            If no method is registered to handle the given arguments.
        """
        # Raise an error if no method is registered to handle the given arguments.
        raise NotImplementedError()

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
