# type: ignore
# First, we import the dispatch decorator from pygim
from pygim.performance.dispatch import dispatch

# In this example, the dispatch function is designed to handle two arguments,
# and different implementations are registered to handle different combinations
# of argument types, specifically (int, int) and (int, float).

# The benefit of this approach is that it makes the code more robust and less
# prone to errors. By defining specific behaviors for specific input types, we
# ensure that the function behaves correctly for those types. If the function is
# called with an unsupported type, a NotImplementedError is raised, alerting us
# to the issue.

# Here we're setting up dispatch_by_type to handle multiple types of inputs
# by decorating it with @dispatch
@dispatch
def dispatch_by_type():
    # If no registered function can handle the given arguments,
    # we raise a NotImplementedError
    raise NotImplementedError()

# Here we register a function to handle cases where both arguments are integers
@dispatch_by_type.register(int, int)
def _(number1, number2):
    # The function returns a string indicating the types of the arguments
    return f"Got {type(number1).__name__} and {type(number2).__name__}"

# Here we register another function to handle cases where the first argument
# is an integer and the second is a float
@dispatch_by_type.register(int, float)
def _(number1, number2):
    # Similarly, this function also returns a string indicating the types
    # of the arguments
    return f"Got {type(number1).__name__} and {type(number2).__name__}"

# We then test our dispatch function with different combinations of inputs
assert dispatch_by_type(122, 123) == "Got int and int"
assert dispatch_by_type(122, 123.0) == "Got int and float"

# We also show that dispatch can be used within a class definition
class ClassDispatchExample:
    # We define a method dispatch_by_type inside the class and decorate it
    # with @dispatch
    @dispatch
    def dispatch_by_type(self):
        # Similar to before, we raise a NotImplementedError if no registered
        # function can handle the given arguments
        raise NotImplementedError()

    # We register a method to handle cases where both arguments are integers
    @dispatch_by_type.register(int, int)
    def _(self, number1, number2):
        # The method returns a string indicating the types of the arguments
        # along with "in method"
        return f"Got {type(number1).__name__} and {type(number2).__name__} in method"

    # We register another method to handle cases where the first argument is
    # an integer and the second is a float
    @dispatch_by_type.register(int, float)
    def _(self, number1, number2):
        # Similarly, this method also returns a string indicating the types
        # of the arguments along with "in method"
        return f"Got {type(number1).__name__} and {type(number2).__name__} in method"

# We create an instance of ClassDispatchExample
example = ClassDispatchExample()
# And test the dispatch method with different combinations of inputs
assert example.dispatch_by_type(122, 123) == "Got int and int in method"
assert example.dispatch_by_type(122, 123.0) == "Got int and float in method"
