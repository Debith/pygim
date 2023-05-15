# Import necessary modules
import enum
from pygim import dispatch

# The following approach leverages the specific enumerated values to dispatch to the
# correct function, which enables concise and readable handling of different enumeration
# cases. It simplifies the control flow and improves the maintainability of the code by
# removing the need for numerous if-else statements or switch cases. Moreover, it enhances
# type safety as enumerated types provide a way of defining a type consisting of a set of
# named values, which can help prevent programming errors.

# Define an enumeration class, ExampleEnum
class ExampleEnum(enum.Enum):
    FIRST = "First Entry"
    SECOND = "Second Entry"

# Define a dispatch function, dispatch_by_enum, to handle multiple types of inputs
# Decorate it with @dispatch
@dispatch
def dispatch_by_enum():
    # If no registered function can handle the given arguments, raise a NotImplementedError
    raise NotImplementedError()

# Register a function to handle cases where the first argument is ExampleEnum.FIRST
@dispatch_by_enum.register(ExampleEnum.FIRST)
def _(__enum, __number_arg):
    # The function returns a string indicating it has received the value ExampleEnum.FIRST
    # and a number
    return f"First got `{__enum.value}` and {__number_arg}"

# Register another function to handle cases where the first argument is ExampleEnum.SECOND
@dispatch_by_enum.register(ExampleEnum.SECOND)
def _(__enum, __number_arg):
    # The function returns a string indicating it has received the value ExampleEnum.SECOND
    # and a number
    return f"Second got `{__enum.value}` and {__number_arg}"

# Test the dispatch function with different inputs
assert dispatch_by_enum(ExampleEnum.FIRST, 1) == "First got `First Entry` and 1"
assert dispatch_by_enum(ExampleEnum.SECOND, 2) == "Second got `Second Entry` and 2"

# Show that dispatch can be used within a class definition
class ClassDispatchExample:
    # Define a method dispatch_by_enum inside the class and decorate it with @dispatch
    @dispatch
    def dispatch_by_enum(self):
        # If no registered function can handle the given arguments, raise a NotImplementedError
        raise NotImplementedError()

    # Register a method to handle cases where the first argument is ExampleEnum.FIRST
    @dispatch_by_enum.register(ExampleEnum.FIRST)
    def _(self, __enum, __number_arg):
        # The method returns a string indicating it has received the value ExampleEnum.FIRST
        # and a number
        return f"First got `{__enum.value}` and {__number_arg} in method"

    # Register another method to handle cases where the first argument is ExampleEnum.SECOND
    @dispatch_by_enum.register(ExampleEnum.SECOND)
    def _(self, __enum, __number_arg):
        # The method returns a string indicating it has received the value ExampleEnum.SECOND
        # and a number
        return f"Second got `{__enum.value}` and {__number_arg} in method"

# Create an instance of ClassDispatchExample
example = ClassDispatchExample()
# Test the dispatch method with different inputs
assert example.dispatch_by_enum(ExampleEnum.FIRST, 1) == "First got `First Entry` and 1 in method"
assert example.dispatch_by_enum(ExampleEnum.SECOND, 2) == "Second got `Second Entry` and 2 in method"
