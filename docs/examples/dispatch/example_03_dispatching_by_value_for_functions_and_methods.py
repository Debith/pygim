# type: ignore
# Import the dispatch decorator from pygim
from pygim import dispatch

# This approach, which extends the functionality of the dispatch decorator to allow
# dispatching based on the values of arguments, offers increased flexibility and
# adaptability in function behavior. It allows a single function to accommodate
# diverse scenarios based on argument values, not just their types or number. This
# reduces the need for conditional logic within the function, leading to cleaner,
# more readable, and maintainable code. It also enhances the modularity of the code,
# as new behaviors for different argument values can be added or removed easily by
# registering or unregistering the corresponding functions.

# Define a dispatch function, dispatch_by_value, to handle multiple types of inputs
# Decorate it with @dispatch
@dispatch
def dispatch_by_value():
    # If no registered function can handle the given arguments, raise a NotImplementedError
    raise NotImplementedError()

# Register a function to handle cases where the argument value is "first"
@dispatch_by_value.register("first")
def _(arg):
    # The function returns a string indicating it has received the value "first"
    return f"First got {arg}"

# Register another function to handle cases where the argument value is "second"
@dispatch_by_value.register("second")
def _(arg):
    # The function returns a string indicating it has received the value "second"
    return f"Second got {arg}"

# Test the dispatch function with the argument values "first" and "second"
assert dispatch_by_value("first") == "First got first"
assert dispatch_by_value("second") == "Second got second"

# Show that dispatch can be used within a class definition
class ClassDispatchExample:
    # Define a method dispatch_by_value inside the class and decorate it with @dispatch
    @dispatch
    def dispatch_by_value(self):
        # If no registered function can handle the given arguments, raise a NotImplementedError
        raise NotImplementedError()

    # Register a method to handle cases where the argument value is "first"
    @dispatch_by_value.register("first")
    def _(self, arg):
        # The method returns a string indicating it has received the value "first" in a method
        return f"First got {arg} in method"

    # Register another method to handle cases where the argument value is "second"
    @dispatch_by_value.register("second")
    def _(self, arg):
        # The method returns a string indicating it has received the value "second" in a method
        return f"Second got {arg} in method"

# Create an instance of ClassDispatchExample
example = ClassDispatchExample()
# Test the dispatch method with the argument values "first" and "second"
assert example.dispatch_by_value("first") == "First got first in method"
assert example.dispatch_by_value("second") == "Second got second in method"
