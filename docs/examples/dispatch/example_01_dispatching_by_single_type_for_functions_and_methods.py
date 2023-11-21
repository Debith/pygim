# type: ignore
# first, import the dispatch decorator from pygim
from pygim.performance.dispatch import dispatch

# The purpose of dispatching is to allow a single function name to handle
# multiple types of input. In the provided example code, the dispatch_by_type
# function is decorated with @dispatch, which means it can have multiple
# implementations, each handling a specific type of input.
#
# The individual implementations are registered through the use of register
# method on the dispatched function, with each implementation handling a
# specific type of input.
#
# Dispatching allows simplified and cleaner code, especially when dealing
# with multiple types of input. Instead of having to define separate functions
# for each data type, we can use dispatching to handle them all in one function.

@dispatch
def dispatch_by_type():
    # raise an error if no function is able to handle the given arguments
    raise NotImplementedError()

# register functions to handle different argument types using the .register
# method on the dispatch function
@dispatch_by_type.register(int)
def _(value):
    # handle the case where the argument is an integer
    return f"This is {type(value).__name__}"

@dispatch_by_type.register(float)
def _(value):
    # handle the case where the argument is a float
    return f"This is {type(value).__name__}"

# assert that the dispatch function outputs the expected results for various inputs
assert dispatch_by_type(123) == "This is int"
assert dispatch_by_type(123.0) == "This is float"

# you can also use dispatch within a class definition
class ClassDispatchExample:
    # define the dispatch function within the class and decorate it with dispatch
    @dispatch
    def dispatch_by_type(self):
        # raise an error if no function is able to handle the given arguments
        raise NotImplementedError()

    # define methods to handle different argument types and register them using .register
    @dispatch_by_type.register(int)
    def _(self, value):
        # handle the case where the argument is an integer
        return f"This is {type(value).__name__}"

    @dispatch_by_type.register(float)
    def _(self, value):
        # handle the case where the argument is a float
        return f"This is {type(value).__name__}"

# create an instance of the ClassDispatchExample and test the dispatch function
example = ClassDispatchExample()
assert example.dispatch_by_type(123) == "This is int"
assert example.dispatch_by_type(123.0) == "This is float"
